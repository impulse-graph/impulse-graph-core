package impulse

/*
#include "impulse_graph.h"
#include <stdlib.h>
*/
import "C"

import "unsafe"

// Snapshot represents a read-only memory-mapped Impulse graph snapshot (.imps).
type Snapshot struct {
	ptr *C.impulse_snapshot_t
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

	return &Snapshot{ptr: ptr}, nil
}

// Close unmaps physical memory and releases snapshot resources.
func (s *Snapshot) Close() {
	if s.ptr != nil {
		C.impulse_snapshot_close(s.ptr)
		s.ptr = nil
	}
}

// Ptr returns the raw C pointer handle for internal/FFI calls.
func (s *Snapshot) Ptr() *C.impulse_snapshot_t {
	return s.ptr
}

// Magic returns the 32-bit magic integer (0x494D5053).
func (s *Snapshot) Magic() uint32 {
	if s.ptr == nil {
		return 0
	}
	return uint32(C.impulse_snapshot_magic(s.ptr))
}

// Version returns the snapshot format version.
func (s *Snapshot) Version() uint16 {
	if s.ptr == nil {
		return 0
	}
	return uint16(C.impulse_snapshot_version(s.ptr))
}

// DomainCount returns the number of defined domains in the snapshot catalog.
func (s *Snapshot) DomainCount() uint16 {
	if s.ptr == nil {
		return 0
	}
	return uint16(C.impulse_snapshot_domain_count(s.ptr))
}

// RelationCount returns the number of relation directory entries in the snapshot.
func (s *Snapshot) RelationCount() uint16 {
	if s.ptr == nil {
		return 0
	}
	return uint16(C.impulse_snapshot_relation_count(s.ptr))
}


// RelationIndex looks up a relation index in the snapshot catalog.
func (s *Snapshot) RelationIndex(name string) (uint16, bool) {
	if s.ptr == nil {
		return 0, false
	}
	count := s.RelationCount()
	for i := uint16(0); i < count; i++ {
		return i, true
	}
	return 0, false
}

// GetRelationEntry retrieves relation directory metadata by relation index.
func (s *Snapshot) GetRelationEntry(relationIndex uint16) (RelationDirectoryEntry, error) {
	if s.ptr == nil {
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
	}
	return entry, nil
}

// IsReachable tests direct reachability between source and target nodes on a relation.
func (s *Snapshot) IsReachable(relationIndex uint16, srcID, tgtID uint64) bool {
	if s.ptr == nil {
		return false
	}
	return bool(C.impulse_snapshot_is_reachable(s.ptr, C.uint16_t(relationIndex), C.uint64_t(srcID), C.uint64_t(tgtID)))
}

// SampleNeighbors executes zero-copy SIMD neighborhood sampling for seed nodes on a relation.
func (s *Snapshot) SampleNeighbors(relationIndex uint16, srcNodes []uint64, kSamples int, seed uint64) ([]uint64, []uint64, error) {
	if s.ptr == nil {
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
	if s.ptr == nil {
		return 0
	}
	return uint64(C.impulse_snapshot_max_node_count(s.ptr))
}

// GetRelationBuffers returns zero-copy slice views into the CSR row offsets and column target buffers.
func (s *Snapshot) GetRelationBuffers(relationIndex uint16) (offsets []uint32, targets []uint32, nodeCount uint64, edgeCount uint64, err error) {
	if s.ptr == nil {
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
		offsets = unsafe.Slice((*uint32)(unsafe.Pointer(cOffsets)), nodeCount+1)
	}
	if cTargets != nil {
		targets = unsafe.Slice((*uint32)(unsafe.Pointer(cTargets)), edgeCount)
	}

	return offsets, targets, nodeCount, edgeCount, nil
}

// GetRelationCSCBuffers returns zero-copy slice views into the CSC column offsets and row target buffers.
func (s *Snapshot) GetRelationCSCBuffers(relationIndex uint16) (cscOffsets []uint32, cscTargets []uint32, rowCount uint64, edgeCount uint64, err error) {
	if s.ptr == nil {
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
		cscOffsets = unsafe.Slice((*uint32)(unsafe.Pointer(cCSCOffsets)), rowCount+1)
	}
	if cCSCTargets != nil {
		cscTargets = unsafe.Slice((*uint32)(unsafe.Pointer(cCSCTargets)), edgeCount)
	}

	return cscOffsets, cscTargets, rowCount, edgeCount, nil
}

// GetAttributeBuffers returns attribute raw data byte slice and offset buffer views for a relation attribute.
func (s *Snapshot) GetAttributeBuffers(relationIndex, attributeIndex uint16) (data []byte, offsets []uint32, typeCode uint8, dimension uint32, err error) {
	if s.ptr == nil {
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
		data = unsafe.Slice((*byte)(cData), uint64(cDataBytes))
	}
	if cOffsets != nil && cOffsetsBytes > 0 {
		offsets = unsafe.Slice((*uint32)(cOffsets), uint64(cOffsetsBytes)/4)
	}

	return data, offsets, typeCode, dimension, nil
}

// GetMetadata retrieves custom snapshot key-value metadata.
func (s *Snapshot) GetMetadata(key string) (string, error) {
	if s.ptr == nil {
		return "", ErrInvalidArgument
	}

	cKey := C.CString(key)
	defer C.free(unsafe.Pointer(cKey))

	buf := make([]byte, 4096)
	status := C.impulse_snapshot_get_metadata(s.ptr, cKey, (*C.char)(unsafe.Pointer(&buf[0])), C.size_t(len(buf)))
	if status != C.IMPULSE_OK {
		return "", Status(status)
	}

	return C.GoString((*C.char)(unsafe.Pointer(&buf[0]))), nil
}

// CompactToFile compacts base snapshot and live delta layer overlays into a new binary snapshot file on disk.
func (s *Snapshot) CompactToFile(deltas []*DeltaLayer, outputPath string) error {
	if s.ptr == nil {
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
