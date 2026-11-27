// DO NOT EDIT — Auto-generated from v0.11-refactor/isa_v0_11.yaml
// by tools/gen_isa_tables.py (impOps v0.11.0, impb v11).
// Generated for impulse-cli (assemble/disassemble/inspect) and impulse-rust ffi.rs.
#![allow(dead_code)]

pub const IMPB_MAGIC: u32 = 0x494D5042;
pub const IMPB_VERSION: u16 = 11;
pub const ACTIVE_OPCODE_COUNT: usize = 80;

pub const FLAG_MASKED_S3: u8 = 0x1;
pub const FLAG_SCALAR_RHS: u8 = 0x2;
pub const FLAG_ACCUM: u8 = 0x4;
pub const FLAG_EXTENDED: u8 = 0x8;

pub const VM_FLAG_ZF: u16 = 0x0001;
pub const VM_FLAG_ST: u16 = 0x0002;
pub const VM_FLAG_NF: u16 = 0x0004;

#[repr(u32)]
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum Status {
    ImpulseVmOk = 0,
    ImpulseVmErrInvalidOpcode = 1,
    ImpulseVmErrOutOfBounds = 2,
    ImpulseVmErrNullSnapshot = 3,
    ImpulseVmErrStackOverflow = 4,
    ImpulseVmErrStackUnderflow = 5,
    ImpulseVmErrInvalidOperand = 6,
    ImpulseVmErrUserThrow = 7,
    ImpulseVmErrAssertionFailed = 8,
    ImpulseVmErrTrap = 9,
    ImpulseVmErrDomainMismatch = 10,
    ImpulseVmErrBufferOverflow = 11,
    ImpulseVmErrFloatingPoint = 12,
    ImpulseVmErrGasExhausted = 13,
    ImpulseVmErrTypeMismatch = 14,
    ImpulseVmErrValidationFailed = 15,
    ImpulseVmErrArithmeticOverflow = 16,
    ImpulseVmErrDivByZero = 17,
}

#[repr(u8)]
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum ScalarType {
    ScalarNullUnset = 0,
    ScalarI64 = 1,
    ScalarU64 = 2,
    ScalarF64 = 3,
    ScalarBool3vl = 4,
    ScalarStrHandle = 5,
    ScalarF32Raw = 6,
}

#[repr(u8)]
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum MorselRole {
    Forbidden = 0,
    BodyOk = 1,
    BodyCastOnly = 2,
    BodyVOnly = 3,
    BodyPairOnly = 4,
    FwdJumpOnly = 5,
    Header = 6,
    CommitOnly = 7,
    EndOnly = 8,
}

#[repr(u8)]
#[derive(Debug, Clone, Copy, PartialEq, Eq, Hash)]
pub enum Opcode {
    OpHalt = 0x00,
    OpNop = 0x01,
    OpTrap = 0x02,
    OpThrow = 0x03,
    OpAssert = 0x04,
    OpLoadConst = 0x05,
    OpLoadConstPool = 0x06,
    OpLoadParam = 0x07,
    OpMov = 0x08,
    OpSwap = 0x09,
    OpSpill = 0x0A,
    OpReload = 0x0B,
    OpScalarIntAlu = 0x0C,
    OpScalarFpAlu = 0x0D,
    OpScalar3vl = 0x0E,
    OpJmp = 0x0F,
    OpJcc = 0x10,
    OpLoopDecr = 0x11,
    OpCall = 0x12,
    OpRet = 0x13,
    OpSetMaxDop = 0x14,
    OpScalarDecimalAlu = 0x15,
    OpFseed = 0x20,
    OpInitDomainSpan = 0x21,
    OpSetOp = 0x22,
    OpSetComplement = 0x23,
    OpSetCardinality = 0x24,
    OpSetContains = 0x25,
    OpSetConvert = 0x26,
    OpStableCheck = 0x27,
    OpWalk = 0x30,
    OpWalkFiltered = 0x31,
    OpDegree = 0x33,
    OpSpmv = 0x34,
    OpWalkMorsel = 0x35,
    OpFilterMorsel = 0x36,
    OpMorselCommit = 0x37,
    OpMorselEnd = 0x38,
    OpSampleNeighbors = 0x39,
    OpHasTopology = 0x3A,
    OpGraphKernel = 0x3B,
    OpLoadColumn = 0x40,
    OpGatherNodeAttr = 0x41,
    OpGatherEdgeAttr = 0x42,
    OpGatherVec = 0x43,
    OpNodeFilter = 0x44,
    OpVecCmp = 0x45,
    OpVec3vlOp = 0x46,
    OpVecStrPred = 0x47,
    OpVecMathArith = 0x48,
    OpVecMathExpLog = 0x49,
    OpVecMathTrigHyp = 0x4A,
    OpVecMathAct = 0x4B,
    OpVecIntBitwise = 0x4C,
    OpVecTernary = 0x4D,
    OpVecBlend = 0x4E,
    OpCoalesce = 0x4F,
    OpVecInit = 0x50,
    OpVecPoint = 0x51,
    OpVecReduce = 0x52,
    OpTopK = 0x53,
    OpIndexScan = 0x54,
    OpMapKeysToDense = 0x55,
    OpMapDenseToKeys = 0x56,
    OpCollect = 0x57,
    OpVecDecimalAlu = 0x58,
    OpLaneInitSeeds = 0x60,
    OpLaneInitGroups = 0x61,
    OpLaneWalk = 0x63,
    OpLaneBitwise = 0x64,
    OpLaneFilterQuorum = 0x65,
    OpLaneReduceAnd = 0x66,
    OpLaneReduceAttr = 0x67,
    OpVectorAnnSearch = 0x69,
    OpVectorRerank = 0x6A,
    OpLaneStateInit = 0x6B,
    OpLaneStateWalk = 0x6C,
    OpLaneStateOp = 0x6D,
    OpLaneStateReduce = 0x6E,
    OpVectorPool = 0x6F,
    OpExtensionPayload = 0xFF,
}

