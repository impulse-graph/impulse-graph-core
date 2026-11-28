package impulse

/*
#include "impulse_graph.h"
#include "impulse_vm.h"
*/
import "C"

import "fmt"

// KeyType defines domain catalog key type definitions.
type KeyType uint8

const (
	KeyTypeInt8    KeyType = C.IMPULSE_KEY_TYPE_INT8
	KeyTypeInt16   KeyType = C.IMPULSE_KEY_TYPE_INT16
	KeyTypeInt32   KeyType = C.IMPULSE_KEY_TYPE_INT32
	KeyTypeInt64   KeyType = C.IMPULSE_KEY_TYPE_INT64
	KeyTypeUint8   KeyType = 0x05
	KeyTypeUint16  KeyType = 0x06
	KeyTypeUint32  KeyType = 0x07
	KeyTypeUint64  KeyType = 0x08
	KeyTypeFloat32 KeyType = 0x09
	KeyTypeFloat64 KeyType = 0x0A
	KeyTypeString  KeyType = C.IMPULSE_KEY_TYPE_STRING
	KeyTypeUUID    KeyType = C.IMPULSE_KEY_TYPE_UUID
	KeyTypeUUID128 KeyType = 0x0C
)

// EncodingType defines topology CSR encoding enums.
type EncodingType uint8

const (
	EncRaw  EncodingType = C.IMPULSE_ENC_RAW
	EncZstd EncodingType = C.IMPULSE_ENC_ZSTD
)

// Attribute type codes (v0.11).
const (
	AttrTypeBool              uint8 = 0x0F
	AttrTypeDecimal64         uint8 = 0x15
	AttrTypeDecimal64Nullable uint8 = 0x95
)

// Status represents C-ABI status/error return codes.
type Status int32

const (
	StatusOk                     Status = C.IMPULSE_OK
	ErrInvalidMagic              Status = C.IMPULSE_ERR_INVALID_MAGIC
	ErrUnsupportedVersion        Status = C.IMPULSE_ERR_UNSUPPORTED_VERSION
	ErrUnsupportedGlobalFeature  Status = C.IMPULSE_ERR_UNSUPPORTED_GLOBAL_FEATURE
	ErrUnsupportedSectionFeature Status = C.IMPULSE_ERR_UNSUPPORTED_SECTION_FEATURE
	ErrCorruptChecksum           Status = C.IMPULSE_ERR_CORRUPT_CHECKSUM
	ErrIOFailure                 Status = C.IMPULSE_ERR_IO_FAILURE
	ErrInvalidArgument           Status = C.IMPULSE_ERR_INVALID_ARGUMENT
	ErrSignatureMismatch         Status = C.IMPULSE_ERR_SIGNATURE_MISMATCH
	ErrBufferOverflow            Status = C.IMPULSE_ERR_BUFFER_OVERFLOW
)

func (s Status) Error() string {
	switch s {
	case StatusOk:
		return "IMPULSE_OK"
	case ErrInvalidMagic:
		return "IMPULSE_ERR_INVALID_MAGIC"
	case ErrUnsupportedVersion:
		return "IMPULSE_ERR_UNSUPPORTED_VERSION"
	case ErrUnsupportedGlobalFeature:
		return "IMPULSE_ERR_UNSUPPORTED_GLOBAL_FEATURE"
	case ErrUnsupportedSectionFeature:
		return "IMPULSE_ERR_UNSUPPORTED_SECTION_FEATURE"
	case ErrCorruptChecksum:
		return "IMPULSE_ERR_CORRUPT_CHECKSUM"
	case ErrIOFailure:
		return "IMPULSE_ERR_IO_FAILURE"
	case ErrInvalidArgument:
		return "IMPULSE_ERR_INVALID_ARGUMENT"
	case ErrSignatureMismatch:
		return "IMPULSE_ERR_SIGNATURE_MISMATCH"
	case ErrBufferOverflow:
		return "IMPULSE_ERR_BUFFER_OVERFLOW"
	default:
		return fmt.Sprintf("IMPULSE_ERR_UNKNOWN(%d)", int(s))
	}
}

