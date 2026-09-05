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
	KeyTypeInt8   KeyType = C.IMPULSE_KEY_TYPE_INT8
	KeyTypeInt16  KeyType = C.IMPULSE_KEY_TYPE_INT16
	KeyTypeInt32  KeyType = C.IMPULSE_KEY_TYPE_INT32
	KeyTypeInt64  KeyType = C.IMPULSE_KEY_TYPE_INT64
	KeyTypeUUID   KeyType = C.IMPULSE_KEY_TYPE_UUID
	KeyTypeString KeyType = C.IMPULSE_KEY_TYPE_STRING
)

// EncodingType defines topology CSR encoding enums.
type EncodingType uint8

const (
	EncRaw  EncodingType = C.IMPULSE_ENC_RAW
	EncZstd EncodingType = C.IMPULSE_ENC_ZSTD
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

// RelationDirectoryEntry holds metadata for a relation directory in a snapshot.
type RelationDirectoryEntry struct {
	RelationID      uint16
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
}

// VMStatus represents ImpulseVM execution status return codes.
type VMStatus int32

const (
	VMStatusOk           VMStatus = C.IMPULSE_VM_OK
	VMErrInvalidOpcode   VMStatus = C.IMPULSE_VM_ERR_INVALID_OPCODE
	VMErrOutOfBounds     VMStatus = C.IMPULSE_VM_ERR_OUT_OF_BOUNDS
	VMErrNullSnapshot    VMStatus = C.IMPULSE_VM_ERR_NULL_SNAPSHOT
	VMErrStackOverflow   VMStatus = C.IMPULSE_VM_ERR_STACK_OVERFLOW
	VMErrStackUnderflow  VMStatus = C.IMPULSE_VM_ERR_STACK_UNDERFLOW
	VMErrInvalidRegister VMStatus = C.IMPULSE_VM_ERR_INVALID_REGISTER
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
	case VMErrInvalidRegister:
		return "IMPULSE_VM_ERR_INVALID_REGISTER"
	default:
		return fmt.Sprintf("IMPULSE_VM_ERR_UNKNOWN(%d)", int(v))
	}
}

// VM Register Type Tags
type RegisterType uint8

const (
	TypeNull         RegisterType = C.TYPE_NULL
	TypeInt64        RegisterType = C.TYPE_INT64
	TypeNodeID       RegisterType = C.TYPE_NODE_ID
	TypeRelationID   RegisterType = C.TYPE_RELATION_ID
	TypeBitsetHandle RegisterType = C.TYPE_BITSET_HANDLE
	TypeNodeVector   RegisterType = C.TYPE_NODE_VECTOR
	TypeCSRSpan      RegisterType = C.TYPE_CSR_SPAN
	TypeBoolean      RegisterType = C.TYPE_BOOLEAN
	TypeFloat        RegisterType = C.TYPE_FLOAT
	TypeDouble       RegisterType = C.TYPE_DOUBLE
	TypeValueMap     RegisterType = C.TYPE_VALUE_MAP
	TypeStringVector RegisterType = C.TYPE_STRING_VECTOR
	TypeFloatVector  RegisterType = C.TYPE_FLOAT_VECTOR
	TypeDoubleVector RegisterType = C.TYPE_DOUBLE_VECTOR
	TypeUint64Vector RegisterType = C.TYPE_UINT64_VECTOR
)

