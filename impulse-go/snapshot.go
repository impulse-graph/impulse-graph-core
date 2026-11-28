package impulse

/*
#include "impulse_graph.h"
#include "impulse_vm_v11.h"
#include <stdlib.h>

static inline void impulse_go_unpack_attr_desc(
    const impulse_attribute_descriptor_t* d,
    uint8_t* type_code,
    uint8_t* reserved1,
    uint16_t* reserved2,
    uint32_t* dimension,
    uint64_t* data_offset,
    uint64_t* data_bytes,
    uint64_t* offsets_offset,
    uint64_t* offsets_bytes
) {
    *type_code = d->type_code;
    *reserved1 = d->reserved1;
    *reserved2 = d->reserved2;
    *dimension = d->dimension;
    *data_offset = d->data_offset;
    *data_bytes = d->data_bytes;
    *offsets_offset = d->offsets_offset;
    *offsets_bytes = d->offsets_bytes;
}
*/
import "C"

import (
	"runtime"
	"sync"
	"unsafe"
)

// Snapshot represents a read-only memory-mapped Impulse graph snapshot (.imps).
type Snapshot struct {
	ptr    *C.impulse_snapshot_t
	poolMu sync.RWMutex
	wsPool chan *VmWorkspace
	closed bool
}

// OpenSnapshot opens a binary graph snapshot file via zero-copy OS memory-mapping (mmap).
func OpenSnapshot(path string) (*Snapshot, error) {
	cPath := C.CString(path)
	defer C.free(unsafe.Pointer(cPath))

	var status C.impulse_status_t
	ptr := C.impulse_snapshot_open(cPath, &status)
	if ptr == nil || status != C.IMPULSE_OK {
		errStr := C.GoString(C.impulse_get_last_error())
		if errStr != "" {
			return nil, fmtError(Status(status), errStr)
		}
		return nil, Status(status)
	}
	_ = C.impulse_vm_v11_prepare_snapshot(ptr)

	poolCap := runtime.GOMAXPROCS(0) * 2
	if poolCap < 8 {
		poolCap = 8
	}
	s := &Snapshot{
		ptr:    ptr,
		wsPool: make(chan *VmWorkspace, poolCap),
	}
	runtime.SetFinalizer(s, (*Snapshot).Close)
	return s, nil
}

// AcquireWorkspace borrows a pooled 128B-aligned v0.11 VmWorkspace, or allocates one if the pool is empty.
func (s *Snapshot) AcquireWorkspace(scratchBytes uint64) (*VmWorkspace, error) {
	if scratchBytes < 4*1024*1024 {
		scratchBytes = 4 * 1024 * 1024
	}
	s.poolMu.RLock()
	if s.closed || s.ptr == nil {
		s.poolMu.RUnlock()
		return nil, VMErrNullSnapshot
	}
	select {
	case ws := <-s.wsPool:
		s.poolMu.RUnlock()
		if uint64(ws.cap) >= scratchBytes {
			return ws, nil
		}
		ws.Close()
		return NewVmWorkspace(scratchBytes)
	default:
		s.poolMu.RUnlock()
		return NewVmWorkspace(scratchBytes)
	}
}

// ReleaseWorkspace returns a v0.11 VmWorkspace to the Snapshot pool, or frees it if the pool is full or closed.
func (s *Snapshot) ReleaseWorkspace(ws *VmWorkspace) {
	if ws == nil || ws.rawBuf == nil {
		return
	}
	s.poolMu.RLock()
	if s.closed || s.wsPool == nil {
		s.poolMu.RUnlock()
		ws.Close()
		return
	}
	select {
	case s.wsPool <- ws:
		s.poolMu.RUnlock()
	default:
		s.poolMu.RUnlock()
		ws.Close()
	}
}

// Close unmaps physical memory and releases snapshot resources and all pooled workspaces.
func (s *Snapshot) Close() {
	s.poolMu.Lock()
	s.closed = true
	if s.wsPool != nil {
		for {
			select {
			case ws := <-s.wsPool:
				ws.Close()
			default:
				goto drained
			}
		}
	}
drained:
	if s.ptr != nil {
		C.impulse_snapshot_close(s.ptr)
		s.ptr = nil
	}
	runtime.SetFinalizer(s, nil)
	s.poolMu.Unlock()
}

// Ptr returns the raw C pointer handle for internal/FFI calls.
func (s *Snapshot) Ptr() *C.impulse_snapshot_t {
	return s.ptr
}

// Magic returns the 32-bit magic integer (0x494D5053).
func (s *Snapshot) Magic() uint32 {
	s.poolMu.RLock()
	defer s.poolMu.RUnlock()
	defer runtime.KeepAlive(s)
	if s.closed || s.ptr == nil {
		return 0
	}
	return uint32(C.impulse_snapshot_magic(s.ptr))
}

