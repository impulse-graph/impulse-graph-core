// DO NOT EDIT — Auto-generated from v0.11-refactor/isa_v0_11.yaml
// by tools/gen_isa_tables.py (impOps v0.11.0, impb v11).
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

// =============================================================================
// 1. SQLite-Style X-Macro Opcode Lists & Zero-Cost 64-Bit Nibble Macros
// =============================================================================
// X(NAME, CODE, MNEMONIC, PILLAR, ALLOWED_FLAGS, VALID_SUBOP_MASK, MORSEL_ROLE)
#define IMPULSE_V11_ACTIVE_OPCODE_LIST(X) \
  X(OP_HALT, 0x00, HALT, 1, 0x0, 0x0001, Forbidden) \
  X(OP_NOP, 0x01, NOP, 1, 0x0, 0x0001, BodyOk) \
  X(OP_TRAP, 0x02, TRAP, 1, 0x0, 0x0001, Forbidden) \
  X(OP_THROW, 0x03, THROW, 1, 0x0, 0x0003, Forbidden) \
  X(OP_ASSERT, 0x04, ASSERT, 1, 0x0, 0x00FF, Forbidden) \
  X(OP_LOAD_CONST, 0x05, LOAD_CONST, 1, 0x8, 0x00FF, Forbidden) \
  X(OP_LOAD_CONST_POOL, 0x06, LOAD_CONST_POOL, 1, 0x4, 0x0007, Forbidden) \
  X(OP_LOAD_PARAM, 0x07, LOAD_PARAM, 1, 0x4, 0x0007, Forbidden) \
  X(OP_MOV, 0x08, MOV, 1, 0x0, 0x003F, BodyOk) \
  X(OP_SWAP, 0x09, SWAP, 1, 0x0, 0x0007, BodyOk) \
  X(OP_SPILL, 0x0A, SPILL, 1, 0x0, 0x0007, BodyVOnly) \
  X(OP_RELOAD, 0x0B, RELOAD, 1, 0x0, 0x0007, BodyVOnly) \
  X(OP_SCALAR_INT_ALU, 0x0C, SCALAR_INT_ALU, 1, 0x0, 0xFFFF, Forbidden) \
  X(OP_SCALAR_FP_ALU, 0x0D, SCALAR_FP_ALU, 1, 0x0, 0xFFFF, Forbidden) \
  X(OP_SCALAR_3VL, 0x0E, SCALAR_3VL, 1, 0x0, 0xFFFF, Forbidden) \
  X(OP_JMP, 0x0F, JMP, 1, 0x0, 0x0001, FwdJumpOnly) \
  X(OP_JCC, 0x10, JCC, 1, 0x0, 0x03FF, FwdJumpOnly) \
  X(OP_LOOP_DECR, 0x11, LOOP_DECR, 1, 0x0, 0x0001, Forbidden) \
  X(OP_CALL, 0x12, CALL, 1, 0x0, 0x0003, Forbidden) \
  X(OP_RET, 0x13, RET, 1, 0x0, 0x0001, Forbidden) \
  X(OP_SET_MAX_DOP, 0x14, SET_MAX_DOP, 1, 0x0, 0x0003, Forbidden) \
  X(OP_FSEED, 0x20, FSEED, 2, 0x4, 0x001F, Forbidden) \
  X(OP_INIT_DOMAIN_SPAN, 0x21, INIT_DOMAIN_SPAN, 2, 0x0, 0x0001, Forbidden) \
  X(OP_SET_OP, 0x22, SET_OP, 2, 0x0, 0x000F, BodyOk) \
  X(OP_SET_COMPLEMENT, 0x23, SET_COMPLEMENT, 2, 0x0, 0x0001, BodyOk) \
  X(OP_SET_CARDINALITY, 0x24, SET_CARDINALITY, 2, 0x0, 0x001F, Forbidden) \
  X(OP_SET_CONTAINS, 0x25, SET_CONTAINS, 2, 0x0, 0x0003, Forbidden) \
  X(OP_SET_CONVERT, 0x26, SET_CONVERT, 2, 0x4, 0x000F, BodyOk) \
  X(OP_STABLE_CHECK, 0x27, STABLE_CHECK, 2, 0x0, 0x0003, Forbidden) \
  X(OP_WALK, 0x30, WALK, 3, 0x5, 0x03FF, Forbidden) \
  X(OP_WALK_FILTERED, 0x31, WALK_FILTERED, 3, 0x5, 0xFFFF, Forbidden) \
  X(OP_DEGREE, 0x33, DEGREE, 3, 0x1, 0x003F, Forbidden) \
  X(OP_SPMV, 0x34, SPMV, 3, 0x7, 0x01FF, Forbidden) \
  X(OP_WALK_MORSEL, 0x35, WALK_MORSEL, 3, 0x1, 0x000F, Header) \
  X(OP_FILTER_MORSEL, 0x36, FILTER_MORSEL, 3, 0x0, 0x0001, Header) \
  X(OP_MORSEL_COMMIT, 0x37, MORSEL_COMMIT, 3, 0x7, 0x01FF, CommitOnly) \
  X(OP_MORSEL_END, 0x38, MORSEL_END, 3, 0x0, 0x0001, EndOnly) \
  X(OP_SAMPLE_NEIGHBORS, 0x39, SAMPLE_NEIGHBORS, 3, 0x0, 0x000F, Forbidden) \
  X(OP_HAS_TOPOLOGY, 0x3A, HAS_TOPOLOGY, 3, 0x0, 0x03FF, Forbidden) \
  X(OP_GRAPH_KERNEL, 0x3B, GRAPH_KERNEL, 3, 0x1, 0x001F, Forbidden) \
  X(OP_LOAD_COLUMN, 0x40, LOAD_COLUMN, 4, 0x0, 0x0003, Forbidden) \
  X(OP_GATHER_NODE_ATTR, 0x41, GATHER_NODE_ATTR, 4, 0x1, 0x0007, BodyOk) \
  X(OP_GATHER_EDGE_ATTR, 0x42, GATHER_EDGE_ATTR, 4, 0x1, 0x0003, BodyOk) \
  X(OP_GATHER_VEC, 0x43, GATHER_VEC, 4, 0x1, 0x0001, BodyOk) \
  X(OP_NODE_FILTER, 0x44, NODE_FILTER, 4, 0x6, 0x01FF, Forbidden) \
  X(OP_VEC_CMP, 0x45, VEC_CMP, 4, 0x3, 0x00FF, BodyOk) \
  X(OP_VEC_3VL_OP, 0x46, VEC_3VL_OP, 4, 0x3, 0x0FFF, BodyOk) \
  X(OP_VEC_STR_PRED, 0x47, VEC_STR_PRED, 4, 0x3, 0x03FF, BodyOk) \
  X(OP_VEC_MATH_ARITH, 0x48, VEC_MATH_ARITH, 4, 0x3, 0xFFFF, BodyOk) \
  X(OP_VEC_MATH_EXP_LOG, 0x49, VEC_MATH_EXP_LOG, 4, 0x3, 0x3FFF, BodyOk) \
  X(OP_VEC_MATH_TRIG_HYP, 0x4A, VEC_MATH_TRIG_HYP, 4, 0x3, 0x3FFF, BodyOk) \
  X(OP_VEC_MATH_ACT, 0x4B, VEC_MATH_ACT, 4, 0x3, 0x003F, BodyOk) \
  X(OP_VEC_INT_BITWISE, 0x4C, VEC_INT_BITWISE, 4, 0x3, 0x3FFF, BodyOk) \
  X(OP_VEC_TERNARY, 0x4D, VEC_TERNARY, 4, 0x2, 0x03FF, BodyOk) \
  X(OP_VEC_BLEND, 0x4E, VEC_BLEND, 4, 0x2, 0x000F, BodyOk) \
  X(OP_COALESCE, 0x4F, COALESCE, 4, 0x3, 0x0001, BodyOk) \
  X(OP_VEC_INIT, 0x50, VEC_INIT, 4, 0x2, 0x7F7F, Forbidden) \
  X(OP_VEC_POINT, 0x51, VEC_POINT, 4, 0x0, 0x000F, BodyCastOnly) \
  X(OP_VEC_REDUCE, 0x52, VEC_REDUCE, 4, 0x1, 0x07FF, Forbidden) \
  X(OP_TOP_K, 0x53, TOP_K, 4, 0x3, 0x000F, Forbidden) \
  X(OP_INDEX_SCAN, 0x54, INDEX_SCAN, 4, 0x4, 0x007F, Forbidden) \
  X(OP_MAP_KEYS_TO_DENSE, 0x55, MAP_KEYS_TO_DENSE, 4, 0x4, 0x001F, Forbidden) \
  X(OP_MAP_DENSE_TO_KEYS, 0x56, MAP_DENSE_TO_KEYS, 4, 0x0, 0x0007, Forbidden) \
  X(OP_COLLECT, 0x57, COLLECT, 4, 0x1, 0x001F, Forbidden) \
  X(OP_LANE_INIT_SEEDS, 0x60, LANE_INIT_SEEDS, 5, 0x0, 0x0003, Forbidden) \
  X(OP_LANE_INIT_GROUPS, 0x61, LANE_INIT_GROUPS, 5, 0x4, 0x0001, Forbidden) \
  X(OP_LANE_WALK, 0x63, LANE_WALK, 5, 0x5, 0x03FF, Forbidden) \
  X(OP_LANE_BITWISE, 0x64, LANE_BITWISE, 5, 0x2, 0x000F, Forbidden) \
  X(OP_LANE_FILTER_QUORUM, 0x65, LANE_FILTER_QUORUM, 5, 0x6, 0x0007, Forbidden) \
  X(OP_LANE_REDUCE_AND, 0x66, LANE_REDUCE_AND, 5, 0x6, 0x0001, Forbidden) \
  X(OP_LANE_REDUCE_ATTR, 0x67, LANE_REDUCE_ATTR, 5, 0x0, 0x000F, Forbidden) \
  X(OP_VECTOR_ANN_SEARCH, 0x69, VECTOR_ANN_SEARCH, 5, 0x1, 0x0003, Forbidden) \
  X(OP_VECTOR_RERANK, 0x6A, VECTOR_RERANK, 5, 0x1, 0x00FF, BodyPairOnly) \
  X(OP_LANE_STATE_INIT, 0x6B, LANE_STATE_INIT, 5, 0x2, 0x003F, Forbidden) \
  X(OP_LANE_STATE_WALK, 0x6C, LANE_STATE_WALK, 5, 0x7, 0x03FF, Forbidden) \
  X(OP_LANE_STATE_OP, 0x6D, LANE_STATE_OP, 5, 0x2, 0x001F, Forbidden) \
  X(OP_LANE_STATE_REDUCE, 0x6E, LANE_STATE_REDUCE, 5, 0x2, 0x007F, Forbidden) \
  X(OP_VECTOR_POOL, 0x6F, VECTOR_POOL, 5, 0x0, 0x000F, Forbidden)