// SnapshotHeader holds active Page 0 snapshot header metadata (impulse_snapshot_header_info_t).
type SnapshotHeader struct {
	Magic                 uint32
	DataOffset            uint32
	TimestampMs           uint64
	RequiredFeatures      uint64
	FooterDirectoryOffset uint64
	FooterDirectoryBytes  uint64
	FileSize              uint64
	Version               uint16
	DomainCount           uint16
	RelationCount         uint16
	IndexCount            uint16
	HeaderChecksum        uint16
	SnapshotUUID          [16]byte
}

// DomainCatalogEntry holds metadata for a node domain in a snapshot (v0.11).
type DomainCatalogEntry struct {
	DomainID     uint16
	KeyType      KeyType
	DenseIDWidth uint8
	NameOffset   uint32
	NodeCount    uint64
	AttrCount    uint16
	KeyAttrIndex uint16
	Name         string
}

// AttributeDescriptor holds metadata for a node domain or edge relation attribute.
type AttributeDescriptor struct {
	Name            string
	TypeCode        uint8
	BaseTypeCode    uint8
	IsNullable      bool
	Has64BitOffsets bool
	Dimension       uint32
	Precision       uint16
	Scale           uint8
	DataOffset      uint64
	DataBytes       uint64
	OffsetsOffset   uint64
	OffsetsBytes    uint64
}

// ValidityRange computes the 128-byte-aligned validity bitmap (offset, byteLength) prefix
// for nullable attributes per FORMAT_SPECIFICATION.md §4.2.
func (a AttributeDescriptor) ValidityRange(elemCount uint64) (uint64, uint64) {
	if !a.IsNullable || elemCount == 0 || a.DataBytes == 0 {
		return 0, 0
	}
	var rawBitsBytes uint64
	if elemCount > ^uint64(0)-7 {
		rawBitsBytes = ^uint64(0) / 8
	} else {
		rawBitsBytes = (elemCount + 7) / 8
	}
	var aligned uint64
	if rawBitsBytes > ^uint64(0)-127 {
		aligned = ^uint64(0)
	} else {
		aligned = ((rawBitsBytes + 127) / 128) * 128
	}
	if aligned > a.DataBytes {
		aligned = a.DataBytes
	}
	return a.DataOffset, aligned
}

// IndexDirectoryEntry holds metadata for a Section 2.6 secondary index descriptor.
type IndexDirectoryEntry struct {
	IndexID            uint32
	DomainID           uint16
	RelationID         uint16
	AttributeIndex     uint16
	IndexType          uint8
	Name               string
	DataOffset         uint64
	DataBytes          uint64
	PayloadFeatureMask uint64
}

// RelationDirectoryEntry holds metadata for a relation directory in a snapshot (v0.11).
type RelationDirectoryEntry struct {
	RelationID      uint16
	Name            string
	SrcDomainID     uint16
	TgtDomainID     uint16
	EncodingID      uint8
	NodeIDWidth     uint8
	EdgeIndexWidth  uint8
	NameOffset      uint32
	NodeCount       uint64
	EdgeCount       uint64
	SectionFeatures uint64
	CSRRowOffOffset uint64
	CSRRowOffBytes  uint64
	CSRColIdxOffset uint64
	CSRColIdxBytes  uint64
	CSCRowOffOffset uint64
	CSCRowOffBytes  uint64
	CSCColIdxOffset uint64
	CSCColIdxBytes  uint64
	AttrCount       uint16
	// v0.11.0 COO Topology Extension Block
	CooSrcOffset    uint64
	CooSrcBytes     uint64
	CooTgtOffset    uint64
	CooTgtBytes     uint64
	CooEdgeIDOffset uint64
	CooEdgeIDBytes  uint64
	CooOrder        uint8
	SnapshotVersion uint16
}