// Version returns the snapshot format version.
func (s *Snapshot) Version() uint16 {
	s.poolMu.RLock()
	defer s.poolMu.RUnlock()
	defer runtime.KeepAlive(s)
	if s.closed || s.ptr == nil {
		return 0
	}
	return uint16(C.impulse_snapshot_version(s.ptr))
}

// Header returns the active Page 0 snapshot header metadata and total file size.
func (s *Snapshot) Header() (SnapshotHeader, error) {
	s.poolMu.RLock()
	defer s.poolMu.RUnlock()
	defer runtime.KeepAlive(s)
	if s.closed || s.ptr == nil {
		return SnapshotHeader{}, ErrInvalidArgument
	}
	var cHdr C.impulse_snapshot_header_info_t
	status := C.impulse_snapshot_get_header(s.ptr, &cHdr)
	if status != C.IMPULSE_OK {
		return SnapshotHeader{}, Status(status)
	}
	hdr := SnapshotHeader{
		Magic:                 uint32(cHdr.magic),
		DataOffset:            uint32(cHdr.data_offset),
		TimestampMs:           uint64(cHdr.timestamp_ms),
		RequiredFeatures:      uint64(cHdr.required_features),
		FooterDirectoryOffset: uint64(cHdr.footer_directory_offset),
		FooterDirectoryBytes:  uint64(cHdr.footer_directory_bytes),
		FileSize:              uint64(cHdr.file_size),
		Version:               uint16(cHdr.version),
		DomainCount:           uint16(cHdr.domain_count),
		RelationCount:         uint16(cHdr.relation_count),
		IndexCount:            uint16(cHdr.index_count),
		HeaderChecksum:        uint16(cHdr.header_checksum),
	}
	for i := 0; i < 16; i++ {
		hdr.SnapshotUUID[i] = byte(cHdr.snapshot_uuid[i])
	}
	return hdr, nil
}

// FileSize returns the total mapped byte size of the snapshot file.
func (s *Snapshot) FileSize() uint64 {
	s.poolMu.RLock()
	defer s.poolMu.RUnlock()
	defer runtime.KeepAlive(s)
	if s.closed || s.ptr == nil {
		return 0
	}
	return uint64(C.impulse_snapshot_file_size(s.ptr))
}

// SHA256 computes the SHA-256 digest over the entire mapped snapshot file.
func (s *Snapshot) SHA256() ([32]byte, error) {
	var out [32]byte
	s.poolMu.RLock()
	defer s.poolMu.RUnlock()
	defer runtime.KeepAlive(s)
	if s.closed || s.ptr == nil {
		return out, ErrInvalidArgument
	}
	status := C.impulse_snapshot_compute_sha256(s.ptr, (*C.uint8_t)(unsafe.Pointer(&out[0])))
	if status != C.IMPULSE_OK {
		return out, Status(status)
	}
	return out, nil
}

// DomainCount returns the number of defined domains in the snapshot catalog.
func (s *Snapshot) DomainCount() uint16 {
	s.poolMu.RLock()
	defer s.poolMu.RUnlock()
	defer runtime.KeepAlive(s)
	if s.closed || s.ptr == nil {
		return 0
	}
	return uint16(C.impulse_snapshot_domain_count(s.ptr))
}

// GetDomainEntry retrieves domain catalog metadata and name by domain index.
func (s *Snapshot) GetDomainEntry(domainIndex uint16) (DomainCatalogEntry, error) {
	s.poolMu.RLock()
	defer s.poolMu.RUnlock()
	defer runtime.KeepAlive(s)
	if s.closed || s.ptr == nil {
		return DomainCatalogEntry{}, ErrInvalidArgument
	}
	var cEntry C.impulse_domain_catalog_entry_t
	var cName *C.char
	status := C.impulse_snapshot_get_domain_entry(s.ptr, C.uint16_t(domainIndex), &cEntry, &cName)
	if status != C.IMPULSE_OK {
		return DomainCatalogEntry{}, Status(status)
	}
	var name string
	if cName != nil {
		name = C.GoString(cName)
	}
	return DomainCatalogEntry{
		DomainID:     uint16(cEntry.domain_id),
		KeyType:      KeyType(cEntry.key_type),
		DenseIDWidth: uint8(cEntry.dense_id_width),
		NameOffset:   uint32(cEntry.name_offset),
		NodeCount:    uint64(cEntry.node_count),
		AttrCount:    uint16(cEntry.attr_count),
		KeyAttrIndex: uint16(cEntry.key_attr_index),
		Name:         name,
	}, nil
}