#define IMPULSE_V11_OPCODE_LIST(X) \
  IMPULSE_V11_ACTIVE_OPCODE_LIST(X) \
  X(OP_EXTENSION_PAYLOAD, 0xFF, EXTENSION_PAYLOAD, 0, 0x0, 0x0001, Forbidden)

// 64-bit instruction word field extraction macros (little-endian nibble layout):
// [imm32:32 | s2:4 | s3:4 | dst:4 | s1:4 | flags:4 | subop:4 | opcode:8]
#define IMP_V11_OPCODE(w)    (static_cast<uint8_t>((static_cast<uint64_t>(w)) & 0xFFULL))
#define IMP_V11_SUBOP(w)     (static_cast<uint8_t>(((static_cast<uint64_t>(w)) >> 8) & 0xFULL))
#define IMP_V11_FLAGS(w)     (static_cast<uint8_t>(((static_cast<uint64_t>(w)) >> 12) & 0xFULL))
#define IMP_V11_S1(w)        (static_cast<uint8_t>(((static_cast<uint64_t>(w)) >> 16) & 0xFULL))
#define IMP_V11_DST(w)       (static_cast<uint8_t>(((static_cast<uint64_t>(w)) >> 20) & 0xFULL))
#define IMP_V11_S3(w)        (static_cast<uint8_t>(((static_cast<uint64_t>(w)) >> 24) & 0xFULL))
#define IMP_V11_S2(w)        (static_cast<uint8_t>(((static_cast<uint64_t>(w)) >> 28) & 0xFULL))
#define IMP_V11_IMM32(w)     (static_cast<uint32_t>(((static_cast<uint64_t>(w)) >> 32) & 0xFFFFFFFFULL))
#define IMP_V11_IMM32_S(w)   (static_cast<int32_t>(IMP_V11_IMM32(w)))
#define IMP_V11_IMM16_LO(w)  (static_cast<uint16_t>(IMP_V11_IMM32(w) & 0xFFFFu))
#define IMP_V11_IMM16_HI(w)  (static_cast<uint16_t>((IMP_V11_IMM32(w) >> 16) & 0xFFFFu))