// IsVirtual reports whether the relation is a virtual union relation (bit 2, v0.10+).
func (r RelationDirectoryEntry) IsVirtual() bool {
	if r.SnapshotVersion > 0 && r.SnapshotVersion < 10 {
		return false
	}
	return (r.SectionFeatures & uint64(C.IMP_REL_FEATURE_VIRTUAL)) != 0
}

// HasCSR reports whether forward CSR arrays are present on this relation.
func (r RelationDirectoryEntry) HasCSR() bool {
	return !r.IsVirtual() && (r.CSRRowOffBytes > 0 || r.CSRColIdxBytes > 0)
}

// HasCSC reports whether reverse CSC arrays are present on this relation.
func (r RelationDirectoryEntry) HasCSC() bool {
	return !r.IsVirtual() && (r.CSCRowOffBytes > 0 || r.CSCColIdxBytes > 0)
}

// HasCOO reports whether v0.11 COO arrays are present on this relation.
func (r RelationDirectoryEntry) HasCOO() bool {
	if r.IsVirtual() {
		return false
	}
	hasFlag := (r.SnapshotVersion == 0 || r.SnapshotVersion >= 11) &&
		(r.SectionFeatures&uint64(C.IMPULSE_SECTION_FEAT_HAS_COO)) != 0
	return hasFlag || (r.CooSrcBytes > 0 && r.CooTgtBytes > 0)
}

// IsFlatForward reports whether forward topology is a flat 1:1/M:1 direct array.
func (r RelationDirectoryEntry) IsFlatForward() bool {
	return !r.IsVirtual() && r.EncodingID != 3 && r.CSRRowOffBytes == 0 && r.CSRColIdxBytes > 0
}

// IsFlatReverse reports whether reverse topology is a flat 1:1/1:M direct array.
func (r RelationDirectoryEntry) IsFlatReverse() bool {
	return !r.IsVirtual() && r.EncodingID != 3 && r.CSCRowOffBytes == 0 && r.CSCColIdxBytes > 0
}

// VMStatus represents ImpulseVM execution status return codes.
type VMStatus int32

const (
	VMStatusOk          VMStatus = C.IMPULSE_VM_OK
	VMErrInvalidOpcode  VMStatus = C.IMPULSE_VM_ERR_INVALID_OPCODE
	VMErrOutOfBounds    VMStatus = C.IMPULSE_VM_ERR_OUT_OF_BOUNDS
	VMErrNullSnapshot   VMStatus = C.IMPULSE_VM_ERR_NULL_SNAPSHOT
	VMErrStackOverflow  VMStatus = C.IMPULSE_VM_ERR_STACK_OVERFLOW
	VMErrStackUnderflow VMStatus = C.IMPULSE_VM_ERR_STACK_UNDERFLOW
	VMErrInvalidOperand VMStatus = C.IMPULSE_VM_ERR_INVALID_OPERAND
)

func (v VMStatus) Error() string {
	switch v {
	case VMStatusOk:
		return "IMPULSE_VM_OK"
	case VMErrInvalidOpcode:
		return "IMPULSE_VM_ERR_INVALID_OPCODE"
	case VMErrOutOfBounds:
		return "IMPULSE_VM_ERR_OUT_OF_BOUNDS"
	case VMErrNullSnapshot:
		return "IMPULSE_VM_ERR_NULL_SNAPSHOT"
	case VMErrStackOverflow:
		return "IMPULSE_VM_ERR_STACK_OVERFLOW"
	case VMErrStackUnderflow:
		return "IMPULSE_VM_ERR_STACK_UNDERFLOW"
	case VMErrInvalidOperand:
		return "IMPULSE_VM_ERR_INVALID_OPERAND"
	default:
		return fmt.Sprintf("IMPULSE_VM_ERR_UNKNOWN(%d)", int(v))
	}
}