// GetRelationName retrieves the UTF-8 name of a relation by index.
func (s *Snapshot) GetRelationName(relationIndex uint16) (string, error) {
	s.poolMu.RLock()
	defer s.poolMu.RUnlock()
	defer runtime.KeepAlive(s)
	if s.closed || s.ptr == nil {
		return "", ErrInvalidArgument
	}
	var cName *C.char
	status := C.impulse_snapshot_get_relation_name(s.ptr, C.uint16_t(relationIndex), &cName)
	if status != C.IMPULSE_OK {
		return "", Status(status)
	}
	if cName == nil {
		return "", nil
	}
	return C.GoString(cName), nil
}

// GetRelationVirtualComponents retrieves direct constituent child relation IDs for a virtual super-relation.
func (s *Snapshot) GetRelationVirtualComponents(relationIndex uint16) ([]uint16, error) {
	s.poolMu.RLock()
	defer s.poolMu.RUnlock()
	defer runtime.KeepAlive(s)
	if s.closed || s.ptr == nil {
		return nil, ErrInvalidArgument
	}
	var count C.uint16_t
	status := C.impulse_snapshot_get_relation_virtual_components(s.ptr, C.uint16_t(relationIndex), nil, 0, &count)
	if status != C.IMPULSE_OK {
		return nil, Status(status)
	}
	if count == 0 {
		return nil, nil
	}
	ids := make([]uint16, int(count))
	status = C.impulse_snapshot_get_relation_virtual_components(
		s.ptr,
		C.uint16_t(relationIndex),
		(*C.uint16_t)(unsafe.Pointer(&ids[0])),
		count,
		&count,
	)
	if status != C.IMPULSE_OK {
		return nil, Status(status)
	}
	return ids[:int(count)], nil
}

// RelationCount returns the number of relation directory entries in the snapshot.
func (s *Snapshot) RelationCount() uint16 {
	s.poolMu.RLock()
	defer s.poolMu.RUnlock()
	defer runtime.KeepAlive(s)
	if s.closed || s.ptr == nil {
		return 0
	}
	return uint16(C.impulse_snapshot_relation_count(s.ptr))
}

// RelationIndex looks up a relation index in the snapshot catalog.
func (s *Snapshot) RelationIndex(name string) (uint16, bool) {
	s.poolMu.RLock()
	defer s.poolMu.RUnlock()
	defer runtime.KeepAlive(s)
	if s.closed || s.ptr == nil {
		return 0, false
	}
	count := uint16(C.impulse_snapshot_relation_count(s.ptr))
	for i := uint16(0); i < count; i++ {
		var cName *C.char
		status := C.impulse_snapshot_get_relation_name(s.ptr, C.uint16_t(i), &cName)
		if status == C.IMPULSE_OK && cName != nil && C.GoString(cName) == name {
			return i, true
		}
	}
	return 0, false
}

// GetRelationEntry retrieves relation directory metadata by relation index.
func (s *Snapshot) GetRelationEntry(relationIndex uint16) (RelationDirectoryEntry, error) {
	s.poolMu.RLock()
	defer s.poolMu.RUnlock()
	defer runtime.KeepAlive(s)
	if s.closed || s.ptr == nil {
		return RelationDirectoryEntry{}, ErrInvalidArgument
	}

	var cEntry C.impulse_relation_directory_entry_t
	status := C.impulse_snapshot_get_relation_entry(s.ptr, C.uint16_t(relationIndex), &cEntry)
	if status != C.IMPULSE_OK {
		return RelationDirectoryEntry{}, Status(status)
	}

	entry := RelationDirectoryEntry{
		RelationID:      uint16(cEntry.relation_id),
		SrcDomainID:     uint16(cEntry.src_domain_id),
		TgtDomainID:     uint16(cEntry.tgt_domain_id),
		EncodingID:      uint8(cEntry.encoding_id),
		NodeIDWidth:     uint8(cEntry.node_id_width),
		EdgeIndexWidth:  uint8(cEntry.edge_index_width),
		NameOffset:      uint32(cEntry.name_offset),
		NodeCount:       uint64(cEntry.node_count),
		EdgeCount:       uint64(cEntry.edge_count),
		SectionFeatures: uint64(cEntry.section_features),
		CSRRowOffOffset: uint64(cEntry.csr_row_off_offset),
		CSRRowOffBytes:  uint64(cEntry.csr_row_off_bytes),
		CSRColIdxOffset: uint64(cEntry.csr_col_idx_offset),
		CSRColIdxBytes:  uint64(cEntry.csr_col_idx_bytes),
		CSCRowOffOffset: uint64(cEntry.csc_row_off_offset),
		CSCRowOffBytes:  uint64(cEntry.csc_row_off_bytes),
		CSCColIdxOffset: uint64(cEntry.csc_col_idx_offset),
		CSCColIdxBytes:  uint64(cEntry.csc_col_idx_bytes),
		AttrCount:       uint16(cEntry.attr_count),
		CooSrcOffset:    uint64(cEntry.coo_src_offset),
		CooSrcBytes:     uint64(cEntry.coo_src_bytes),
		CooTgtOffset:    uint64(cEntry.coo_tgt_offset),
		CooTgtBytes:     uint64(cEntry.coo_tgt_bytes),
		CooEdgeIDOffset: uint64(cEntry.coo_edge_id_offset),
		CooEdgeIDBytes:  uint64(cEntry.coo_edge_id_bytes),
		CooOrder:        uint8(cEntry.coo_order),
	}
	return entry, nil
}