namespace impulse::vm {

inline constexpr uint32_t kImpbMagic = 0x494D5042u;
inline constexpr uint16_t kImpbVersion = 11u;
inline constexpr std::size_t kActiveOpcodeCount = 78u;

enum ModifierFlag : uint8_t {
  FLAG_MASKED_S3  = 0x1u,
  FLAG_SCALAR_RHS = 0x2u,
  FLAG_ACCUM      = 0x4u,
  FLAG_EXTENDED   = 0x8u,
};

enum VmConditionFlag : uint16_t {
  VM_FLAG_ZF = 0x0001u,
  VM_FLAG_ST = 0x0002u,
  VM_FLAG_NF = 0x0004u,
};

enum class Status : uint32_t {
  IMPULSE_VM_OK = 0u,
  IMPULSE_VM_ERR_INVALID_OPCODE = 1u,
  IMPULSE_VM_ERR_OUT_OF_BOUNDS = 2u,
  IMPULSE_VM_ERR_NULL_SNAPSHOT = 3u,
  IMPULSE_VM_ERR_STACK_OVERFLOW = 4u,
  IMPULSE_VM_ERR_STACK_UNDERFLOW = 5u,
  IMPULSE_VM_ERR_INVALID_OPERAND = 6u,
  IMPULSE_VM_ERR_USER_THROW = 7u,
  IMPULSE_VM_ERR_ASSERTION_FAILED = 8u,
  IMPULSE_VM_ERR_TRAP = 9u,
  IMPULSE_VM_ERR_DOMAIN_MISMATCH = 10u,
  IMPULSE_VM_ERR_BUFFER_OVERFLOW = 11u,
  IMPULSE_VM_ERR_FLOATING_POINT = 12u,
  IMPULSE_VM_ERR_GAS_EXHAUSTED = 13u,
  IMPULSE_VM_ERR_TYPE_MISMATCH = 14u,
  IMPULSE_VM_ERR_VALIDATION_FAILED = 15u,
};

enum class ScalarType : uint8_t {
  SCALAR_NULL_UNSET = 0u,
  SCALAR_I64 = 1u,
  SCALAR_U64 = 2u,
  SCALAR_F64 = 3u,
  SCALAR_BOOL_3VL = 4u,
  SCALAR_STR_HANDLE = 5u,
  SCALAR_F32_RAW = 6u,
};

enum class MorselRole : uint8_t {
  Forbidden = 0u,
  BodyOk = 1u,
  BodyCastOnly = 2u,
  BodyVOnly = 3u,
  BodyPairOnly = 4u,
  FwdJumpOnly = 5u,
  Header = 6u,
  CommitOnly = 7u,
  EndOnly = 8u,
};

enum class Opcode : uint8_t {
#define IMP_V11_ENUM_ENTRY(NAME, CODE, MNEM, PILLAR, FLAGS, MASK, ROLE) NAME = CODE,
  IMPULSE_V11_OPCODE_LIST(IMP_V11_ENUM_ENTRY)
#undef IMP_V11_ENUM_ENTRY
};

namespace subop {
inline constexpr uint8_t kHALT_OK = 0u;
inline constexpr uint8_t kNOP_NOP = 0u;
inline constexpr uint8_t kTRAP_TRAP = 0u;
inline constexpr uint8_t kTHROW_IMM = 0u;
inline constexpr uint8_t kTHROW_REG = 1u;
inline constexpr uint8_t kASSERT_TRUE_R = 0u;
inline constexpr uint8_t kASSERT_NONEMPTY_F = 1u;
inline constexpr uint8_t kASSERT_EMPTY_F = 2u;
inline constexpr uint8_t kASSERT_ZF_SET = 3u;
inline constexpr uint8_t kASSERT_ZF_CLEAR = 4u;
inline constexpr uint8_t kASSERT_ST_SET = 5u;
inline constexpr uint8_t kASSERT_NOT_NULL_R = 6u;
inline constexpr uint8_t kASSERT_IS_NULL_R = 7u;
inline constexpr uint8_t kLOAD_CONST_I32 = 0u;
inline constexpr uint8_t kLOAD_CONST_U32 = 1u;
inline constexpr uint8_t kLOAD_CONST_F32 = 2u;
inline constexpr uint8_t kLOAD_CONST_F32_RAW = 3u;
inline constexpr uint8_t kLOAD_CONST_I64 = 4u;
inline constexpr uint8_t kLOAD_CONST_U64 = 5u;
inline constexpr uint8_t kLOAD_CONST_F64 = 6u;
inline constexpr uint8_t kLOAD_CONST_NULL = 7u;
inline constexpr uint8_t kLOAD_CONST_POOL_VEC = 0u;
inline constexpr uint8_t kLOAD_CONST_POOL_FRONTIER = 1u;
inline constexpr uint8_t kLOAD_CONST_POOL_STR = 2u;
inline constexpr uint8_t kLOAD_PARAM_SCALAR = 0u;
inline constexpr uint8_t kLOAD_PARAM_FRONTIER = 1u;
inline constexpr uint8_t kLOAD_PARAM_VEC = 2u;
inline constexpr uint8_t kMOV_R = 0u;
inline constexpr uint8_t kMOV_F = 1u;
inline constexpr uint8_t kMOV_V = 2u;
inline constexpr uint8_t kMOV_CLEAR_R = 3u;
inline constexpr uint8_t kMOV_CLEAR_F = 4u;
inline constexpr uint8_t kMOV_CLEAR_V = 5u;
inline constexpr uint8_t kSWAP_R = 0u;
inline constexpr uint8_t kSWAP_F = 1u;
inline constexpr uint8_t kSWAP_V = 2u;
inline constexpr uint8_t kSPILL_R = 0u;
inline constexpr uint8_t kSPILL_F = 1u;
inline constexpr uint8_t kSPILL_V = 2u;
inline constexpr uint8_t kRELOAD_R = 0u;
inline constexpr uint8_t kRELOAD_F = 1u;
inline constexpr uint8_t kRELOAD_V = 2u;
inline constexpr uint8_t kSCALAR_INT_ALU_ADD = 0u;
inline constexpr uint8_t kSCALAR_INT_ALU_SUB = 1u;
inline constexpr uint8_t kSCALAR_INT_ALU_MUL = 2u;
inline constexpr uint8_t kSCALAR_INT_ALU_DIV_S = 3u;
inline constexpr uint8_t kSCALAR_INT_ALU_DIV_U = 4u;
inline constexpr uint8_t kSCALAR_INT_ALU_REM_S = 5u;
inline constexpr uint8_t kSCALAR_INT_ALU_REM_U = 6u;
inline constexpr uint8_t kSCALAR_INT_ALU_AND = 7u;
inline constexpr uint8_t kSCALAR_INT_ALU_OR = 8u;
inline constexpr uint8_t kSCALAR_INT_ALU_XOR = 9u;
inline constexpr uint8_t kSCALAR_INT_ALU_NOT = 10u;
inline constexpr uint8_t kSCALAR_INT_ALU_SHL = 11u;
inline constexpr uint8_t kSCALAR_INT_ALU_SHR_U = 12u;
inline constexpr uint8_t kSCALAR_INT_ALU_SHR_S = 13u;
inline constexpr uint8_t kSCALAR_INT_ALU_MIN_S = 14u;
inline constexpr uint8_t kSCALAR_INT_ALU_MAX_S = 15u;
inline constexpr uint8_t kSCALAR_FP_ALU_ADD = 0u;
inline constexpr uint8_t kSCALAR_FP_ALU_SUB = 1u;
inline constexpr uint8_t kSCALAR_FP_ALU_MUL = 2u;
inline constexpr uint8_t kSCALAR_FP_ALU_DIV = 3u;
inline constexpr uint8_t kSCALAR_FP_ALU_NEG = 4u;
inline constexpr uint8_t kSCALAR_FP_ALU_ABS = 5u;
inline constexpr uint8_t kSCALAR_FP_ALU_SQRT = 6u;
inline constexpr uint8_t kSCALAR_FP_ALU_MIN = 7u;
inline constexpr uint8_t kSCALAR_FP_ALU_MAX = 8u;
inline constexpr uint8_t kSCALAR_FP_ALU_FMA = 9u;
inline constexpr uint8_t kSCALAR_FP_ALU_FLOOR = 10u;
inline constexpr uint8_t kSCALAR_FP_ALU_CEIL = 11u;
inline constexpr uint8_t kSCALAR_FP_ALU_ROUND = 12u;
inline constexpr uint8_t kSCALAR_FP_ALU_I64_TO_F64 = 13u;
inline constexpr uint8_t kSCALAR_FP_ALU_F64_TO_I64 = 14u;
inline constexpr uint8_t kSCALAR_FP_ALU_SAFE_DIV = 15u;
inline constexpr uint8_t kSCALAR_3VL_CMP_EQ_I64 = 0u;
inline constexpr uint8_t kSCALAR_3VL_CMP_NE_I64 = 1u;
inline constexpr uint8_t kSCALAR_3VL_CMP_LT_I64 = 2u;
inline constexpr uint8_t kSCALAR_3VL_CMP_LE_I64 = 3u;
inline constexpr uint8_t kSCALAR_3VL_CMP_LT_U64 = 4u;
inline constexpr uint8_t kSCALAR_3VL_CMP_LE_U64 = 5u;
inline constexpr uint8_t kSCALAR_3VL_CMP_EQ_F64 = 6u;
inline constexpr uint8_t kSCALAR_3VL_CMP_NE_F64 = 7u;
inline constexpr uint8_t kSCALAR_3VL_CMP_LT_F64 = 8u;
inline constexpr uint8_t kSCALAR_3VL_CMP_LE_F64 = 9u;
inline constexpr uint8_t kSCALAR_3VL_AND = 10u;
inline constexpr uint8_t kSCALAR_3VL_OR = 11u;
inline constexpr uint8_t kSCALAR_3VL_NOT = 12u;
inline constexpr uint8_t kSCALAR_3VL_IS_NULL = 13u;
inline constexpr uint8_t kSCALAR_3VL_IS_NOT_NULL = 14u;
inline constexpr uint8_t kSCALAR_3VL_COALESCE = 15u;
inline constexpr uint8_t kJMP_JMP = 0u;
inline constexpr uint8_t kJCC_JZ = 0u;
inline constexpr uint8_t kJCC_JNZ = 1u;
inline constexpr uint8_t kJCC_JST = 2u;
inline constexpr uint8_t kJCC_JNST = 3u;
inline constexpr uint8_t kJCC_JNULL = 4u;
inline constexpr uint8_t kJCC_JNNULL = 5u;
inline constexpr uint8_t kJCC_JTRUE_R = 6u;
inline constexpr uint8_t kJCC_JFALSE_OR_NULL_R = 7u;
inline constexpr uint8_t kJCC_JEMPTY_F = 8u;
inline constexpr uint8_t kJCC_JNONEMPTY_F = 9u;
inline constexpr uint8_t kLOOP_DECR_DECR = 0u;
inline constexpr uint8_t kCALL_STRIDE = 0u;
inline constexpr uint8_t kCALL_FULL16 = 1u;
inline constexpr uint8_t kRET_RET = 0u;
inline constexpr uint8_t kSET_MAX_DOP_IMM = 0u;
inline constexpr uint8_t kSET_MAX_DOP_REG = 1u;
inline constexpr uint8_t kFSEED_EMPTY = 0u;
inline constexpr uint8_t kFSEED_SINGLE_IMM = 1u;
inline constexpr uint8_t kFSEED_SINGLE_REG = 2u;
inline constexpr uint8_t kFSEED_INSERT_REG = 3u;
inline constexpr uint8_t kFSEED_REMOVE_REG = 4u;
inline constexpr uint8_t kINIT_DOMAIN_SPAN_ALL_SPAN = 0u;
inline constexpr uint8_t kSET_OP_UNION = 0u;
inline constexpr uint8_t kSET_OP_INTERSECT = 1u;
inline constexpr uint8_t kSET_OP_DIFF = 2u;
inline constexpr uint8_t kSET_OP_SYM_DIFF = 3u;
inline constexpr uint8_t kSET_COMPLEMENT_COMPLEMENT = 0u;
inline constexpr uint8_t kSET_CARDINALITY_CARD_1BIT = 0u;
inline constexpr uint8_t kSET_CARDINALITY_LANE_POP_SUM = 1u;
inline constexpr uint8_t kSET_CARDINALITY_LANE_ANY_NODES = 2u;
inline constexpr uint8_t kSET_CARDINALITY_LANE_ALL_NODES = 3u;
inline constexpr uint8_t kSET_CARDINALITY_SINGLE_LANE = 4u;
inline constexpr uint8_t kSET_CONTAINS_CONTAINS_1BIT = 0u;
inline constexpr uint8_t kSET_CONTAINS_GET_LANE_MASK = 1u;
inline constexpr uint8_t kSET_CONVERT_F2V_IDS = 0u;
inline constexpr uint8_t kSET_CONVERT_V2F_IDS = 1u;
inline constexpr uint8_t kSET_CONVERT_B2F = 2u;
inline constexpr uint8_t kSET_CONVERT_F2B = 3u;
inline constexpr uint8_t kSTABLE_CHECK_FRONTIER_EQ = 0u;
inline constexpr uint8_t kSTABLE_CHECK_VEC_L1 = 1u;
inline constexpr uint8_t kWALK_FWD = 0u;
inline constexpr uint8_t kWALK_REV = 1u;
inline constexpr uint8_t kWALK_BOTH = 2u;
inline constexpr uint8_t kWALK_FWD_FIXPOINT_PLUS = 3u;
inline constexpr uint8_t kWALK_FWD_FIXPOINT_STAR = 4u;
inline constexpr uint8_t kWALK_REV_FIXPOINT_PLUS = 5u;
inline constexpr uint8_t kWALK_REV_FIXPOINT_STAR = 6u;
inline constexpr uint8_t kWALK_FWD_BOUNDED = 7u;
inline constexpr uint8_t kWALK_COO_FWD = 8u;
inline constexpr uint8_t kWALK_COO_REV = 9u;
inline constexpr uint8_t kWALK_FILTERED_EQ = 0u;
inline constexpr uint8_t kWALK_FILTERED_NE = 1u;
inline constexpr uint8_t kWALK_FILTERED_LT = 2u;
inline constexpr uint8_t kWALK_FILTERED_LE = 3u;
inline constexpr uint8_t kWALK_FILTERED_GT = 4u;
inline constexpr uint8_t kWALK_FILTERED_GE = 5u;
inline constexpr uint8_t kWALK_FILTERED_IS_NOT_NULL = 6u;
inline constexpr uint8_t kWALK_FILTERED_IS_NULL = 7u;
inline constexpr uint8_t kWALK_FILTERED_REV_EQ = 8u;
inline constexpr uint8_t kWALK_FILTERED_REV_NE = 9u;
inline constexpr uint8_t kWALK_FILTERED_REV_LT = 10u;
inline constexpr uint8_t kWALK_FILTERED_REV_LE = 11u;
inline constexpr uint8_t kWALK_FILTERED_REV_GT = 12u;
inline constexpr uint8_t kWALK_FILTERED_REV_GE = 13u;
inline constexpr uint8_t kWALK_FILTERED_REV_IS_NOT_NULL = 14u;
inline constexpr uint8_t kWALK_FILTERED_REV_IS_NULL = 15u;
inline constexpr uint8_t kDEGREE_SUM_FWD_R = 0u;
inline constexpr uint8_t kDEGREE_SUM_REV_R = 1u;
inline constexpr uint8_t kDEGREE_VEC_FWD_V = 2u;
inline constexpr uint8_t kDEGREE_VEC_REV_V = 3u;
inline constexpr uint8_t kDEGREE_NODE_FWD_R = 4u;
inline constexpr uint8_t kDEGREE_NODE_REV_R = 5u;
inline constexpr uint8_t kSPMV_FWD_PLUS_TIMES = 0u;
inline constexpr uint8_t kSPMV_REV_PLUS_TIMES = 1u;
inline constexpr uint8_t kSPMV_FWD_MIN_PLUS = 2u;
inline constexpr uint8_t kSPMV_REV_MIN_PLUS = 3u;
inline constexpr uint8_t kSPMV_FWD_MAX_PLUS = 4u;
inline constexpr uint8_t kSPMV_FWD_MAX_TIMES = 5u;
inline constexpr uint8_t kSPMV_FWD_MIN_MAX = 6u;
inline constexpr uint8_t kSPMV_FWD_FIRST_SRC = 7u;
inline constexpr uint8_t kSPMV_FWD_ARGMIN_PLUS = 8u;
inline constexpr uint8_t kWALK_MORSEL_FWD = 0u;
inline constexpr uint8_t kWALK_MORSEL_REV = 1u;
inline constexpr uint8_t kWALK_MORSEL_COO_FWD = 2u;
inline constexpr uint8_t kWALK_MORSEL_COO_REV = 3u;
inline constexpr uint8_t kFILTER_MORSEL_NODE = 0u;
inline constexpr uint8_t kMORSEL_COMMIT_YIELD_FRONTIER = 0u;
inline constexpr uint8_t kMORSEL_COMMIT_YIELD_LANE_OR = 1u;
inline constexpr uint8_t kMORSEL_COMMIT_SCATTER_ADD = 2u;
inline constexpr uint8_t kMORSEL_COMMIT_SCATTER_MIN = 3u;
inline constexpr uint8_t kMORSEL_COMMIT_SCATTER_MAX = 4u;
inline constexpr uint8_t kMORSEL_COMMIT_SCATTER_MIN_SRC = 5u;
inline constexpr uint8_t kMORSEL_COMMIT_SCATTER_ARGMIN = 6u;
inline constexpr uint8_t kMORSEL_COMMIT_SCATTER_ARGMAX = 7u;
inline constexpr uint8_t kMORSEL_COMMIT_COUNT_ADD_R = 8u;
inline constexpr uint8_t kMORSEL_END_END = 0u;
inline constexpr uint8_t kSAMPLE_NEIGHBORS_FRONTIER_WOR = 0u;
inline constexpr uint8_t kSAMPLE_NEIGHBORS_VEC_WR = 1u;
inline constexpr uint8_t kSAMPLE_NEIGHBORS_RW_STEP_FWD = 2u;
inline constexpr uint8_t kSAMPLE_NEIGHBORS_RW_STEP_STAY = 3u;
inline constexpr uint8_t kHAS_TOPOLOGY_HAS_CSR = 0u;
inline constexpr uint8_t kHAS_TOPOLOGY_HAS_CSC = 1u;
inline constexpr uint8_t kHAS_TOPOLOGY_HAS_COO = 2u;
inline constexpr uint8_t kHAS_TOPOLOGY_HAS_DENSE = 3u;
inline constexpr uint8_t kHAS_TOPOLOGY_HAS_KEY_CAT = 4u;
inline constexpr uint8_t kHAS_TOPOLOGY_HAS_NODE_ATTR = 5u;
inline constexpr uint8_t kHAS_TOPOLOGY_HAS_EDGE_ATTR = 6u;
inline constexpr uint8_t kHAS_TOPOLOGY_HAS_INDEX = 7u;
inline constexpr uint8_t kHAS_TOPOLOGY_DOMAIN_CARD = 8u;
inline constexpr uint8_t kHAS_TOPOLOGY_REL_EDGES = 9u;
inline constexpr uint8_t kGRAPH_KERNEL_CC_AFFOREST = 0u;
inline constexpr uint8_t kGRAPH_KERNEL_KCORE_NUMS = 1u;
inline constexpr uint8_t kGRAPH_KERNEL_KCORE_FRONTIER = 2u;
inline constexpr uint8_t kGRAPH_KERNEL_TRI_COUNT_TOTAL = 3u;
inline constexpr uint8_t kGRAPH_KERNEL_TRI_COUNT_VEC = 4u;
inline constexpr uint8_t kLOAD_COLUMN_NODE_ATTR = 0u;
inline constexpr uint8_t kLOAD_COLUMN_EDGE_ATTR = 1u;
inline constexpr uint8_t kGATHER_NODE_ATTR_VEC = 0u;
inline constexpr uint8_t kGATHER_NODE_ATTR_FRONTIER_COMPACT = 1u;
inline constexpr uint8_t kGATHER_NODE_ATTR_SCALAR = 2u;
inline constexpr uint8_t kGATHER_EDGE_ATTR_VEC = 0u;
inline constexpr uint8_t kGATHER_EDGE_ATTR_SCALAR = 1u;
inline constexpr uint8_t kGATHER_VEC_GATHER = 0u;
inline constexpr uint8_t kNODE_FILTER_EQ = 0u;
inline constexpr uint8_t kNODE_FILTER_NE = 1u;
inline constexpr uint8_t kNODE_FILTER_LT = 2u;
inline constexpr uint8_t kNODE_FILTER_LE = 3u;
inline constexpr uint8_t kNODE_FILTER_GT = 4u;
inline constexpr uint8_t kNODE_FILTER_GE = 5u;
inline constexpr uint8_t kNODE_FILTER_BETWEEN = 6u;
inline constexpr uint8_t kNODE_FILTER_IS_NULL = 7u;
inline constexpr uint8_t kNODE_FILTER_IS_NOT_NULL = 8u;
inline constexpr uint8_t kVEC_CMP_EQ = 0u;
inline constexpr uint8_t kVEC_CMP_NE = 1u;
inline constexpr uint8_t kVEC_CMP_LT = 2u;
inline constexpr uint8_t kVEC_CMP_LE = 3u;
inline constexpr uint8_t kVEC_CMP_GT = 4u;
inline constexpr uint8_t kVEC_CMP_GE = 5u;
inline constexpr uint8_t kVEC_CMP_BETWEEN = 6u;
inline constexpr uint8_t kVEC_CMP_IN_SET = 7u;
inline constexpr uint8_t kVEC_3VL_OP_AND = 0u;
inline constexpr uint8_t kVEC_3VL_OP_OR = 1u;
inline constexpr uint8_t kVEC_3VL_OP_NOT = 2u;
inline constexpr uint8_t kVEC_3VL_OP_XOR = 3u;
inline constexpr uint8_t kVEC_3VL_OP_IS_NULL = 4u;
inline constexpr uint8_t kVEC_3VL_OP_IS_NOT_NULL = 5u;
inline constexpr uint8_t kVEC_3VL_OP_IS_TRUE = 6u;
inline constexpr uint8_t kVEC_3VL_OP_IS_FALSE = 7u;
inline constexpr uint8_t kVEC_3VL_OP_IS_NAN = 8u;
inline constexpr uint8_t kVEC_3VL_OP_IS_INF = 9u;
inline constexpr uint8_t kVEC_3VL_OP_IS_FINITE = 10u;
inline constexpr uint8_t kVEC_3VL_OP_ASSERT_FINITE = 11u;
inline constexpr uint8_t kVEC_STR_PRED_EQ = 0u;
inline constexpr uint8_t kVEC_STR_PRED_NE = 1u;
inline constexpr uint8_t kVEC_STR_PRED_LT = 2u;
inline constexpr uint8_t kVEC_STR_PRED_LE = 3u;
inline constexpr uint8_t kVEC_STR_PRED_GT = 4u;
inline constexpr uint8_t kVEC_STR_PRED_GE = 5u;
inline constexpr uint8_t kVEC_STR_PRED_STARTS_WITH = 6u;
inline constexpr uint8_t kVEC_STR_PRED_ENDS_WITH = 7u;
inline constexpr uint8_t kVEC_STR_PRED_CONTAINS = 8u;
inline constexpr uint8_t kVEC_STR_PRED_LEN_EQ = 9u;
inline constexpr uint8_t kVEC_MATH_ARITH_ADD = 0u;
inline constexpr uint8_t kVEC_MATH_ARITH_SUB = 1u;
inline constexpr uint8_t kVEC_MATH_ARITH_MUL = 2u;
inline constexpr uint8_t kVEC_MATH_ARITH_DIV = 3u;
inline constexpr uint8_t kVEC_MATH_ARITH_REM = 4u;
inline constexpr uint8_t kVEC_MATH_ARITH_MIN = 5u;
inline constexpr uint8_t kVEC_MATH_ARITH_MAX = 6u;
inline constexpr uint8_t kVEC_MATH_ARITH_COPYSIGN = 7u;
inline constexpr uint8_t kVEC_MATH_ARITH_NEG = 8u;
inline constexpr uint8_t kVEC_MATH_ARITH_ABS = 9u;
inline constexpr uint8_t kVEC_MATH_ARITH_SQRT = 10u;
inline constexpr uint8_t kVEC_MATH_ARITH_RSQRT = 11u;
inline constexpr uint8_t kVEC_MATH_ARITH_FLOOR = 12u;
inline constexpr uint8_t kVEC_MATH_ARITH_CEIL = 13u;
inline constexpr uint8_t kVEC_MATH_ARITH_TRUNC = 14u;
inline constexpr uint8_t kVEC_MATH_ARITH_ROUND = 15u;
inline constexpr uint8_t kVEC_MATH_EXP_LOG_EXP = 0u;
inline constexpr uint8_t kVEC_MATH_EXP_LOG_EXP2 = 1u;
inline constexpr uint8_t kVEC_MATH_EXP_LOG_EXP10 = 2u;
inline constexpr uint8_t kVEC_MATH_EXP_LOG_EXPM1 = 3u;
inline constexpr uint8_t kVEC_MATH_EXP_LOG_LOG = 4u;
inline constexpr uint8_t kVEC_MATH_EXP_LOG_LOG2 = 5u;
inline constexpr uint8_t kVEC_MATH_EXP_LOG_LOG10 = 6u;
inline constexpr uint8_t kVEC_MATH_EXP_LOG_LOG1P = 7u;
inline constexpr uint8_t kVEC_MATH_EXP_LOG_POW = 8u;
inline constexpr uint8_t kVEC_MATH_EXP_LOG_CBRT = 9u;
inline constexpr uint8_t kVEC_MATH_EXP_LOG_HYPOT = 10u;
inline constexpr uint8_t kVEC_MATH_EXP_LOG_ERF = 11u;
inline constexpr uint8_t kVEC_MATH_EXP_LOG_ERFC = 12u;
inline constexpr uint8_t kVEC_MATH_EXP_LOG_LGAMMA = 13u;
inline constexpr uint8_t kVEC_MATH_TRIG_HYP_SIN = 0u;
inline constexpr uint8_t kVEC_MATH_TRIG_HYP_COS = 1u;
inline constexpr uint8_t kVEC_MATH_TRIG_HYP_TAN = 2u;
inline constexpr uint8_t kVEC_MATH_TRIG_HYP_ASIN = 3u;
inline constexpr uint8_t kVEC_MATH_TRIG_HYP_ACOS = 4u;
inline constexpr uint8_t kVEC_MATH_TRIG_HYP_ATAN = 5u;
inline constexpr uint8_t kVEC_MATH_TRIG_HYP_ATAN2 = 6u;
inline constexpr uint8_t kVEC_MATH_TRIG_HYP_SINC = 7u;
inline constexpr uint8_t kVEC_MATH_TRIG_HYP_SINH = 8u;
inline constexpr uint8_t kVEC_MATH_TRIG_HYP_COSH = 9u;
inline constexpr uint8_t kVEC_MATH_TRIG_HYP_TANH = 10u;
inline constexpr uint8_t kVEC_MATH_TRIG_HYP_ASINH = 11u;
inline constexpr uint8_t kVEC_MATH_TRIG_HYP_ACOSH = 12u;
inline constexpr uint8_t kVEC_MATH_TRIG_HYP_ATANH = 13u;
inline constexpr uint8_t kVEC_MATH_ACT_RELU = 0u;
inline constexpr uint8_t kVEC_MATH_ACT_LEAKY_RELU = 1u;
inline constexpr uint8_t kVEC_MATH_ACT_SIGMOID = 2u;
inline constexpr uint8_t kVEC_MATH_ACT_GELU = 3u;
inline constexpr uint8_t kVEC_MATH_ACT_SILU = 4u;
inline constexpr uint8_t kVEC_MATH_ACT_SOFTPLUS = 5u;
inline constexpr uint8_t kVEC_INT_BITWISE_AND = 0u;
inline constexpr uint8_t kVEC_INT_BITWISE_OR = 1u;
inline constexpr uint8_t kVEC_INT_BITWISE_XOR = 2u;
inline constexpr uint8_t kVEC_INT_BITWISE_NOT = 3u;
inline constexpr uint8_t kVEC_INT_BITWISE_AND_NOT = 4u;
inline constexpr uint8_t kVEC_INT_BITWISE_SHL = 5u;
inline constexpr uint8_t kVEC_INT_BITWISE_SHR_U = 6u;
inline constexpr uint8_t kVEC_INT_BITWISE_SHR_S = 7u;
inline constexpr uint8_t kVEC_INT_BITWISE_ROTL = 8u;
inline constexpr uint8_t kVEC_INT_BITWISE_ROTR = 9u;
inline constexpr uint8_t kVEC_INT_BITWISE_POPCOUNT = 10u;
inline constexpr uint8_t kVEC_INT_BITWISE_CLZ = 11u;
inline constexpr uint8_t kVEC_INT_BITWISE_CTZ = 12u;
inline constexpr uint8_t kVEC_INT_BITWISE_BSWAP = 13u;
inline constexpr uint8_t kVEC_TERNARY_FMA_V = 0u;
inline constexpr uint8_t kVEC_TERNARY_FMA_R = 1u;
inline constexpr uint8_t kVEC_TERNARY_LERP_V = 2u;
inline constexpr uint8_t kVEC_TERNARY_LERP_R = 3u;
inline constexpr uint8_t kVEC_TERNARY_CLAMP_V = 4u;
inline constexpr uint8_t kVEC_TERNARY_CLAMP_R = 5u;
inline constexpr uint8_t kVEC_TERNARY_SAFE_DIV_V = 6u;
inline constexpr uint8_t kVEC_TERNARY_SAFE_DIV_R = 7u;
inline constexpr uint8_t kVEC_TERNARY_TIME_VALID_AT_R = 8u;
inline constexpr uint8_t kVEC_TERNARY_MUL_DIV_I64_R = 9u;
inline constexpr uint8_t kVEC_BLEND_KLEENE_VV = 0u;
inline constexpr uint8_t kVEC_BLEND_KLEENE_VR = 1u;
inline constexpr uint8_t kVEC_BLEND_CASE_VV = 2u;
inline constexpr uint8_t kVEC_BLEND_CASE_VR = 3u;
inline constexpr uint8_t kCOALESCE_COALESCE = 0u;
inline constexpr uint8_t kVEC_INIT_I64 = 0u;
inline constexpr uint8_t kVEC_INIT_U64 = 1u;
inline constexpr uint8_t kVEC_INIT_I32 = 2u;
inline constexpr uint8_t kVEC_INIT_U32 = 3u;
inline constexpr uint8_t kVEC_INIT_F64 = 4u;
inline constexpr uint8_t kVEC_INIT_F32 = 5u;
inline constexpr uint8_t kVEC_INIT_BOOL_3VL = 6u;
inline constexpr uint8_t kVEC_INIT_DOM_I64 = 8u;
inline constexpr uint8_t kVEC_INIT_DOM_U64 = 9u;
inline constexpr uint8_t kVEC_INIT_DOM_I32 = 10u;
inline constexpr uint8_t kVEC_INIT_DOM_U32 = 11u;
inline constexpr uint8_t kVEC_INIT_DOM_F64 = 12u;
inline constexpr uint8_t kVEC_INIT_DOM_F32 = 13u;
inline constexpr uint8_t kVEC_INIT_DOM_BOOL_3VL = 14u;
inline constexpr uint8_t kVEC_POINT_VEC_GET = 0u;
inline constexpr uint8_t kVEC_POINT_VEC_SET = 1u;
inline constexpr uint8_t kVEC_POINT_VEC_CAST = 2u;
inline constexpr uint8_t kVEC_POINT_VEC_LEN = 3u;
inline constexpr uint8_t kVEC_REDUCE_SUM = 0u;
inline constexpr uint8_t kVEC_REDUCE_MIN = 1u;
inline constexpr uint8_t kVEC_REDUCE_MAX = 2u;
inline constexpr uint8_t kVEC_REDUCE_COUNT_VALID = 3u;
inline constexpr uint8_t kVEC_REDUCE_MEAN = 4u;
inline constexpr uint8_t kVEC_REDUCE_L1_NORM = 5u;
inline constexpr uint8_t kVEC_REDUCE_L2_NORM_SQ = 6u;
inline constexpr uint8_t kVEC_REDUCE_ARGMIN = 7u;
inline constexpr uint8_t kVEC_REDUCE_ARGMAX = 8u;
inline constexpr uint8_t kVEC_REDUCE_ANY_TRUE = 9u;
inline constexpr uint8_t kVEC_REDUCE_ALL_TRUE = 10u;
inline constexpr uint8_t kTOP_K_DESC_IDS = 0u;
inline constexpr uint8_t kTOP_K_ASC_IDS = 1u;
inline constexpr uint8_t kTOP_K_DESC_SCORES = 2u;
inline constexpr uint8_t kTOP_K_ASC_SCORES = 3u;
inline constexpr uint8_t kINDEX_SCAN_EQ = 0u;
inline constexpr uint8_t kINDEX_SCAN_BETWEEN_CC = 1u;
inline constexpr uint8_t kINDEX_SCAN_BETWEEN_CO = 2u;
inline constexpr uint8_t kINDEX_SCAN_LT = 3u;
inline constexpr uint8_t kINDEX_SCAN_LE = 4u;
inline constexpr uint8_t kINDEX_SCAN_GT = 5u;
inline constexpr uint8_t kINDEX_SCAN_GE = 6u;
inline constexpr uint8_t kMAP_KEYS_TO_DENSE_PARAM_TO_F = 0u;
inline constexpr uint8_t kMAP_KEYS_TO_DENSE_VEC_TO_V = 1u;
inline constexpr uint8_t kMAP_KEYS_TO_DENSE_SCALAR_TO_R = 2u;
inline constexpr uint8_t kMAP_KEYS_TO_DENSE_F_TO_F = 3u;
inline constexpr uint8_t kMAP_KEYS_TO_DENSE_V_TO_V = 4u;
inline constexpr uint8_t kMAP_DENSE_TO_KEYS_VEC = 0u;
inline constexpr uint8_t kMAP_DENSE_TO_KEYS_FRONTIER_SORTED = 1u;
inline constexpr uint8_t kMAP_DENSE_TO_KEYS_SCALAR = 2u;
inline constexpr uint8_t kCOLLECT_FRONTIER_BITSET = 0u;
inline constexpr uint8_t kCOLLECT_FRONTIER_IDS = 1u;
inline constexpr uint8_t kCOLLECT_VEC = 2u;
inline constexpr uint8_t kCOLLECT_SCALAR = 3u;
inline constexpr uint8_t kCOLLECT_TABLE_COL = 4u;
inline constexpr uint8_t kLANE_INIT_SEEDS_AUTO_WIDTH = 0u;
inline constexpr uint8_t kLANE_INIT_SEEDS_FORCE_W64 = 1u;
inline constexpr uint8_t kLANE_INIT_GROUPS_GROUPS = 0u;
inline constexpr uint8_t kLANE_WALK_FWD = 0u;
inline constexpr uint8_t kLANE_WALK_REV = 1u;
inline constexpr uint8_t kLANE_WALK_BOTH = 2u;
inline constexpr uint8_t kLANE_WALK_FWD_FIXPOINT_PLUS = 3u;
inline constexpr uint8_t kLANE_WALK_FWD_FIXPOINT_STAR = 4u;
inline constexpr uint8_t kLANE_WALK_REV_FIXPOINT_PLUS = 5u;
inline constexpr uint8_t kLANE_WALK_REV_FIXPOINT_STAR = 6u;
inline constexpr uint8_t kLANE_WALK_FWD_EDGE_MASKED = 7u;
inline constexpr uint8_t kLANE_WALK_BOTH_FIXPOINT_STAR = 8u;
inline constexpr uint8_t kLANE_WALK_BOTH_FIXPOINT_STAR_MASKED = 9u;
inline constexpr uint8_t kLANE_BITWISE_PRUNE_MASK = 0u;
inline constexpr uint8_t kLANE_BITWISE_KEEP_MASK = 1u;
inline constexpr uint8_t kLANE_BITWISE_AND_NOT_LANES = 2u;
inline constexpr uint8_t kLANE_BITWISE_EXTRACT_LANE = 3u;
inline constexpr uint8_t kLANE_FILTER_QUORUM_AT_LEAST = 0u;
inline constexpr uint8_t kLANE_FILTER_QUORUM_EXACT = 1u;
inline constexpr uint8_t kLANE_FILTER_QUORUM_AT_MOST_NONZERO = 2u;
inline constexpr uint8_t kLANE_REDUCE_AND_REDUCE_AND = 0u;
inline constexpr uint8_t kLANE_REDUCE_ATTR_SUM = 0u;
inline constexpr uint8_t kLANE_REDUCE_ATTR_MIN = 1u;
inline constexpr uint8_t kLANE_REDUCE_ATTR_MAX = 2u;
inline constexpr uint8_t kLANE_REDUCE_ATTR_COUNT = 3u;
inline constexpr uint8_t kVECTOR_ANN_SEARCH_IDS = 0u;
inline constexpr uint8_t kVECTOR_ANN_SEARCH_RADIUS = 1u;
inline constexpr uint8_t kVECTOR_RERANK_DOT = 0u;
inline constexpr uint8_t kVECTOR_RERANK_COSINE = 1u;
inline constexpr uint8_t kVECTOR_RERANK_NEG_L2_SQ = 2u;
inline constexpr uint8_t kVECTOR_RERANK_L2_SQ = 3u;
inline constexpr uint8_t kVECTOR_RERANK_PAIR_DOT = 4u;
inline constexpr uint8_t kVECTOR_RERANK_PAIR_COSINE = 5u;
inline constexpr uint8_t kVECTOR_RERANK_PAIR_NEG_L2_SQ = 6u;
inline constexpr uint8_t kVECTOR_RERANK_PAIR_L2_SQ = 7u;
inline constexpr uint8_t kLANE_STATE_INIT_ZERO_F32 = 0u;
inline constexpr uint8_t kLANE_STATE_INIT_INF_F32 = 1u;
inline constexpr uint8_t kLANE_STATE_INIT_ZERO_F64 = 2u;
inline constexpr uint8_t kLANE_STATE_INIT_INF_F64 = 3u;
inline constexpr uint8_t kLANE_STATE_INIT_FROM_FRONTIER_SEEDS = 4u;
inline constexpr uint8_t kLANE_STATE_INIT_WITH_WITNESS = 5u;
inline constexpr uint8_t kLANE_STATE_WALK_FWD_MIN_PLUS = 0u;
inline constexpr uint8_t kLANE_STATE_WALK_FWD_MAX_PLUS = 1u;
inline constexpr uint8_t kLANE_STATE_WALK_FWD_MAX_MUL = 2u;
inline constexpr uint8_t kLANE_STATE_WALK_FWD_SUM_MUL = 3u;
inline constexpr uint8_t kLANE_STATE_WALK_FWD_MIN_MAX = 4u;
inline constexpr uint8_t kLANE_STATE_WALK_REV_MIN_PLUS = 5u;
inline constexpr uint8_t kLANE_STATE_WALK_REV_MAX_PLUS = 6u;
inline constexpr uint8_t kLANE_STATE_WALK_REV_MAX_MUL = 7u;
inline constexpr uint8_t kLANE_STATE_WALK_REV_SUM_MUL = 8u;
inline constexpr uint8_t kLANE_STATE_WALK_REV_MIN_MAX = 9u;
inline constexpr uint8_t kLANE_STATE_OP_PROJECT_AFFINE = 0u;
inline constexpr uint8_t kLANE_STATE_OP_PRUNE_CUTOFF = 1u;
inline constexpr uint8_t kLANE_STATE_OP_PRUNE_HUB_QUORUM = 2u;
inline constexpr uint8_t kLANE_STATE_OP_TO_LANE_FRONTIER = 3u;
inline constexpr uint8_t kLANE_STATE_OP_TO_QUORUM_FRONTIER = 4u;
inline constexpr uint8_t kLANE_STATE_REDUCE_MIN = 0u;
inline constexpr uint8_t kLANE_STATE_REDUCE_MAX = 1u;
inline constexpr uint8_t kLANE_STATE_REDUCE_SUM = 2u;
inline constexpr uint8_t kLANE_STATE_REDUCE_MEAN = 3u;
inline constexpr uint8_t kLANE_STATE_REDUCE_ARGMIN = 4u;
inline constexpr uint8_t kLANE_STATE_REDUCE_ARGMAX = 5u;
inline constexpr uint8_t kLANE_STATE_REDUCE_TRACE_PATH = 6u;
inline constexpr uint8_t kVECTOR_POOL_LOAD_NODE = 0u;
inline constexpr uint8_t kVECTOR_POOL_MEAN_POOL = 1u;
inline constexpr uint8_t kVECTOR_POOL_SUM_POOL = 2u;
inline constexpr uint8_t kVECTOR_POOL_L2_NORM = 3u;
inline constexpr uint8_t kEXTENSION_PAYLOAD_PAYLOAD = 0u;
}  // namespace subop

struct OpcodeMeta {
  bool defined;
  uint8_t code;
  uint8_t pillar;
  uint8_t allowed_flags;
  uint16_t valid_subop_mask;
  MorselRole morsel_role;
  std::string_view name;
  std::string_view mnemonic;
};

inline constexpr std::array<OpcodeMeta, 256> kOpcodeMeta = []() constexpr {
  std::array<OpcodeMeta, 256> tbl{};
#define IMP_V11_META_ENTRY(NAME, CODE, MNEM, PILLAR, FLAGS, MASK, ROLE) \
  tbl[CODE] = OpcodeMeta{true, CODE, PILLAR, FLAGS, MASK, MorselRole::ROLE, #NAME, #MNEM};
  IMPULSE_V11_OPCODE_LIST(IMP_V11_META_ENTRY)
#undef IMP_V11_META_ENTRY
  return tbl;
}();

[[nodiscard]] constexpr uint64_t encode_word(
    Opcode op, uint8_t flags, uint8_t subop,
    uint8_t dst, uint8_t s1, uint8_t s2, uint8_t s3, uint32_t imm32) noexcept {
  const uint64_t b0 = static_cast<uint64_t>(op) & 0xFFULL;
  const uint64_t b1 = ((static_cast<uint64_t>(flags) & 0xFULL) << 4) | (static_cast<uint64_t>(subop) & 0xFULL);
  const uint64_t b2 = ((static_cast<uint64_t>(dst) & 0xFULL) << 4) | (static_cast<uint64_t>(s1) & 0xFULL);
  const uint64_t b3 = ((static_cast<uint64_t>(s2) & 0xFULL) << 4) | (static_cast<uint64_t>(s3) & 0xFULL);
  return b0 | (b1 << 8) | (b2 << 16) | (b3 << 24) | (static_cast<uint64_t>(imm32) << 32);
}

[[nodiscard]] constexpr uint64_t encode_extension_word(uint32_t imm32_hi) noexcept {
  return 0xFFULL | (static_cast<uint64_t>(imm32_hi) << 32);
}

}  // namespace impulse::vm