pub mod subop {
    pub const HALT_OK: u8 = 0;
    pub const NOP_NOP: u8 = 0;
    pub const TRAP_TRAP: u8 = 0;
    pub const THROW_IMM: u8 = 0;
    pub const THROW_REG: u8 = 1;
    pub const ASSERT_TRUE_R: u8 = 0;
    pub const ASSERT_NONEMPTY_F: u8 = 1;
    pub const ASSERT_EMPTY_F: u8 = 2;
    pub const ASSERT_ZF_SET: u8 = 3;
    pub const ASSERT_ZF_CLEAR: u8 = 4;
    pub const ASSERT_ST_SET: u8 = 5;
    pub const ASSERT_NOT_NULL_R: u8 = 6;
    pub const ASSERT_IS_NULL_R: u8 = 7;
    pub const LOAD_CONST_I32: u8 = 0;
    pub const LOAD_CONST_U32: u8 = 1;
    pub const LOAD_CONST_F32: u8 = 2;
    pub const LOAD_CONST_F32_RAW: u8 = 3;
    pub const LOAD_CONST_I64: u8 = 4;
    pub const LOAD_CONST_U64: u8 = 5;
    pub const LOAD_CONST_F64: u8 = 6;
    pub const LOAD_CONST_NULL: u8 = 7;
    pub const LOAD_CONST_POOL_VEC: u8 = 0;
    pub const LOAD_CONST_POOL_FRONTIER: u8 = 1;
    pub const LOAD_CONST_POOL_STR: u8 = 2;
    pub const LOAD_PARAM_SCALAR: u8 = 0;
    pub const LOAD_PARAM_FRONTIER: u8 = 1;
    pub const LOAD_PARAM_VEC: u8 = 2;
    pub const MOV_R: u8 = 0;
    pub const MOV_F: u8 = 1;
    pub const MOV_V: u8 = 2;
    pub const MOV_CLEAR_R: u8 = 3;
    pub const MOV_CLEAR_F: u8 = 4;
    pub const MOV_CLEAR_V: u8 = 5;
    pub const SWAP_R: u8 = 0;
    pub const SWAP_F: u8 = 1;
    pub const SWAP_V: u8 = 2;
    pub const SPILL_R: u8 = 0;
    pub const SPILL_F: u8 = 1;
    pub const SPILL_V: u8 = 2;
    pub const RELOAD_R: u8 = 0;
    pub const RELOAD_F: u8 = 1;
    pub const RELOAD_V: u8 = 2;
    pub const SCALAR_INT_ALU_ADD: u8 = 0;
    pub const SCALAR_INT_ALU_SUB: u8 = 1;
    pub const SCALAR_INT_ALU_MUL: u8 = 2;
    pub const SCALAR_INT_ALU_DIV_S: u8 = 3;
    pub const SCALAR_INT_ALU_DIV_U: u8 = 4;
    pub const SCALAR_INT_ALU_REM_S: u8 = 5;
    pub const SCALAR_INT_ALU_REM_U: u8 = 6;
    pub const SCALAR_INT_ALU_AND: u8 = 7;
    pub const SCALAR_INT_ALU_OR: u8 = 8;
    pub const SCALAR_INT_ALU_XOR: u8 = 9;
    pub const SCALAR_INT_ALU_NOT: u8 = 10;
    pub const SCALAR_INT_ALU_SHL: u8 = 11;
    pub const SCALAR_INT_ALU_SHR_U: u8 = 12;
    pub const SCALAR_INT_ALU_SHR_S: u8 = 13;
    pub const SCALAR_INT_ALU_MIN_S: u8 = 14;
    pub const SCALAR_INT_ALU_MAX_S: u8 = 15;
    pub const SCALAR_FP_ALU_ADD: u8 = 0;
    pub const SCALAR_FP_ALU_SUB: u8 = 1;
    pub const SCALAR_FP_ALU_MUL: u8 = 2;
    pub const SCALAR_FP_ALU_DIV: u8 = 3;
    pub const SCALAR_FP_ALU_NEG: u8 = 4;
    pub const SCALAR_FP_ALU_ABS: u8 = 5;
    pub const SCALAR_FP_ALU_SQRT: u8 = 6;
    pub const SCALAR_FP_ALU_MIN: u8 = 7;
    pub const SCALAR_FP_ALU_MAX: u8 = 8;
    pub const SCALAR_FP_ALU_FMA: u8 = 9;
    pub const SCALAR_FP_ALU_FLOOR: u8 = 10;
    pub const SCALAR_FP_ALU_CEIL: u8 = 11;
    pub const SCALAR_FP_ALU_ROUND: u8 = 12;
    pub const SCALAR_FP_ALU_I64_TO_F64: u8 = 13;
    pub const SCALAR_FP_ALU_F64_TO_I64: u8 = 14;
    pub const SCALAR_FP_ALU_SAFE_DIV: u8 = 15;
    pub const SCALAR_3VL_CMP_EQ_I64: u8 = 0;
    pub const SCALAR_3VL_CMP_NE_I64: u8 = 1;
    pub const SCALAR_3VL_CMP_LT_I64: u8 = 2;
    pub const SCALAR_3VL_CMP_LE_I64: u8 = 3;
    pub const SCALAR_3VL_CMP_LT_U64: u8 = 4;
    pub const SCALAR_3VL_CMP_LE_U64: u8 = 5;
    pub const SCALAR_3VL_CMP_EQ_F64: u8 = 6;
    pub const SCALAR_3VL_CMP_NE_F64: u8 = 7;
    pub const SCALAR_3VL_CMP_LT_F64: u8 = 8;
    pub const SCALAR_3VL_CMP_LE_F64: u8 = 9;
    pub const SCALAR_3VL_AND: u8 = 10;
    pub const SCALAR_3VL_OR: u8 = 11;
    pub const SCALAR_3VL_NOT: u8 = 12;
    pub const SCALAR_3VL_IS_NULL: u8 = 13;
    pub const SCALAR_3VL_IS_NOT_NULL: u8 = 14;
    pub const SCALAR_3VL_COALESCE: u8 = 15;
    pub const JMP_JMP: u8 = 0;
    pub const JCC_JZ: u8 = 0;
    pub const JCC_JNZ: u8 = 1;
    pub const JCC_JST: u8 = 2;
    pub const JCC_JNST: u8 = 3;
    pub const JCC_JNULL: u8 = 4;
    pub const JCC_JNNULL: u8 = 5;
    pub const JCC_JTRUE_R: u8 = 6;
    pub const JCC_JFALSE_OR_NULL_R: u8 = 7;
    pub const JCC_JEMPTY_F: u8 = 8;
    pub const JCC_JNONEMPTY_F: u8 = 9;
    pub const LOOP_DECR_DECR: u8 = 0;
    pub const CALL_STRIDE: u8 = 0;
    pub const CALL_FULL16: u8 = 1;
    pub const RET_RET: u8 = 0;
    pub const SET_MAX_DOP_IMM: u8 = 0;
    pub const SET_MAX_DOP_REG: u8 = 1;
    pub const SCALAR_DECIMAL_ALU_ADD_CHK: u8 = 0;
    pub const SCALAR_DECIMAL_ALU_SUB_CHK: u8 = 1;
    pub const SCALAR_DECIMAL_ALU_MUL_CHK: u8 = 2;
    pub const SCALAR_DECIMAL_ALU_MUL_DIV_ROUND: u8 = 3;
    pub const SCALAR_DECIMAL_ALU_DIV_ROUND: u8 = 4;
    pub const SCALAR_DECIMAL_ALU_RESCALE: u8 = 5;
    pub const SCALAR_DECIMAL_ALU_BOUND_CHK: u8 = 6;
    pub const SCALAR_DECIMAL_ALU_ABS_CHK: u8 = 7;
    pub const SCALAR_DECIMAL_ALU_NEG_CHK: u8 = 8;
    pub const SCALAR_DECIMAL_ALU_F64_TO_DEC: u8 = 9;
    pub const SCALAR_DECIMAL_ALU_DEC_TO_F64: u8 = 10;
    pub const SCALAR_DECIMAL_ALU_ROUND_SCALE: u8 = 11;
    pub const SCALAR_DECIMAL_ALU_TRUNC_SCALE: u8 = 12;
    pub const SCALAR_DECIMAL_ALU_FLOOR_SCALE: u8 = 13;
    pub const SCALAR_DECIMAL_ALU_CEIL_SCALE: u8 = 14;
    pub const SCALAR_DECIMAL_ALU_DIV_SAFE: u8 = 15;
    pub const FSEED_EMPTY: u8 = 0;
    pub const FSEED_SINGLE_IMM: u8 = 1;
    pub const FSEED_SINGLE_REG: u8 = 2;
    pub const FSEED_INSERT_REG: u8 = 3;
    pub const FSEED_REMOVE_REG: u8 = 4;
    pub const INIT_DOMAIN_SPAN_ALL_SPAN: u8 = 0;
    pub const SET_OP_UNION: u8 = 0;
    pub const SET_OP_INTERSECT: u8 = 1;
    pub const SET_OP_DIFF: u8 = 2;
    pub const SET_OP_SYM_DIFF: u8 = 3;
    pub const SET_COMPLEMENT_COMPLEMENT: u8 = 0;
    pub const SET_CARDINALITY_CARD_1BIT: u8 = 0;
    pub const SET_CARDINALITY_LANE_POP_SUM: u8 = 1;
    pub const SET_CARDINALITY_LANE_ANY_NODES: u8 = 2;
    pub const SET_CARDINALITY_LANE_ALL_NODES: u8 = 3;
    pub const SET_CARDINALITY_SINGLE_LANE: u8 = 4;
    pub const SET_CONTAINS_CONTAINS_1BIT: u8 = 0;
    pub const SET_CONTAINS_GET_LANE_MASK: u8 = 1;
    pub const SET_CONVERT_F2V_IDS: u8 = 0;
    pub const SET_CONVERT_V2F_IDS: u8 = 1;
    pub const SET_CONVERT_B2F: u8 = 2;
    pub const SET_CONVERT_F2B: u8 = 3;
    pub const STABLE_CHECK_FRONTIER_EQ: u8 = 0;
    pub const STABLE_CHECK_VEC_L1: u8 = 1;
    pub const WALK_FWD: u8 = 0;
    pub const WALK_REV: u8 = 1;
    pub const WALK_BOTH: u8 = 2;
    pub const WALK_FWD_FIXPOINT_PLUS: u8 = 3;
    pub const WALK_FWD_FIXPOINT_STAR: u8 = 4;
    pub const WALK_REV_FIXPOINT_PLUS: u8 = 5;
    pub const WALK_REV_FIXPOINT_STAR: u8 = 6;
    pub const WALK_FWD_BOUNDED: u8 = 7;
    pub const WALK_COO_FWD: u8 = 8;
    pub const WALK_COO_REV: u8 = 9;
    pub const WALK_FILTERED_EQ: u8 = 0;
    pub const WALK_FILTERED_NE: u8 = 1;
    pub const WALK_FILTERED_LT: u8 = 2;
    pub const WALK_FILTERED_LE: u8 = 3;
    pub const WALK_FILTERED_GT: u8 = 4;
    pub const WALK_FILTERED_GE: u8 = 5;
    pub const WALK_FILTERED_IS_NOT_NULL: u8 = 6;
    pub const WALK_FILTERED_IS_NULL: u8 = 7;
    pub const WALK_FILTERED_REV_EQ: u8 = 8;
    pub const WALK_FILTERED_REV_NE: u8 = 9;
    pub const WALK_FILTERED_REV_LT: u8 = 10;
    pub const WALK_FILTERED_REV_LE: u8 = 11;
    pub const WALK_FILTERED_REV_GT: u8 = 12;
    pub const WALK_FILTERED_REV_GE: u8 = 13;
    pub const WALK_FILTERED_REV_IS_NOT_NULL: u8 = 14;
    pub const WALK_FILTERED_REV_IS_NULL: u8 = 15;
    pub const DEGREE_SUM_FWD_R: u8 = 0;
    pub const DEGREE_SUM_REV_R: u8 = 1;
    pub const DEGREE_VEC_FWD_V: u8 = 2;
    pub const DEGREE_VEC_REV_V: u8 = 3;
    pub const DEGREE_NODE_FWD_R: u8 = 4;
    pub const DEGREE_NODE_REV_R: u8 = 5;
    pub const SPMV_FWD_PLUS_TIMES: u8 = 0;
    pub const SPMV_REV_PLUS_TIMES: u8 = 1;
    pub const SPMV_FWD_MIN_PLUS: u8 = 2;
    pub const SPMV_REV_MIN_PLUS: u8 = 3;
    pub const SPMV_FWD_MAX_PLUS: u8 = 4;
    pub const SPMV_FWD_MAX_TIMES: u8 = 5;
    pub const SPMV_FWD_MIN_MAX: u8 = 6;
    pub const SPMV_FWD_FIRST_SRC: u8 = 7;
    pub const SPMV_FWD_ARGMIN_PLUS: u8 = 8;
    pub const WALK_MORSEL_FWD: u8 = 0;
    pub const WALK_MORSEL_REV: u8 = 1;
    pub const WALK_MORSEL_COO_FWD: u8 = 2;
    pub const WALK_MORSEL_COO_REV: u8 = 3;
    pub const FILTER_MORSEL_NODE: u8 = 0;
    pub const MORSEL_COMMIT_YIELD_FRONTIER: u8 = 0;
    pub const MORSEL_COMMIT_YIELD_LANE_OR: u8 = 1;
    pub const MORSEL_COMMIT_SCATTER_ADD: u8 = 2;
    pub const MORSEL_COMMIT_SCATTER_MIN: u8 = 3;
    pub const MORSEL_COMMIT_SCATTER_MAX: u8 = 4;
    pub const MORSEL_COMMIT_SCATTER_MIN_SRC: u8 = 5;
    pub const MORSEL_COMMIT_SCATTER_ARGMIN: u8 = 6;
    pub const MORSEL_COMMIT_SCATTER_ARGMAX: u8 = 7;
    pub const MORSEL_COMMIT_COUNT_ADD_R: u8 = 8;
    pub const MORSEL_COMMIT_SCATTER_ADD_CHK: u8 = 9;
    pub const MORSEL_END_END: u8 = 0;
    pub const SAMPLE_NEIGHBORS_FRONTIER_WOR: u8 = 0;
    pub const SAMPLE_NEIGHBORS_VEC_WR: u8 = 1;
    pub const SAMPLE_NEIGHBORS_RW_STEP_FWD: u8 = 2;
    pub const SAMPLE_NEIGHBORS_RW_STEP_STAY: u8 = 3;
    pub const HAS_TOPOLOGY_HAS_CSR: u8 = 0;
    pub const HAS_TOPOLOGY_HAS_CSC: u8 = 1;
    pub const HAS_TOPOLOGY_HAS_COO: u8 = 2;
    pub const HAS_TOPOLOGY_HAS_DENSE: u8 = 3;
    pub const HAS_TOPOLOGY_HAS_KEY_CAT: u8 = 4;
    pub const HAS_TOPOLOGY_HAS_NODE_ATTR: u8 = 5;
    pub const HAS_TOPOLOGY_HAS_EDGE_ATTR: u8 = 6;
    pub const HAS_TOPOLOGY_HAS_INDEX: u8 = 7;
    pub const HAS_TOPOLOGY_DOMAIN_CARD: u8 = 8;
    pub const HAS_TOPOLOGY_REL_EDGES: u8 = 9;
    pub const GRAPH_KERNEL_CC_AFFOREST: u8 = 0;
    pub const GRAPH_KERNEL_KCORE_NUMS: u8 = 1;
    pub const GRAPH_KERNEL_KCORE_FRONTIER: u8 = 2;
    pub const GRAPH_KERNEL_TRI_COUNT_TOTAL: u8 = 3;
    pub const GRAPH_KERNEL_TRI_COUNT_VEC: u8 = 4;
    pub const LOAD_COLUMN_NODE_ATTR: u8 = 0;
    pub const LOAD_COLUMN_EDGE_ATTR: u8 = 1;
    pub const GATHER_NODE_ATTR_VEC: u8 = 0;
    pub const GATHER_NODE_ATTR_FRONTIER_COMPACT: u8 = 1;
    pub const GATHER_NODE_ATTR_SCALAR: u8 = 2;
    pub const GATHER_EDGE_ATTR_VEC: u8 = 0;
    pub const GATHER_EDGE_ATTR_SCALAR: u8 = 1;
    pub const GATHER_VEC_GATHER: u8 = 0;
    pub const NODE_FILTER_EQ: u8 = 0;
    pub const NODE_FILTER_NE: u8 = 1;
    pub const NODE_FILTER_LT: u8 = 2;
    pub const NODE_FILTER_LE: u8 = 3;
    pub const NODE_FILTER_GT: u8 = 4;
    pub const NODE_FILTER_GE: u8 = 5;
    pub const NODE_FILTER_BETWEEN: u8 = 6;
    pub const NODE_FILTER_IS_NULL: u8 = 7;
    pub const NODE_FILTER_IS_NOT_NULL: u8 = 8;
    pub const VEC_CMP_EQ: u8 = 0;
    pub const VEC_CMP_NE: u8 = 1;
    pub const VEC_CMP_LT: u8 = 2;
    pub const VEC_CMP_LE: u8 = 3;
    pub const VEC_CMP_GT: u8 = 4;
    pub const VEC_CMP_GE: u8 = 5;
    pub const VEC_CMP_BETWEEN: u8 = 6;
    pub const VEC_CMP_IN_SET: u8 = 7;
    pub const VEC_3VL_OP_AND: u8 = 0;
    pub const VEC_3VL_OP_OR: u8 = 1;
    pub const VEC_3VL_OP_NOT: u8 = 2;
    pub const VEC_3VL_OP_XOR: u8 = 3;
    pub const VEC_3VL_OP_IS_NULL: u8 = 4;
    pub const VEC_3VL_OP_IS_NOT_NULL: u8 = 5;
    pub const VEC_3VL_OP_IS_TRUE: u8 = 6;
    pub const VEC_3VL_OP_IS_FALSE: u8 = 7;
    pub const VEC_3VL_OP_IS_NAN: u8 = 8;
    pub const VEC_3VL_OP_IS_INF: u8 = 9;
    pub const VEC_3VL_OP_IS_FINITE: u8 = 10;
    pub const VEC_3VL_OP_ASSERT_FINITE: u8 = 11;
    pub const VEC_STR_PRED_EQ: u8 = 0;
    pub const VEC_STR_PRED_NE: u8 = 1;
    pub const VEC_STR_PRED_LT: u8 = 2;
    pub const VEC_STR_PRED_LE: u8 = 3;
    pub const VEC_STR_PRED_GT: u8 = 4;
    pub const VEC_STR_PRED_GE: u8 = 5;
    pub const VEC_STR_PRED_STARTS_WITH: u8 = 6;
    pub const VEC_STR_PRED_ENDS_WITH: u8 = 7;
    pub const VEC_STR_PRED_CONTAINS: u8 = 8;
    pub const VEC_STR_PRED_LEN_EQ: u8 = 9;
    pub const VEC_MATH_ARITH_ADD: u8 = 0;
    pub const VEC_MATH_ARITH_SUB: u8 = 1;
    pub const VEC_MATH_ARITH_MUL: u8 = 2;
    pub const VEC_MATH_ARITH_DIV: u8 = 3;
    pub const VEC_MATH_ARITH_REM: u8 = 4;
    pub const VEC_MATH_ARITH_MIN: u8 = 5;
    pub const VEC_MATH_ARITH_MAX: u8 = 6;
    pub const VEC_MATH_ARITH_COPYSIGN: u8 = 7;
    pub const VEC_MATH_ARITH_NEG: u8 = 8;
    pub const VEC_MATH_ARITH_ABS: u8 = 9;
    pub const VEC_MATH_ARITH_SQRT: u8 = 10;
    pub const VEC_MATH_ARITH_RSQRT: u8 = 11;
    pub const VEC_MATH_ARITH_FLOOR: u8 = 12;
    pub const VEC_MATH_ARITH_CEIL: u8 = 13;
    pub const VEC_MATH_ARITH_TRUNC: u8 = 14;
    pub const VEC_MATH_ARITH_ROUND: u8 = 15;
    pub const VEC_MATH_EXP_LOG_EXP: u8 = 0;
    pub const VEC_MATH_EXP_LOG_EXP2: u8 = 1;
    pub const VEC_MATH_EXP_LOG_EXP10: u8 = 2;
    pub const VEC_MATH_EXP_LOG_EXPM1: u8 = 3;
    pub const VEC_MATH_EXP_LOG_LOG: u8 = 4;
    pub const VEC_MATH_EXP_LOG_LOG2: u8 = 5;
    pub const VEC_MATH_EXP_LOG_LOG10: u8 = 6;
    pub const VEC_MATH_EXP_LOG_LOG1P: u8 = 7;
    pub const VEC_MATH_EXP_LOG_POW: u8 = 8;
    pub const VEC_MATH_EXP_LOG_CBRT: u8 = 9;
    pub const VEC_MATH_EXP_LOG_HYPOT: u8 = 10;
    pub const VEC_MATH_EXP_LOG_ERF: u8 = 11;
    pub const VEC_MATH_EXP_LOG_ERFC: u8 = 12;
    pub const VEC_MATH_EXP_LOG_LGAMMA: u8 = 13;
    pub const VEC_MATH_TRIG_HYP_SIN: u8 = 0;
    pub const VEC_MATH_TRIG_HYP_COS: u8 = 1;
    pub const VEC_MATH_TRIG_HYP_TAN: u8 = 2;
    pub const VEC_MATH_TRIG_HYP_ASIN: u8 = 3;
    pub const VEC_MATH_TRIG_HYP_ACOS: u8 = 4;
    pub const VEC_MATH_TRIG_HYP_ATAN: u8 = 5;
    pub const VEC_MATH_TRIG_HYP_ATAN2: u8 = 6;
    pub const VEC_MATH_TRIG_HYP_SINC: u8 = 7;
    pub const VEC_MATH_TRIG_HYP_SINH: u8 = 8;
    pub const VEC_MATH_TRIG_HYP_COSH: u8 = 9;
    pub const VEC_MATH_TRIG_HYP_TANH: u8 = 10;
    pub const VEC_MATH_TRIG_HYP_ASINH: u8 = 11;
    pub const VEC_MATH_TRIG_HYP_ACOSH: u8 = 12;
    pub const VEC_MATH_TRIG_HYP_ATANH: u8 = 13;
    pub const VEC_MATH_ACT_RELU: u8 = 0;
    pub const VEC_MATH_ACT_LEAKY_RELU: u8 = 1;
    pub const VEC_MATH_ACT_SIGMOID: u8 = 2;
    pub const VEC_MATH_ACT_GELU: u8 = 3;
    pub const VEC_MATH_ACT_SILU: u8 = 4;
    pub const VEC_MATH_ACT_SOFTPLUS: u8 = 5;
    pub const VEC_INT_BITWISE_AND: u8 = 0;
    pub const VEC_INT_BITWISE_OR: u8 = 1;
    pub const VEC_INT_BITWISE_XOR: u8 = 2;
    pub const VEC_INT_BITWISE_NOT: u8 = 3;
    pub const VEC_INT_BITWISE_AND_NOT: u8 = 4;
    pub const VEC_INT_BITWISE_SHL: u8 = 5;
    pub const VEC_INT_BITWISE_SHR_U: u8 = 6;
    pub const VEC_INT_BITWISE_SHR_S: u8 = 7;
    pub const VEC_INT_BITWISE_ROTL: u8 = 8;
    pub const VEC_INT_BITWISE_ROTR: u8 = 9;
    pub const VEC_INT_BITWISE_POPCOUNT: u8 = 10;
    pub const VEC_INT_BITWISE_CLZ: u8 = 11;
    pub const VEC_INT_BITWISE_CTZ: u8 = 12;
    pub const VEC_INT_BITWISE_BSWAP: u8 = 13;
    pub const VEC_TERNARY_FMA_V: u8 = 0;
    pub const VEC_TERNARY_FMA_R: u8 = 1;
    pub const VEC_TERNARY_LERP_V: u8 = 2;
    pub const VEC_TERNARY_LERP_R: u8 = 3;
    pub const VEC_TERNARY_CLAMP_V: u8 = 4;
    pub const VEC_TERNARY_CLAMP_R: u8 = 5;
    pub const VEC_TERNARY_SAFE_DIV_V: u8 = 6;
    pub const VEC_TERNARY_SAFE_DIV_R: u8 = 7;
    pub const VEC_TERNARY_TIME_VALID_AT_R: u8 = 8;
    pub const VEC_TERNARY_MUL_DIV_I64_R: u8 = 9;
    pub const VEC_BLEND_KLEENE_VV: u8 = 0;
    pub const VEC_BLEND_KLEENE_VR: u8 = 1;
    pub const VEC_BLEND_CASE_VV: u8 = 2;
    pub const VEC_BLEND_CASE_VR: u8 = 3;
    pub const COALESCE_COALESCE: u8 = 0;
    pub const VEC_INIT_I64: u8 = 0;
    pub const VEC_INIT_U64: u8 = 1;
    pub const VEC_INIT_I32: u8 = 2;
    pub const VEC_INIT_U32: u8 = 3;
    pub const VEC_INIT_F64: u8 = 4;
    pub const VEC_INIT_F32: u8 = 5;
    pub const VEC_INIT_BOOL_3VL: u8 = 6;
    pub const VEC_INIT_DOM_I64: u8 = 8;
    pub const VEC_INIT_DOM_U64: u8 = 9;
    pub const VEC_INIT_DOM_I32: u8 = 10;
    pub const VEC_INIT_DOM_U32: u8 = 11;
    pub const VEC_INIT_DOM_F64: u8 = 12;
    pub const VEC_INIT_DOM_F32: u8 = 13;
    pub const VEC_INIT_DOM_BOOL_3VL: u8 = 14;
    pub const VEC_POINT_VEC_GET: u8 = 0;
    pub const VEC_POINT_VEC_SET: u8 = 1;
    pub const VEC_POINT_VEC_CAST: u8 = 2;
    pub const VEC_POINT_VEC_LEN: u8 = 3;
    pub const VEC_REDUCE_SUM: u8 = 0;
    pub const VEC_REDUCE_MIN: u8 = 1;
    pub const VEC_REDUCE_MAX: u8 = 2;
    pub const VEC_REDUCE_COUNT_VALID: u8 = 3;
    pub const VEC_REDUCE_MEAN: u8 = 4;
    pub const VEC_REDUCE_L1_NORM: u8 = 5;
    pub const VEC_REDUCE_L2_NORM_SQ: u8 = 6;
    pub const VEC_REDUCE_ARGMIN: u8 = 7;
    pub const VEC_REDUCE_ARGMAX: u8 = 8;
    pub const VEC_REDUCE_ANY_TRUE: u8 = 9;
    pub const VEC_REDUCE_ALL_TRUE: u8 = 10;
    pub const VEC_REDUCE_SUM_I64_CHK: u8 = 11;
    pub const TOP_K_DESC_IDS: u8 = 0;
    pub const TOP_K_ASC_IDS: u8 = 1;
    pub const TOP_K_DESC_SCORES: u8 = 2;
    pub const TOP_K_ASC_SCORES: u8 = 3;
    pub const INDEX_SCAN_EQ: u8 = 0;
    pub const INDEX_SCAN_BETWEEN_CC: u8 = 1;
    pub const INDEX_SCAN_BETWEEN_CO: u8 = 2;
    pub const INDEX_SCAN_LT: u8 = 3;
    pub const INDEX_SCAN_LE: u8 = 4;
    pub const INDEX_SCAN_GT: u8 = 5;
    pub const INDEX_SCAN_GE: u8 = 6;
    pub const MAP_KEYS_TO_DENSE_PARAM_TO_F: u8 = 0;
    pub const MAP_KEYS_TO_DENSE_VEC_TO_V: u8 = 1;
    pub const MAP_KEYS_TO_DENSE_SCALAR_TO_R: u8 = 2;
    pub const MAP_KEYS_TO_DENSE_F_TO_F: u8 = 3;
    pub const MAP_KEYS_TO_DENSE_V_TO_V: u8 = 4;
    pub const MAP_DENSE_TO_KEYS_VEC: u8 = 0;
    pub const MAP_DENSE_TO_KEYS_FRONTIER_SORTED: u8 = 1;
    pub const MAP_DENSE_TO_KEYS_SCALAR: u8 = 2;
    pub const COLLECT_FRONTIER_BITSET: u8 = 0;
    pub const COLLECT_FRONTIER_IDS: u8 = 1;
    pub const COLLECT_VEC: u8 = 2;
    pub const COLLECT_SCALAR: u8 = 3;
    pub const COLLECT_TABLE_COL: u8 = 4;
    pub const VEC_DECIMAL_ALU_ADD_CHK: u8 = 0;
    pub const VEC_DECIMAL_ALU_SUB_CHK: u8 = 1;
    pub const VEC_DECIMAL_ALU_MUL_CHK: u8 = 2;
    pub const VEC_DECIMAL_ALU_MUL_DIV_R: u8 = 3;
    pub const VEC_DECIMAL_ALU_MUL_DIV_V: u8 = 4;
    pub const VEC_DECIMAL_ALU_DIV_ROUND: u8 = 5;
    pub const VEC_DECIMAL_ALU_DIV_SAFE: u8 = 6;
    pub const VEC_DECIMAL_ALU_RESCALE: u8 = 7;
    pub const VEC_DECIMAL_ALU_BOUND_CHK: u8 = 8;
    pub const VEC_DECIMAL_ALU_ABS_CHK: u8 = 9;
    pub const VEC_DECIMAL_ALU_NEG_CHK: u8 = 10;
    pub const VEC_DECIMAL_ALU_F64_TO_DEC: u8 = 11;
    pub const VEC_DECIMAL_ALU_DEC_TO_F64: u8 = 12;
    pub const VEC_DECIMAL_ALU_ROUND_SCALE: u8 = 13;
    pub const VEC_DECIMAL_ALU_TRUNC_SCALE: u8 = 14;
    pub const VEC_DECIMAL_ALU_MUL_DIV_SAFE_R: u8 = 15;
    pub const LANE_INIT_SEEDS_AUTO_WIDTH: u8 = 0;
    pub const LANE_INIT_SEEDS_FORCE_W64: u8 = 1;
    pub const LANE_INIT_GROUPS_GROUPS: u8 = 0;
    pub const LANE_WALK_FWD: u8 = 0;
    pub const LANE_WALK_REV: u8 = 1;
    pub const LANE_WALK_BOTH: u8 = 2;
    pub const LANE_WALK_FWD_FIXPOINT_PLUS: u8 = 3;
    pub const LANE_WALK_FWD_FIXPOINT_STAR: u8 = 4;
    pub const LANE_WALK_REV_FIXPOINT_PLUS: u8 = 5;
    pub const LANE_WALK_REV_FIXPOINT_STAR: u8 = 6;
    pub const LANE_WALK_FWD_EDGE_MASKED: u8 = 7;
    pub const LANE_WALK_BOTH_FIXPOINT_STAR: u8 = 8;
    pub const LANE_WALK_BOTH_FIXPOINT_STAR_MASKED: u8 = 9;
    pub const LANE_BITWISE_PRUNE_MASK: u8 = 0;
    pub const LANE_BITWISE_KEEP_MASK: u8 = 1;
    pub const LANE_BITWISE_AND_NOT_LANES: u8 = 2;
    pub const LANE_BITWISE_EXTRACT_LANE: u8 = 3;
    pub const LANE_FILTER_QUORUM_AT_LEAST: u8 = 0;
    pub const LANE_FILTER_QUORUM_EXACT: u8 = 1;
    pub const LANE_FILTER_QUORUM_AT_MOST_NONZERO: u8 = 2;
    pub const LANE_REDUCE_AND_REDUCE_AND: u8 = 0;
    pub const LANE_REDUCE_ATTR_SUM: u8 = 0;
    pub const LANE_REDUCE_ATTR_MIN: u8 = 1;
    pub const LANE_REDUCE_ATTR_MAX: u8 = 2;
    pub const LANE_REDUCE_ATTR_COUNT: u8 = 3;
    pub const VECTOR_ANN_SEARCH_IDS: u8 = 0;
    pub const VECTOR_ANN_SEARCH_RADIUS: u8 = 1;
    pub const VECTOR_RERANK_DOT: u8 = 0;
    pub const VECTOR_RERANK_COSINE: u8 = 1;
    pub const VECTOR_RERANK_NEG_L2_SQ: u8 = 2;
    pub const VECTOR_RERANK_L2_SQ: u8 = 3;
    pub const VECTOR_RERANK_PAIR_DOT: u8 = 4;
    pub const VECTOR_RERANK_PAIR_COSINE: u8 = 5;
    pub const VECTOR_RERANK_PAIR_NEG_L2_SQ: u8 = 6;
    pub const VECTOR_RERANK_PAIR_L2_SQ: u8 = 7;
    pub const LANE_STATE_INIT_ZERO_F32: u8 = 0;
    pub const LANE_STATE_INIT_INF_F32: u8 = 1;
    pub const LANE_STATE_INIT_ZERO_F64: u8 = 2;
    pub const LANE_STATE_INIT_INF_F64: u8 = 3;
    pub const LANE_STATE_INIT_FROM_FRONTIER_SEEDS: u8 = 4;
    pub const LANE_STATE_INIT_WITH_WITNESS: u8 = 5;
    pub const LANE_STATE_WALK_FWD_MIN_PLUS: u8 = 0;
    pub const LANE_STATE_WALK_FWD_MAX_PLUS: u8 = 1;
    pub const LANE_STATE_WALK_FWD_MAX_MUL: u8 = 2;
    pub const LANE_STATE_WALK_FWD_SUM_MUL: u8 = 3;
    pub const LANE_STATE_WALK_FWD_MIN_MAX: u8 = 4;
    pub const LANE_STATE_WALK_REV_MIN_PLUS: u8 = 5;
    pub const LANE_STATE_WALK_REV_MAX_PLUS: u8 = 6;
    pub const LANE_STATE_WALK_REV_MAX_MUL: u8 = 7;
    pub const LANE_STATE_WALK_REV_SUM_MUL: u8 = 8;
    pub const LANE_STATE_WALK_REV_MIN_MAX: u8 = 9;
    pub const LANE_STATE_OP_PROJECT_AFFINE: u8 = 0;
    pub const LANE_STATE_OP_PRUNE_CUTOFF: u8 = 1;
    pub const LANE_STATE_OP_PRUNE_HUB_QUORUM: u8 = 2;
    pub const LANE_STATE_OP_TO_LANE_FRONTIER: u8 = 3;
    pub const LANE_STATE_OP_TO_QUORUM_FRONTIER: u8 = 4;
    pub const LANE_STATE_REDUCE_MIN: u8 = 0;
    pub const LANE_STATE_REDUCE_MAX: u8 = 1;
    pub const LANE_STATE_REDUCE_SUM: u8 = 2;
    pub const LANE_STATE_REDUCE_MEAN: u8 = 3;
    pub const LANE_STATE_REDUCE_ARGMIN: u8 = 4;
    pub const LANE_STATE_REDUCE_ARGMAX: u8 = 5;
    pub const LANE_STATE_REDUCE_TRACE_PATH: u8 = 6;
    pub const VECTOR_POOL_LOAD_NODE: u8 = 0;
    pub const VECTOR_POOL_MEAN_POOL: u8 = 1;
    pub const VECTOR_POOL_SUM_POOL: u8 = 2;
    pub const VECTOR_POOL_L2_NORM: u8 = 3;
    pub const EXTENSION_PAYLOAD_PAYLOAD: u8 = 0;
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct OpcodeMeta {
    pub op: Opcode,
    pub code: u8,
    pub pillar: u8,
    pub allowed_flags: u8,
    pub valid_subop_mask: u16,
    pub morsel_role: MorselRole,
    pub name: &'static str,
    pub mnemonic: &'static str,
}

pub const OPCODE_META: [Option<OpcodeMeta>; 256] = {
    let mut tbl: [Option<OpcodeMeta>; 256] = [None; 256];
    tbl[0x00] = Some(OpcodeMeta {
        op: Opcode::OpHalt,
        code: 0x00,
        pillar: 1,
        allowed_flags: 0x0,
        valid_subop_mask: 0x0001,
        morsel_role: MorselRole::Forbidden,
        name: "OP_HALT",
        mnemonic: "HALT",
    });
    tbl[0x01] = Some(OpcodeMeta {
        op: Opcode::OpNop,
        code: 0x01,
        pillar: 1,
        allowed_flags: 0x0,
        valid_subop_mask: 0x0001,
        morsel_role: MorselRole::BodyOk,
        name: "OP_NOP",
        mnemonic: "NOP",
    });
    tbl[0x02] = Some(OpcodeMeta {
        op: Opcode::OpTrap,
        code: 0x02,
        pillar: 1,
        allowed_flags: 0x0,
        valid_subop_mask: 0x0001,
        morsel_role: MorselRole::Forbidden,
        name: "OP_TRAP",
        mnemonic: "TRAP",
    });
    tbl[0x03] = Some(OpcodeMeta {
        op: Opcode::OpThrow,
        code: 0x03,
        pillar: 1,
        allowed_flags: 0x0,
        valid_subop_mask: 0x0003,
        morsel_role: MorselRole::Forbidden,
        name: "OP_THROW",
        mnemonic: "THROW",
    });
    tbl[0x04] = Some(OpcodeMeta {
        op: Opcode::OpAssert,
        code: 0x04,
        pillar: 1,
        allowed_flags: 0x0,
        valid_subop_mask: 0x00FF,
        morsel_role: MorselRole::Forbidden,
        name: "OP_ASSERT",
        mnemonic: "ASSERT",
    });
    tbl[0x05] = Some(OpcodeMeta {
        op: Opcode::OpLoadConst,
        code: 0x05,
        pillar: 1,
        allowed_flags: 0x8,
        valid_subop_mask: 0x00FF,
        morsel_role: MorselRole::Forbidden,
        name: "OP_LOAD_CONST",
        mnemonic: "LOAD_CONST",
    });
    tbl[0x06] = Some(OpcodeMeta {
        op: Opcode::OpLoadConstPool,
        code: 0x06,
        pillar: 1,
        allowed_flags: 0x4,
        valid_subop_mask: 0x0007,
        morsel_role: MorselRole::Forbidden,
        name: "OP_LOAD_CONST_POOL",
        mnemonic: "LOAD_CONST_POOL",
    });
    tbl[0x07] = Some(OpcodeMeta {
        op: Opcode::OpLoadParam,
        code: 0x07,
        pillar: 1,
        allowed_flags: 0x4,
        valid_subop_mask: 0x0007,
        morsel_role: MorselRole::Forbidden,
        name: "OP_LOAD_PARAM",
        mnemonic: "LOAD_PARAM",
    });
    tbl[0x08] = Some(OpcodeMeta {
        op: Opcode::OpMov,
        code: 0x08,
        pillar: 1,
        allowed_flags: 0x0,
        valid_subop_mask: 0x003F,
        morsel_role: MorselRole::BodyOk,
        name: "OP_MOV",
        mnemonic: "MOV",
    });
    tbl[0x09] = Some(OpcodeMeta {
        op: Opcode::OpSwap,
        code: 0x09,
        pillar: 1,
        allowed_flags: 0x0,
        valid_subop_mask: 0x0007,
        morsel_role: MorselRole::BodyOk,
        name: "OP_SWAP",
        mnemonic: "SWAP",
    });
    tbl[0x0A] = Some(OpcodeMeta {
        op: Opcode::OpSpill,
        code: 0x0A,
        pillar: 1,
        allowed_flags: 0x0,
        valid_subop_mask: 0x0007,
        morsel_role: MorselRole::BodyVOnly,
        name: "OP_SPILL",
        mnemonic: "SPILL",
    });
    tbl[0x0B] = Some(OpcodeMeta {
        op: Opcode::OpReload,
        code: 0x0B,
        pillar: 1,
        allowed_flags: 0x0,
        valid_subop_mask: 0x0007,
        morsel_role: MorselRole::BodyVOnly,
        name: "OP_RELOAD",
        mnemonic: "RELOAD",
    });
    tbl[0x0C] = Some(OpcodeMeta {
        op: Opcode::OpScalarIntAlu,
        code: 0x0C,
        pillar: 1,
        allowed_flags: 0x0,
        valid_subop_mask: 0xFFFF,
        morsel_role: MorselRole::Forbidden,
        name: "OP_SCALAR_INT_ALU",
        mnemonic: "SCALAR_INT_ALU",
    });
    tbl[0x0D] = Some(OpcodeMeta {
        op: Opcode::OpScalarFpAlu,
        code: 0x0D,
        pillar: 1,
        allowed_flags: 0x0,
        valid_subop_mask: 0xFFFF,
        morsel_role: MorselRole::Forbidden,
        name: "OP_SCALAR_FP_ALU",
        mnemonic: "SCALAR_FP_ALU",
    });
    tbl[0x0E] = Some(OpcodeMeta {
        op: Opcode::OpScalar3vl,
        code: 0x0E,
        pillar: 1,
        allowed_flags: 0x0,
        valid_subop_mask: 0xFFFF,
        morsel_role: MorselRole::Forbidden,
        name: "OP_SCALAR_3VL",
        mnemonic: "SCALAR_3VL",
    });
    tbl[0x0F] = Some(OpcodeMeta {
        op: Opcode::OpJmp,
        code: 0x0F,
        pillar: 1,
        allowed_flags: 0x0,
        valid_subop_mask: 0x0001,
        morsel_role: MorselRole::FwdJumpOnly,
        name: "OP_JMP",
        mnemonic: "JMP",
    });
    tbl[0x10] = Some(OpcodeMeta {
        op: Opcode::OpJcc,
        code: 0x10,
        pillar: 1,
        allowed_flags: 0x0,
        valid_subop_mask: 0x03FF,
        morsel_role: MorselRole::FwdJumpOnly,
        name: "OP_JCC",
        mnemonic: "JCC",
    });
    tbl[0x11] = Some(OpcodeMeta {
        op: Opcode::OpLoopDecr,
        code: 0x11,
        pillar: 1,
        allowed_flags: 0x0,
        valid_subop_mask: 0x0001,
        morsel_role: MorselRole::Forbidden,
        name: "OP_LOOP_DECR",
        mnemonic: "LOOP_DECR",
    });
    tbl[0x12] = Some(OpcodeMeta {
        op: Opcode::OpCall,
        code: 0x12,
        pillar: 1,
        allowed_flags: 0x0,
        valid_subop_mask: 0x0003,
        morsel_role: MorselRole::Forbidden,
        name: "OP_CALL",
        mnemonic: "CALL",
    });
    tbl[0x13] = Some(OpcodeMeta {
        op: Opcode::OpRet,
        code: 0x13,
        pillar: 1,
        allowed_flags: 0x0,
        valid_subop_mask: 0x0001,
        morsel_role: MorselRole::Forbidden,
        name: "OP_RET",
        mnemonic: "RET",
    });
    tbl[0x14] = Some(OpcodeMeta {
        op: Opcode::OpSetMaxDop,
        code: 0x14,
        pillar: 1,
        allowed_flags: 0x0,
        valid_subop_mask: 0x0003,
        morsel_role: MorselRole::Forbidden,
        name: "OP_SET_MAX_DOP",
        mnemonic: "SET_MAX_DOP",
    });
    tbl[0x15] = Some(OpcodeMeta {
        op: Opcode::OpScalarDecimalAlu,
        code: 0x15,
        pillar: 1,
        allowed_flags: 0x0,
        valid_subop_mask: 0xFFFF,
        morsel_role: MorselRole::Forbidden,
        name: "OP_SCALAR_DECIMAL_ALU",
        mnemonic: "SCALAR_DECIMAL_ALU",
    });
    tbl[0x20] = Some(OpcodeMeta {
        op: Opcode::OpFseed,
        code: 0x20,
        pillar: 2,
        allowed_flags: 0x4,
        valid_subop_mask: 0x001F,
        morsel_role: MorselRole::Forbidden,
        name: "OP_FSEED",
        mnemonic: "FSEED",
    });
    tbl[0x21] = Some(OpcodeMeta {
        op: Opcode::OpInitDomainSpan,
        code: 0x21,
        pillar: 2,
        allowed_flags: 0x0,
        valid_subop_mask: 0x0001,
        morsel_role: MorselRole::Forbidden,
        name: "OP_INIT_DOMAIN_SPAN",
        mnemonic: "INIT_DOMAIN_SPAN",
    });
    tbl[0x22] = Some(OpcodeMeta {
        op: Opcode::OpSetOp,
        code: 0x22,
        pillar: 2,
        allowed_flags: 0x0,
        valid_subop_mask: 0x000F,
        morsel_role: MorselRole::BodyOk,
        name: "OP_SET_OP",
        mnemonic: "SET_OP",
    });
    tbl[0x23] = Some(OpcodeMeta {
        op: Opcode::OpSetComplement,
        code: 0x23,
        pillar: 2,
        allowed_flags: 0x0,
        valid_subop_mask: 0x0001,
        morsel_role: MorselRole::BodyOk,
        name: "OP_SET_COMPLEMENT",
        mnemonic: "SET_COMPLEMENT",
    });
    tbl[0x24] = Some(OpcodeMeta {
        op: Opcode::OpSetCardinality,
        code: 0x24,
        pillar: 2,
        allowed_flags: 0x0,
        valid_subop_mask: 0x001F,
        morsel_role: MorselRole::Forbidden,
        name: "OP_SET_CARDINALITY",
        mnemonic: "SET_CARDINALITY",
    });
    tbl[0x25] = Some(OpcodeMeta {
        op: Opcode::OpSetContains,
        code: 0x25,
        pillar: 2,
        allowed_flags: 0x0,
        valid_subop_mask: 0x0003,
        morsel_role: MorselRole::Forbidden,
        name: "OP_SET_CONTAINS",
        mnemonic: "SET_CONTAINS",
    });
    tbl[0x26] = Some(OpcodeMeta {
        op: Opcode::OpSetConvert,
        code: 0x26,
        pillar: 2,
        allowed_flags: 0x4,
        valid_subop_mask: 0x000F,
        morsel_role: MorselRole::BodyOk,
        name: "OP_SET_CONVERT",
        mnemonic: "SET_CONVERT",
    });
    tbl[0x27] = Some(OpcodeMeta {
        op: Opcode::OpStableCheck,
        code: 0x27,
        pillar: 2,
        allowed_flags: 0x0,
        valid_subop_mask: 0x0003,
        morsel_role: MorselRole::Forbidden,
        name: "OP_STABLE_CHECK",
        mnemonic: "STABLE_CHECK",
    });
    tbl[0x30] = Some(OpcodeMeta {
        op: Opcode::OpWalk,
        code: 0x30,
        pillar: 3,
        allowed_flags: 0x5,
        valid_subop_mask: 0x03FF,
        morsel_role: MorselRole::Forbidden,
        name: "OP_WALK",
        mnemonic: "WALK",
    });
    tbl[0x31] = Some(OpcodeMeta {
        op: Opcode::OpWalkFiltered,
        code: 0x31,
        pillar: 3,
        allowed_flags: 0x5,
        valid_subop_mask: 0xFFFF,
        morsel_role: MorselRole::Forbidden,
        name: "OP_WALK_FILTERED",
        mnemonic: "WALK_FILTERED",
    });
    tbl[0x33] = Some(OpcodeMeta {
        op: Opcode::OpDegree,
        code: 0x33,
        pillar: 3,
        allowed_flags: 0x1,
        valid_subop_mask: 0x003F,
        morsel_role: MorselRole::Forbidden,
        name: "OP_DEGREE",
        mnemonic: "DEGREE",
    });
    tbl[0x34] = Some(OpcodeMeta {
        op: Opcode::OpSpmv,
        code: 0x34,
        pillar: 3,
        allowed_flags: 0x7,
        valid_subop_mask: 0x01FF,
        morsel_role: MorselRole::Forbidden,
        name: "OP_SPMV",
        mnemonic: "SPMV",
    });
    tbl[0x35] = Some(OpcodeMeta {
        op: Opcode::OpWalkMorsel,
        code: 0x35,
        pillar: 3,
        allowed_flags: 0x1,
        valid_subop_mask: 0x000F,
        morsel_role: MorselRole::Header,
        name: "OP_WALK_MORSEL",
        mnemonic: "WALK_MORSEL",
    });
    tbl[0x36] = Some(OpcodeMeta {
        op: Opcode::OpFilterMorsel,
        code: 0x36,
        pillar: 3,
        allowed_flags: 0x0,
        valid_subop_mask: 0x0001,
        morsel_role: MorselRole::Header,
        name: "OP_FILTER_MORSEL",
        mnemonic: "FILTER_MORSEL",
    });
    tbl[0x37] = Some(OpcodeMeta {
        op: Opcode::OpMorselCommit,
        code: 0x37,
        pillar: 3,
        allowed_flags: 0x7,
        valid_subop_mask: 0x03FF,
        morsel_role: MorselRole::CommitOnly,
        name: "OP_MORSEL_COMMIT",
        mnemonic: "MORSEL_COMMIT",
    });
    tbl[0x38] = Some(OpcodeMeta {
        op: Opcode::OpMorselEnd,
        code: 0x38,
        pillar: 3,
        allowed_flags: 0x0,
        valid_subop_mask: 0x0001,
        morsel_role: MorselRole::EndOnly,
        name: "OP_MORSEL_END",
        mnemonic: "MORSEL_END",
    });
    tbl[0x39] = Some(OpcodeMeta {
        op: Opcode::OpSampleNeighbors,
        code: 0x39,
        pillar: 3,
        allowed_flags: 0x0,
        valid_subop_mask: 0x000F,
        morsel_role: MorselRole::Forbidden,
        name: "OP_SAMPLE_NEIGHBORS",
        mnemonic: "SAMPLE_NEIGHBORS",
    });
    tbl[0x3A] = Some(OpcodeMeta {
        op: Opcode::OpHasTopology,
        code: 0x3A,
        pillar: 3,
        allowed_flags: 0x0,
        valid_subop_mask: 0x03FF,
        morsel_role: MorselRole::Forbidden,
        name: "OP_HAS_TOPOLOGY",
        mnemonic: "HAS_TOPOLOGY",
    });
    tbl[0x3B] = Some(OpcodeMeta {
        op: Opcode::OpGraphKernel,
        code: 0x3B,
        pillar: 3,
        allowed_flags: 0x1,
        valid_subop_mask: 0x001F,
        morsel_role: MorselRole::Forbidden,
        name: "OP_GRAPH_KERNEL",
        mnemonic: "GRAPH_KERNEL",
    });
    tbl[0x40] = Some(OpcodeMeta {
        op: Opcode::OpLoadColumn,
        code: 0x40,
        pillar: 4,
        allowed_flags: 0x0,
        valid_subop_mask: 0x0003,
        morsel_role: MorselRole::Forbidden,
        name: "OP_LOAD_COLUMN",
        mnemonic: "LOAD_COLUMN",
    });
    tbl[0x41] = Some(OpcodeMeta {
        op: Opcode::OpGatherNodeAttr,
        code: 0x41,
        pillar: 4,
        allowed_flags: 0x1,
        valid_subop_mask: 0x0007,
        morsel_role: MorselRole::BodyOk,
        name: "OP_GATHER_NODE_ATTR",
        mnemonic: "GATHER_NODE_ATTR",
    });
    tbl[0x42] = Some(OpcodeMeta {
        op: Opcode::OpGatherEdgeAttr,
        code: 0x42,
        pillar: 4,
        allowed_flags: 0x1,
        valid_subop_mask: 0x0003,
        morsel_role: MorselRole::BodyOk,
        name: "OP_GATHER_EDGE_ATTR",
        mnemonic: "GATHER_EDGE_ATTR",
    });
    tbl[0x43] = Some(OpcodeMeta {
        op: Opcode::OpGatherVec,
        code: 0x43,
        pillar: 4,
        allowed_flags: 0x1,
        valid_subop_mask: 0x0001,
        morsel_role: MorselRole::BodyOk,
        name: "OP_GATHER_VEC",
        mnemonic: "GATHER_VEC",
    });
    tbl[0x44] = Some(OpcodeMeta {
        op: Opcode::OpNodeFilter,
        code: 0x44,
        pillar: 4,
        allowed_flags: 0x6,
        valid_subop_mask: 0x01FF,
        morsel_role: MorselRole::Forbidden,
        name: "OP_NODE_FILTER",
        mnemonic: "NODE_FILTER",
    });
    tbl[0x45] = Some(OpcodeMeta {
        op: Opcode::OpVecCmp,
        code: 0x45,
        pillar: 4,
        allowed_flags: 0x3,
        valid_subop_mask: 0x00FF,
        morsel_role: MorselRole::BodyOk,
        name: "OP_VEC_CMP",
        mnemonic: "VEC_CMP",
    });
    tbl[0x46] = Some(OpcodeMeta {
        op: Opcode::OpVec3vlOp,
        code: 0x46,
        pillar: 4,
        allowed_flags: 0x3,
        valid_subop_mask: 0x0FFF,
        morsel_role: MorselRole::BodyOk,
        name: "OP_VEC_3VL_OP",
        mnemonic: "VEC_3VL_OP",
    });
    tbl[0x47] = Some(OpcodeMeta {
        op: Opcode::OpVecStrPred,
        code: 0x47,
        pillar: 4,
        allowed_flags: 0x3,
        valid_subop_mask: 0x03FF,
        morsel_role: MorselRole::BodyOk,
        name: "OP_VEC_STR_PRED",
        mnemonic: "VEC_STR_PRED",
    });
    tbl[0x48] = Some(OpcodeMeta {
        op: Opcode::OpVecMathArith,
        code: 0x48,
        pillar: 4,
        allowed_flags: 0x3,
        valid_subop_mask: 0xFFFF,
        morsel_role: MorselRole::BodyOk,
        name: "OP_VEC_MATH_ARITH",
        mnemonic: "VEC_MATH_ARITH",
    });
    tbl[0x49] = Some(OpcodeMeta {
        op: Opcode::OpVecMathExpLog,
        code: 0x49,
        pillar: 4,
        allowed_flags: 0x3,
        valid_subop_mask: 0x3FFF,
        morsel_role: MorselRole::BodyOk,
        name: "OP_VEC_MATH_EXP_LOG",
        mnemonic: "VEC_MATH_EXP_LOG",
    });
    tbl[0x4A] = Some(OpcodeMeta {
        op: Opcode::OpVecMathTrigHyp,
        code: 0x4A,
        pillar: 4,
        allowed_flags: 0x3,
        valid_subop_mask: 0x3FFF,
        morsel_role: MorselRole::BodyOk,
        name: "OP_VEC_MATH_TRIG_HYP",
        mnemonic: "VEC_MATH_TRIG_HYP",
    });
    tbl[0x4B] = Some(OpcodeMeta {
        op: Opcode::OpVecMathAct,
        code: 0x4B,
        pillar: 4,
        allowed_flags: 0x3,
        valid_subop_mask: 0x003F,
        morsel_role: MorselRole::BodyOk,
        name: "OP_VEC_MATH_ACT",
        mnemonic: "VEC_MATH_ACT",
    });
    tbl[0x4C] = Some(OpcodeMeta {
        op: Opcode::OpVecIntBitwise,
        code: 0x4C,
        pillar: 4,
        allowed_flags: 0x3,
        valid_subop_mask: 0x3FFF,
        morsel_role: MorselRole::BodyOk,
        name: "OP_VEC_INT_BITWISE",
        mnemonic: "VEC_INT_BITWISE",
    });
    tbl[0x4D] = Some(OpcodeMeta {
        op: Opcode::OpVecTernary,
        code: 0x4D,
        pillar: 4,
        allowed_flags: 0x2,
        valid_subop_mask: 0x03FF,
        morsel_role: MorselRole::BodyOk,
        name: "OP_VEC_TERNARY",
        mnemonic: "VEC_TERNARY",
    });
    tbl[0x4E] = Some(OpcodeMeta {
        op: Opcode::OpVecBlend,
        code: 0x4E,
        pillar: 4,
        allowed_flags: 0x2,
        valid_subop_mask: 0x000F,
        morsel_role: MorselRole::BodyOk,
        name: "OP_VEC_BLEND",
        mnemonic: "VEC_BLEND",
    });
    tbl[0x4F] = Some(OpcodeMeta {
        op: Opcode::OpCoalesce,
        code: 0x4F,
        pillar: 4,
        allowed_flags: 0x3,
        valid_subop_mask: 0x0001,
        morsel_role: MorselRole::BodyOk,
        name: "OP_COALESCE",
        mnemonic: "COALESCE",
    });
    tbl[0x50] = Some(OpcodeMeta {
        op: Opcode::OpVecInit,
        code: 0x50,
        pillar: 4,
        allowed_flags: 0x2,
        valid_subop_mask: 0x7F7F,
        morsel_role: MorselRole::Forbidden,
        name: "OP_VEC_INIT",
        mnemonic: "VEC_INIT",
    });
    tbl[0x51] = Some(OpcodeMeta {
        op: Opcode::OpVecPoint,
        code: 0x51,
        pillar: 4,
        allowed_flags: 0x0,
        valid_subop_mask: 0x000F,
        morsel_role: MorselRole::BodyCastOnly,
        name: "OP_VEC_POINT",
        mnemonic: "VEC_POINT",
    });
    tbl[0x52] = Some(OpcodeMeta {
        op: Opcode::OpVecReduce,
        code: 0x52,
        pillar: 4,
        allowed_flags: 0x1,
        valid_subop_mask: 0x0FFF,
        morsel_role: MorselRole::Forbidden,
        name: "OP_VEC_REDUCE",
        mnemonic: "VEC_REDUCE",
    });
    tbl[0x53] = Some(OpcodeMeta {
        op: Opcode::OpTopK,
        code: 0x53,
        pillar: 4,
        allowed_flags: 0x3,
        valid_subop_mask: 0x000F,
        morsel_role: MorselRole::Forbidden,
        name: "OP_TOP_K",
        mnemonic: "TOP_K",
    });
    tbl[0x54] = Some(OpcodeMeta {
        op: Opcode::OpIndexScan,
        code: 0x54,
        pillar: 4,
        allowed_flags: 0x4,
        valid_subop_mask: 0x007F,
        morsel_role: MorselRole::Forbidden,
        name: "OP_INDEX_SCAN",
        mnemonic: "INDEX_SCAN",
    });
    tbl[0x55] = Some(OpcodeMeta {
        op: Opcode::OpMapKeysToDense,
        code: 0x55,
        pillar: 4,
        allowed_flags: 0x4,
        valid_subop_mask: 0x001F,
        morsel_role: MorselRole::Forbidden,
        name: "OP_MAP_KEYS_TO_DENSE",
        mnemonic: "MAP_KEYS_TO_DENSE",
    });
    tbl[0x56] = Some(OpcodeMeta {
        op: Opcode::OpMapDenseToKeys,
        code: 0x56,
        pillar: 4,
        allowed_flags: 0x0,
        valid_subop_mask: 0x0007,
        morsel_role: MorselRole::Forbidden,
        name: "OP_MAP_DENSE_TO_KEYS",
        mnemonic: "MAP_DENSE_TO_KEYS",
    });
    tbl[0x57] = Some(OpcodeMeta {
        op: Opcode::OpCollect,
        code: 0x57,
        pillar: 4,
        allowed_flags: 0x1,
        valid_subop_mask: 0x001F,
        morsel_role: MorselRole::Forbidden,
        name: "OP_COLLECT",
        mnemonic: "COLLECT",
    });
    tbl[0x58] = Some(OpcodeMeta {
        op: Opcode::OpVecDecimalAlu,
        code: 0x58,
        pillar: 4,
        allowed_flags: 0x3,
        valid_subop_mask: 0xFFFF,
        morsel_role: MorselRole::BodyOk,
        name: "OP_VEC_DECIMAL_ALU",
        mnemonic: "VEC_DECIMAL_ALU",
    });
    tbl[0x60] = Some(OpcodeMeta {
        op: Opcode::OpLaneInitSeeds,
        code: 0x60,
        pillar: 5,
        allowed_flags: 0x0,
        valid_subop_mask: 0x0003,
        morsel_role: MorselRole::Forbidden,
        name: "OP_LANE_INIT_SEEDS",
        mnemonic: "LANE_INIT_SEEDS",
    });
    tbl[0x61] = Some(OpcodeMeta {
        op: Opcode::OpLaneInitGroups,
        code: 0x61,
        pillar: 5,
        allowed_flags: 0x4,
        valid_subop_mask: 0x0001,
        morsel_role: MorselRole::Forbidden,
        name: "OP_LANE_INIT_GROUPS",
        mnemonic: "LANE_INIT_GROUPS",
    });
    tbl[0x63] = Some(OpcodeMeta {
        op: Opcode::OpLaneWalk,
        code: 0x63,
        pillar: 5,
        allowed_flags: 0x5,
        valid_subop_mask: 0x03FF,
        morsel_role: MorselRole::Forbidden,
        name: "OP_LANE_WALK",
        mnemonic: "LANE_WALK",
    });
    tbl[0x64] = Some(OpcodeMeta {
        op: Opcode::OpLaneBitwise,
        code: 0x64,
        pillar: 5,
        allowed_flags: 0x2,
        valid_subop_mask: 0x000F,
        morsel_role: MorselRole::Forbidden,
        name: "OP_LANE_BITWISE",
        mnemonic: "LANE_BITWISE",
    });
    tbl[0x65] = Some(OpcodeMeta {
        op: Opcode::OpLaneFilterQuorum,
        code: 0x65,
        pillar: 5,
        allowed_flags: 0x6,
        valid_subop_mask: 0x0007,
        morsel_role: MorselRole::Forbidden,
        name: "OP_LANE_FILTER_QUORUM",
        mnemonic: "LANE_FILTER_QUORUM",
    });
    tbl[0x66] = Some(OpcodeMeta {
        op: Opcode::OpLaneReduceAnd,
        code: 0x66,
        pillar: 5,
        allowed_flags: 0x6,
        valid_subop_mask: 0x0001,
        morsel_role: MorselRole::Forbidden,
        name: "OP_LANE_REDUCE_AND",
        mnemonic: "LANE_REDUCE_AND",
    });
    tbl[0x67] = Some(OpcodeMeta {
        op: Opcode::OpLaneReduceAttr,
        code: 0x67,
        pillar: 5,
        allowed_flags: 0x0,
        valid_subop_mask: 0x000F,
        morsel_role: MorselRole::Forbidden,
        name: "OP_LANE_REDUCE_ATTR",
        mnemonic: "LANE_REDUCE_ATTR",
    });
    tbl[0x69] = Some(OpcodeMeta {
        op: Opcode::OpVectorAnnSearch,
        code: 0x69,
        pillar: 5,
        allowed_flags: 0x1,
        valid_subop_mask: 0x0003,
        morsel_role: MorselRole::Forbidden,
        name: "OP_VECTOR_ANN_SEARCH",
        mnemonic: "VECTOR_ANN_SEARCH",
    });
    tbl[0x6A] = Some(OpcodeMeta {
        op: Opcode::OpVectorRerank,
        code: 0x6A,
        pillar: 5,
        allowed_flags: 0x1,
        valid_subop_mask: 0x00FF,
        morsel_role: MorselRole::BodyPairOnly,
        name: "OP_VECTOR_RERANK",
        mnemonic: "VECTOR_RERANK",
    });
    tbl[0x6B] = Some(OpcodeMeta {
        op: Opcode::OpLaneStateInit,
        code: 0x6B,
        pillar: 5,
        allowed_flags: 0x2,
        valid_subop_mask: 0x003F,
        morsel_role: MorselRole::Forbidden,
        name: "OP_LANE_STATE_INIT",
        mnemonic: "LANE_STATE_INIT",
    });
    tbl[0x6C] = Some(OpcodeMeta {
        op: Opcode::OpLaneStateWalk,
        code: 0x6C,
        pillar: 5,
        allowed_flags: 0x7,
        valid_subop_mask: 0x03FF,
        morsel_role: MorselRole::Forbidden,
        name: "OP_LANE_STATE_WALK",
        mnemonic: "LANE_STATE_WALK",
    });
    tbl[0x6D] = Some(OpcodeMeta {
        op: Opcode::OpLaneStateOp,
        code: 0x6D,
        pillar: 5,
        allowed_flags: 0x2,
        valid_subop_mask: 0x001F,
        morsel_role: MorselRole::Forbidden,
        name: "OP_LANE_STATE_OP",
        mnemonic: "LANE_STATE_OP",
    });
    tbl[0x6E] = Some(OpcodeMeta {
        op: Opcode::OpLaneStateReduce,
        code: 0x6E,
        pillar: 5,
        allowed_flags: 0x2,
        valid_subop_mask: 0x007F,
        morsel_role: MorselRole::Forbidden,
        name: "OP_LANE_STATE_REDUCE",
        mnemonic: "LANE_STATE_REDUCE",
    });
    tbl[0x6F] = Some(OpcodeMeta {
        op: Opcode::OpVectorPool,
        code: 0x6F,
        pillar: 5,
        allowed_flags: 0x0,
        valid_subop_mask: 0x000F,
        morsel_role: MorselRole::Forbidden,
        name: "OP_VECTOR_POOL",
        mnemonic: "VECTOR_POOL",
    });
    tbl[0xFF] = Some(OpcodeMeta {
        op: Opcode::OpExtensionPayload,
        code: 0xFF,
        pillar: 0,
        allowed_flags: 0x0,
        valid_subop_mask: 0x0001,
        morsel_role: MorselRole::Forbidden,
        name: "OP_EXTENSION_PAYLOAD",
        mnemonic: "EXTENSION_PAYLOAD",
    });
    tbl
};

#[inline]
pub const fn encode_word(
    op: Opcode,
    flags: u8,
    subop: u8,
    dst: u8,
    s1: u8,
    s2: u8,
    s3: u8,
    imm32: u32,
) -> u64 {
    let b0 = (op as u64) & 0xFF;
    let b1 = (((flags as u64) & 0xF) << 4) | ((subop as u64) & 0xF);
    let b2 = (((dst as u64) & 0xF) << 4) | ((s1 as u64) & 0xF);
    let b3 = (((s2 as u64) & 0xF) << 4) | ((s3 as u64) & 0xF);
    b0 | (b1 << 8) | (b2 << 16) | (b3 << 24) | ((imm32 as u64) << 32)
}

#[inline]
pub const fn encode_extension_word(imm32_hi: u32) -> u64 {
    0xFF | ((imm32_hi as u64) << 32)
}