// IsReachable tests direct reachability between source and target nodes on a relation.
func (s *Snapshot) IsReachable(relationIndex uint16, srcID, tgtID uint64) bool {
	s.poolMu.RLock()
	defer s.poolMu.RUnlock()
	defer runtime.KeepAlive(s)
	if s.closed || s.ptr == nil {
		return false
	}
	return bool(C.impulse_snapshot_is_reachable(s.ptr, C.uint16_t(relationIndex), C.uint64_t(srcID), C.uint64_t(tgtID)))
}

// SampleNeighbors executes zero-copy SIMD neighborhood sampling for seed nodes on a relation.
func (s *Snapshot) SampleNeighbors(relationIndex uint16, srcNodes []uint64, kSamples int, seed uint64) ([]uint64, []uint64, error) {
	s.poolMu.RLock()
	defer s.poolMu.RUnlock()
	defer runtime.KeepAlive(s)
	if s.closed || s.ptr == nil {
		return nil, nil, ErrInvalidArgument
	}

	numNodes := len(srcNodes)
	var cSrcNodes *C.uint64_t
	if numNodes > 0 {
		cSrcNodes = (*C.uint64_t)(unsafe.Pointer(&srcNodes[0]))
	}

	capacity := numNodes * kSamples
	if kSamples < 0 || capacity <= 0 {
		capacity = 1024 * 1024
	}

	outSrc := make([]uint64, capacity)
	outTgt := make([]uint64, capacity)

	var outCount C.size_t
	status := C.impulse_snapshot_sample_neighbors(
		s.ptr,
		C.uint16_t(relationIndex),
		cSrcNodes,
		C.size_t(numNodes),
		C.int(kSamples),
		C.uint64_t(seed),
		(*C.uint64_t)(unsafe.Pointer(&outSrc[0])),
		(*C.uint64_t)(unsafe.Pointer(&outTgt[0])),
		C.size_t(capacity),
		&outCount,
	)

	if status != C.IMPULSE_OK {
		return nil, nil, Status(status)
	}

	count := int(outCount)
	return outSrc[:count], outTgt[:count], nil
}

// MaxNodeCount returns the maximum node count across all domains in the snapshot.
func (s *Snapshot) MaxNodeCount() uint64 {
	s.poolMu.RLock()
	defer s.poolMu.RUnlock()
	defer runtime.KeepAlive(s)
	if s.closed || s.ptr == nil {
		return 0
	}
	return uint64(C.impulse_snapshot_max_node_count(s.ptr))
}

// GetRelationBuffers returns safe Go copies of the CSR row offsets and column target buffers
// (preventing dangling mmap slice views after Snapshot.Close).
func (s *Snapshot) GetRelationBuffers(relationIndex uint16) (offsets []uint32, targets []uint32, nodeCount uint64, edgeCount uint64, err error) {
	s.poolMu.RLock()
	defer s.poolMu.RUnlock()
	defer runtime.KeepAlive(s)
	if s.closed || s.ptr == nil {
		return nil, nil, 0, 0, ErrInvalidArgument
	}

	var cOffsets *C.uint32_t
	var cTargets *C.uint32_t
	var cNodeCount C.uint64_t
	var cEdgeCount C.uint64_t

	status := C.impulse_snapshot_get_relation_buffers(
		s.ptr,
		C.uint16_t(relationIndex),
		&cOffsets,
		&cTargets,
		&cNodeCount,
		&cEdgeCount,
	)

	if status != C.IMPULSE_OK {
		return nil, nil, 0, 0, Status(status)
	}

	nodeCount = uint64(cNodeCount)
	edgeCount = uint64(cEdgeCount)

	if cOffsets != nil {
		rawOffsets := unsafe.Slice((*uint32)(unsafe.Pointer(cOffsets)), nodeCount+1)
		offsets = make([]uint32, len(rawOffsets))
		copy(offsets, rawOffsets)
	}
	if cTargets != nil {
		rawTargets := unsafe.Slice((*uint32)(unsafe.Pointer(cTargets)), edgeCount)
		targets = make([]uint32, len(rawTargets))
		copy(targets, rawTargets)
	}

	return offsets, targets, nodeCount, edgeCount, nil
}