// ImpulseVM Opcodes Constants
const (
	OpNop                 uint8 = C.OP_NOP
	OpInitInputNode       uint8 = C.OP_INIT_INPUT_NODE
	OpInitInputSet        uint8 = C.OP_INIT_INPUT_SET
	OpLoadConstInt        uint8 = C.OP_LOAD_CONST_INT
	OpMapKeysToDense      uint8 = C.OP_MAP_KEYS_TO_DENSE
	OpLoadConstFloat      uint8 = C.OP_LOAD_CONST_FLOAT
	OpLoadConstStrPrefix  uint8 = C.OP_LOAD_CONST_STR_PREFIX
	OpCSRWalk             uint8 = C.OP_CSR_WALK
	OpCSRWalkFiltered     uint8 = C.OP_CSR_WALK_FILTERED
	OpCSRDegree           uint8 = C.OP_CSR_DEGREE
	OpCSRWalkPredicate    uint8 = C.OP_CSR_WALK_PREDICATE
	OpNodeFilter          uint8 = C.OP_NODE_FILTER
	OpNodeFilterStrPrefix uint8 = C.OP_NODE_FILTER_STR_PREFIX
	OpCSRWalkReduceSum    uint8 = C.OP_CSR_WALK_REDUCE_SUM
	OpCSRWalkReduce       uint8 = C.OP_CSR_WALK_REDUCE
	OpCSCWalk             uint8 = C.OP_CSC_WALK
	OpSetUnion            uint8 = C.OP_SET_UNION
	OpSetIntersect        uint8 = C.OP_SET_INTERSECT
	OpSetDifference       uint8 = C.OP_SET_DIFFERENCE
	OpSetCardinality      uint8 = C.OP_SET_CARDINALITY
	OpVectorMulAttr       uint8 = C.OP_VECTOR_MUL_ATTR
	OpVectorReduceSum     uint8 = C.OP_VECTOR_REDUCE_SUM
	OpVectorDiv           uint8 = C.OP_VECTOR_DIV
	OpVectorStrConcat     uint8 = C.OP_VECTOR_STR_CONCAT
	OpFloatVectorScale    uint8 = C.OP_FLOAT_VECTOR_SCALE
	OpL1NormDiff          uint8 = C.OP_L1_NORM_DIFF
	OpCCAfforest          uint8 = C.OP_CC_AFFOREST
	OpMxV                 uint8 = C.OP_MXV
	OpVxM                 uint8 = C.OP_VXM
	OpEwiseAdd            uint8 = C.OP_EWISE_ADD
	OpEwiseMult           uint8 = C.OP_EWISE_MULT
	OpReduce              uint8 = C.OP_REDUCE
	OpCCHookCompress      uint8 = C.OP_CC_HOOK_COMPRESS
	OpTCSweepBatch        uint8 = C.OP_TC_SWEEP_BATCH
	OpBrandesForward      uint8 = C.OP_BRANDES_FORWARD
	OpBrandesBackward     uint8 = C.OP_BRANDES_BACKWARD
	OpDeltaStepRelax      uint8 = C.OP_DELTA_STEP_RELAX
	OpReadEdgeWeight      uint8 = C.OP_READ_EDGE_WEIGHT
	OpSampleNeighbors     uint8 = C.OP_SAMPLE_NEIGHBORS
	OpRandomWalk          uint8 = C.OP_RANDOM_WALK
	OpScatterGather       uint8 = C.OP_SCATTER_GATHER
	OpRebacCheck          uint8 = C.OP_REBAC_CHECK
	OpRoaringBitmapAnd    uint8 = C.OP_ROARING_BITMAP_AND
	OpIslandDetect        uint8 = C.OP_ISLAND_DETECT
	OpSparseMatVec        uint8 = C.OP_SPARSE_MATVEC
	OpLouvainModularity   uint8 = C.OP_LOUVAIN_MODULARITY
	OpKcoreDecomposition  uint8 = C.OP_KCORE_DECOMPOSITION
	OpMotifMatch3         uint8 = C.OP_MOTIF_MATCH_3
	OpGraphIsomorphism    uint8 = C.OP_GRAPH_ISOMORPHISM
	OpJmp                 uint8 = C.OP_JMP
	OpJz                  uint8 = C.OP_JZ
	OpJnz                 uint8 = C.OP_JNZ
	OpLoopDecr            uint8 = C.OP_LOOP_DECR
	OpStableCheck         uint8 = C.OP_STABLE_CHECK
	OpCall                uint8 = C.OP_CALL
	OpRet                 uint8 = C.OP_RET
	OpMov                 uint8 = C.OP_MOV
	OpClearReg            uint8 = C.OP_CLEAR_REG
	OpCollectBitset       uint8 = C.OP_COLLECT_BITSET
	OpCollectArray        uint8 = C.OP_COLLECT_ARRAY
	OpMapDenseToKeys      uint8 = C.OP_MAP_DENSE_TO_KEYS
	OpCollectValueMap     uint8 = C.OP_COLLECT_VALUE_MAP
	OpHalt                uint8 = C.OP_HALT
)