// GetRelationCSCBuffers returns safe Go copies of the CSC column offsets and row target buffers
// (preventing dangling mmap slice views after Snapshot.Close).
func (s *Snapshot) GetRelationCSCBuffers(relationIndex uint16) (cscOffsets []uint32, cscTargets []uint32, rowCount uint64, edgeCount uint64, err error) {
	s.poolMu.RLock()
	defer s.poolMu.RUnlock()
	defer runtime.KeepAlive(s)
	if s.closed || s.ptr == nil {
		return nil, nil, 0, 0, ErrInvalidArgument
	}

	var cCSCOffsets *C.uint32_t
	var cCSCTargets *C.uint32_t
	var cRowCount C.uint64_t
	var cEdgeCount C.uint64_t

	status := C.impulse_snapshot_get_relation_csc_buffers(
		s.ptr,
		C.uint16_t(relationIndex),
		&cCSCOffsets,
		&cCSCTargets,
		&cRowCount,
		&cEdgeCount,
	)

	if status != C.IMPULSE_OK {
		return nil, nil, 0, 0, Status(status)
	}

	rowCount = uint64(cRowCount)
	edgeCount = uint64(cEdgeCount)

	if cCSCOffsets != nil {
		rawOffsets := unsafe.Slice((*uint32)(unsafe.Pointer(cCSCOffsets)), rowCount+1)
		cscOffsets = make([]uint32, len(rawOffsets))
		copy(cscOffsets, rawOffsets)
	}
	if cCSCTargets != nil {
		rawTargets := unsafe.Slice((*uint32)(unsafe.Pointer(cCSCTargets)), edgeCount)
		cscTargets = make([]uint32, len(rawTargets))
		copy(cscTargets, rawTargets)
	}

	return cscOffsets, cscTargets, rowCount, edgeCount, nil
}

// GetAttributeBuffers returns safe Go copies of the attribute raw data and offset buffers
// (preventing dangling mmap slice views after Snapshot.Close).
func (s *Snapshot) GetAttributeBuffers(relationIndex, attributeIndex uint16) (data []byte, offsets []uint32, typeCode uint8, dimension uint32, err error) {
	s.poolMu.RLock()
	defer s.poolMu.RUnlock()
	defer runtime.KeepAlive(s)
	if s.closed || s.ptr == nil {
		return nil, nil, 0, 0, ErrInvalidArgument
	}

	var cData unsafe.Pointer
	var cDataBytes C.uint64_t
	var cOffsets unsafe.Pointer
	var cOffsetsBytes C.uint64_t
	var cTypeCode C.uint8_t
	var cDimension C.uint32_t

	status := C.impulse_snapshot_get_attribute_buffers(
		s.ptr,
		C.uint16_t(relationIndex),
		C.uint16_t(attributeIndex),
		&cData,
		&cDataBytes,
		&cOffsets,
		&cOffsetsBytes,
		&cTypeCode,
		&cDimension,
	)

	if status != C.IMPULSE_OK {
		return nil, nil, 0, 0, Status(status)
	}

	typeCode = uint8(cTypeCode)
	dimension = uint32(cDimension)

	if cData != nil && cDataBytes > 0 {
		rawData := unsafe.Slice((*byte)(cData), uint64(cDataBytes))
		data = make([]byte, len(rawData))
		copy(data, rawData)
	}
	if cOffsets != nil && cOffsetsBytes > 0 {
		rawOffsets := unsafe.Slice((*uint32)(cOffsets), uint64(cOffsetsBytes)/4)
		offsets = make([]uint32, len(rawOffsets))
		copy(offsets, rawOffsets)
	}

	return data, offsets, typeCode, dimension, nil
}

// GetDomainAttributeCount returns the number of attributes attached to a domain.
func (s *Snapshot) GetDomainAttributeCount(domainIndex uint16) uint16 {
	s.poolMu.RLock()
	defer s.poolMu.RUnlock()
	defer runtime.KeepAlive(s)
	if s.closed || s.ptr == nil {
		return 0
	}
	return uint16(C.impulse_snapshot_get_domain_attribute_count(s.ptr, C.uint16_t(domainIndex)))
}

// GetDomainAttributeBuffers returns safe Go copies of a domain attribute's raw data and offset buffers.
func (s *Snapshot) GetDomainAttributeBuffers(domainIndex, attributeIndex uint16) (data []byte, offsets []uint32, typeCode uint8, dimension uint32, err error) {
	s.poolMu.RLock()
	defer s.poolMu.RUnlock()
	defer runtime.KeepAlive(s)
	if s.closed || s.ptr == nil {
		return nil, nil, 0, 0, ErrInvalidArgument
	}

	var cData unsafe.Pointer
	var cDataBytes C.uint64_t
	var cOffsets unsafe.Pointer
	var cOffsetsBytes C.uint64_t
	var cTypeCode C.uint8_t
	var cDimension C.uint32_t

	status := C.impulse_snapshot_get_domain_attribute_buffers(
		s.ptr,
		C.uint16_t(domainIndex),
		C.uint16_t(attributeIndex),
		&cData,
		&cDataBytes,
		&cOffsets,
		&cOffsetsBytes,
		&cTypeCode,
		&cDimension,
	)

	if status != C.IMPULSE_OK {
		return nil, nil, 0, 0, Status(status)
	}

	typeCode = uint8(cTypeCode)
	dimension = uint32(cDimension)

	if cData != nil && cDataBytes > 0 {
		rawData := unsafe.Slice((*byte)(cData), uint64(cDataBytes))
		data = make([]byte, len(rawData))
		copy(data, rawData)
	}
	if cOffsets != nil && cOffsetsBytes > 0 {
		rawOffsets := unsafe.Slice((*uint32)(cOffsets), uint64(cOffsetsBytes)/4)
		offsets = make([]uint32, len(rawOffsets))
		copy(offsets, rawOffsets)
	}

	return data, offsets, typeCode, dimension, nil
}

// GetDomainAttributeDecimal64Meta retrieves the (precision, scale) metadata for a domain DECIMAL64 attribute.
func (s *Snapshot) GetDomainAttributeDecimal64Meta(domainIndex, attributeIndex uint16) (precision uint16, scale uint8, err error) {
	s.poolMu.RLock()
	defer s.poolMu.RUnlock()
	defer runtime.KeepAlive(s)
	if s.closed || s.ptr == nil {
		return 0, 0, ErrInvalidArgument
	}
	var cPrecision C.uint16_t
	var cScale C.uint8_t
	status := C.impulse_snapshot_get_domain_attribute_decimal64_meta(
		s.ptr,
		C.uint16_t(domainIndex),
		C.uint16_t(attributeIndex),
		&cPrecision,
		&cScale,
	)
	if status != C.IMPULSE_OK {
		return 0, 0, Status(status)
	}
	return uint16(cPrecision), uint8(cScale), nil
}

// GetAttributeDecimal64Meta retrieves the (precision, scale) metadata for a relation DECIMAL64 attribute.
func (s *Snapshot) GetAttributeDecimal64Meta(relationIndex, attributeIndex uint16) (precision uint16, scale uint8, err error) {
	s.poolMu.RLock()
	defer s.poolMu.RUnlock()
	defer runtime.KeepAlive(s)
	if s.closed || s.ptr == nil {
		return 0, 0, ErrInvalidArgument
	}
	var cPrecision C.uint16_t
	var cScale C.uint8_t
	status := C.impulse_snapshot_get_attribute_decimal64_meta(
		s.ptr,
		C.uint16_t(relationIndex),
		C.uint16_t(attributeIndex),
		&cPrecision,
		&cScale,
	)
	if status != C.IMPULSE_OK {
		return 0, 0, Status(status)
	}
	return uint16(cPrecision), uint8(cScale), nil
}

// GetDomainAttributeDecimal64 decodes a domain DECIMAL64 (0x15 or nullable 0x95) attribute column.
func (s *Snapshot) GetDomainAttributeDecimal64(domainIndex, attributeIndex uint16) ([]NullableDecimal64, Decimal64Meta, error) {
	data, _, typeCode, dim, err := s.GetDomainAttributeBuffers(domainIndex, attributeIndex)
	if err != nil {
		return nil, Decimal64Meta{}, err
	}
	precision, scale, err := s.GetDomainAttributeDecimal64Meta(domainIndex, attributeIndex)
	if err != nil {
		return nil, Decimal64Meta{}, err
	}

	var rowCount uint64
	s.poolMu.RLock()
	if !s.closed && s.ptr != nil {
		var domEntry C.impulse_domain_catalog_entry_t
		if C.impulse_snapshot_get_domain_entry(s.ptr, C.uint16_t(domainIndex), &domEntry, nil) == C.IMPULSE_OK {
			rowCount = uint64(domEntry.node_count)
		}
	}
	s.poolMu.RUnlock()
	runtime.KeepAlive(s)

	vals, err := decodeDecimal64Bytes(data, typeCode, precision, scale, rowCount)
	if err != nil {
		return nil, Decimal64Meta{}, err
	}
	meta := Decimal64Meta{
		Precision:  precision,
		Scale:      scale,
		IsNullable: (typeCode & 0x80) != 0,
		Dimension:  dim,
	}
	return vals, meta, nil
}

// GetAttributeDecimal64 decodes a relation DECIMAL64 (0x15 or nullable 0x95) attribute column.
func (s *Snapshot) GetAttributeDecimal64(relationIndex, attributeIndex uint16) ([]NullableDecimal64, Decimal64Meta, error) {
	data, _, typeCode, dim, err := s.GetAttributeBuffers(relationIndex, attributeIndex)
	if err != nil {
		return nil, Decimal64Meta{}, err
	}
	precision, scale, err := s.GetAttributeDecimal64Meta(relationIndex, attributeIndex)
	if err != nil {
		return nil, Decimal64Meta{}, err
	}
	relEntry, err := s.GetRelationEntry(relationIndex)
	if err != nil {
		return nil, Decimal64Meta{}, err
	}

	vals, err := decodeDecimal64Bytes(data, typeCode, precision, scale, relEntry.EdgeCount)
	if err != nil {
		return nil, Decimal64Meta{}, err
	}
	meta := Decimal64Meta{
		Precision:  precision,
		Scale:      scale,
		IsNullable: (typeCode & 0x80) != 0,
		Dimension:  dim,
	}
	return vals, meta, nil
}

// GetAttributeInfo retrieves edge relation attribute metadata without copying payload bytes.
func (s *Snapshot) GetAttributeInfo(relationIndex, attributeIndex uint16) (AttributeDescriptor, error) {
	s.poolMu.RLock()
	defer s.poolMu.RUnlock()
	defer runtime.KeepAlive(s)
	if s.closed || s.ptr == nil {
		return AttributeDescriptor{}, ErrInvalidArgument
	}
	var cDesc C.impulse_attribute_descriptor_t
	var cName *C.char
	status := C.impulse_snapshot_get_attribute_descriptor(
		s.ptr, C.uint16_t(relationIndex), C.uint16_t(attributeIndex), &cDesc, &cName,
	)
	if status != C.IMPULSE_OK {
		return AttributeDescriptor{}, Status(status)
	}
	return buildAttrDescriptor(cDesc, cName), nil
}

// GetDomainAttributeInfo retrieves node domain attribute metadata without copying payload bytes.
func (s *Snapshot) GetDomainAttributeInfo(domainIndex, attributeIndex uint16) (AttributeDescriptor, error) {
	s.poolMu.RLock()
	defer s.poolMu.RUnlock()
	defer runtime.KeepAlive(s)
	if s.closed || s.ptr == nil {
		return AttributeDescriptor{}, ErrInvalidArgument
	}
	var cDesc C.impulse_attribute_descriptor_t
	var cName *C.char
	status := C.impulse_snapshot_get_domain_attribute_descriptor(
		s.ptr, C.uint16_t(domainIndex), C.uint16_t(attributeIndex), &cDesc, &cName,
	)
	if status != C.IMPULSE_OK {
		return AttributeDescriptor{}, Status(status)
	}
	return buildAttrDescriptor(cDesc, cName), nil
}

func buildAttrDescriptor(cDesc C.impulse_attribute_descriptor_t, cName *C.char) AttributeDescriptor {
	var name string
	if cName != nil {
		name = C.GoString(cName)
	}
	var cTC, cR1 C.uint8_t
	var cR2 C.uint16_t
	var cDim C.uint32_t
	var cDataOff, cDataBytes, cOffsOff, cOffsBytes C.uint64_t
	C.impulse_go_unpack_attr_desc(&cDesc, &cTC, &cR1, &cR2, &cDim, &cDataOff, &cDataBytes, &cOffsOff, &cOffsBytes)
	tc := uint8(cTC)
	base := tc & 0x7F
	d := AttributeDescriptor{
		Name:          name,
		TypeCode:      tc,
		BaseTypeCode:  base,
		IsNullable:    (tc & 0x80) != 0,
		Dimension:     uint32(cDim),
		DataOffset:    uint64(cDataOff),
		DataBytes:     uint64(cDataBytes),
		OffsetsOffset: uint64(cOffsOff),
		OffsetsBytes:  uint64(cOffsBytes),
	}
	if base == 0x15 {
		d.Precision = uint16(cR2)
		d.Scale = uint8(cR1)
	}
	return d
}

// IndexCount returns the number of secondary indices in the snapshot.
func (s *Snapshot) IndexCount() uint16 {
	s.poolMu.RLock()
	defer s.poolMu.RUnlock()
	defer runtime.KeepAlive(s)
	if s.closed || s.ptr == nil {
		return 0
	}
	return uint16(C.impulse_snapshot_get_index_count(s.ptr))
}

// GetIndexEntry retrieves secondary index directory metadata by index.
func (s *Snapshot) GetIndexEntry(indexIdx uint16) (IndexDirectoryEntry, error) {
	s.poolMu.RLock()
	defer s.poolMu.RUnlock()
	defer runtime.KeepAlive(s)
	if s.closed || s.ptr == nil {
		return IndexDirectoryEntry{}, ErrInvalidArgument
	}
	var cEntry C.impulse_index_directory_entry_t
	var cName *C.char
	status := C.impulse_snapshot_get_index_entry(s.ptr, C.uint16_t(indexIdx), &cEntry, &cName)
	if status != C.IMPULSE_OK {
		return IndexDirectoryEntry{}, Status(status)
	}
	var name string
	if cName != nil {
		name = C.GoString(cName)
	}
	return IndexDirectoryEntry{
		IndexID:            uint32(cEntry.index_id),
		DomainID:           uint16(cEntry.domain_id),
		RelationID:         uint16(cEntry.relation_id),
		AttributeIndex:     uint16(cEntry.attribute_index),
		IndexType:          uint8(cEntry.index_type),
		Name:               name,
		DataOffset:         uint64(cEntry.data_offset),
		DataBytes:          uint64(cEntry.data_bytes),
		PayloadFeatureMask: uint64(cEntry.payload_feature_mask),
	}, nil
}

// GetAllMetadata returns all custom UTF-8 key-value metadata entries stored in the snapshot footer.
func (s *Snapshot) GetAllMetadata() (map[string]string, error) {
	s.poolMu.RLock()
	defer s.poolMu.RUnlock()
	defer runtime.KeepAlive(s)
	if s.closed || s.ptr == nil {
		return nil, ErrInvalidArgument
	}
	cnt := uint32(C.impulse_snapshot_get_metadata_count(s.ptr))
	out := make(map[string]string, cnt)
	for i := uint32(0); i < cnt; i++ {
		var cKey *C.char
		var cVal *C.char
		if C.impulse_snapshot_get_metadata_entry(s.ptr, C.uint32_t(i), &cKey, &cVal) == C.IMPULSE_OK && cKey != nil && cVal != nil {
			out[C.GoString(cKey)] = C.GoString(cVal)
		}
	}
	return out, nil
}

// GetMetadata retrieves custom snapshot key-value metadata using O(log N) binary search
// over sorted footer metadata entries (supports arbitrary value lengths).
func (s *Snapshot) GetMetadata(key string) (string, error) {
	s.poolMu.RLock()
	defer s.poolMu.RUnlock()
	defer runtime.KeepAlive(s)
	if s.closed || s.ptr == nil {
		return "", ErrInvalidArgument
	}
	cnt := uint32(C.impulse_snapshot_get_metadata_count(s.ptr))
	lo, hi := uint32(0), cnt
	for lo < hi {
		mid := lo + (hi-lo)/2
		var cKey *C.char
		var cVal *C.char
		if C.impulse_snapshot_get_metadata_entry(s.ptr, C.uint32_t(mid), &cKey, &cVal) != C.IMPULSE_OK || cKey == nil {
			return "", ErrInvalidArgument
		}
		k := C.GoString(cKey)
		if k == key {
			if cVal == nil {
				return "", nil
			}
			return C.GoString(cVal), nil
		}
		if k < key {
			lo = mid + 1
		} else {
			hi = mid
		}
	}
	return "", ErrInvalidArgument
}

// CompactToFile compacts base snapshot and live delta layer overlays into a new binary snapshot file on disk.
func (s *Snapshot) CompactToFile(deltas []*DeltaLayer, outputPath string) error {
	s.poolMu.RLock()
	defer s.poolMu.RUnlock()
	defer runtime.KeepAlive(s)
	if s.closed || s.ptr == nil {
		return ErrInvalidArgument
	}

	cPath := C.CString(outputPath)
	defer C.free(unsafe.Pointer(cPath))

	deltaPtrs := make([]*C.impulse_delta_layer_t, len(deltas))
	for i, d := range deltas {
		if d != nil {
			deltaPtrs[i] = d.Ptr()
		}
	}

	var cDeltaArray **C.impulse_delta_layer_t
	if len(deltas) > 0 {
		cDeltaArray = &deltaPtrs[0]
	}

	status := C.impulse_snapshot_compact_to_file(s.ptr, cDeltaArray, C.size_t(len(deltas)), cPath)
	if status != C.IMPULSE_OK {
		errStr := C.GoString(C.impulse_get_last_error())
		if errStr != "" {
			return fmtError(Status(status), errStr)
		}
		return Status(status)
	}

	return nil
}

type wrappedError struct {
	status Status
	msg    string
}

func (w *wrappedError) Error() string {
	return w.status.Error() + ": " + w.msg
}

func fmtError(status Status, msg string) error {
	return &wrappedError{status: status, msg: msg}
}
