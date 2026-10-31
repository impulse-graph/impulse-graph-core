/**
 * @file impulse_vm.cpp
 * @brief ImpulseVM bytecode execution engine, static verifier, and output buffer materializer.
 *
 * Individual opcode implementations reside in `src/opcodes/op_<hex>_<name>.inc` and are included
 * directly into `impulse_vm_execute` to preserve zero-overhead computed-goto dispatch.
 */
#include "impulse_vm_internal.hpp"
#include "impulse_opcode_contracts.hpp"
using namespace impulse::vm;
#include "opcodes/shared/mock_attr_common.hpp"
#include "opcodes/shared/coo_walk_common.hpp"
#include "opcodes/shared/dense_walk_common.hpp"
#include "opcodes/shared/walk_stream_common.hpp"
#include "opcodes/shared/lane_walk_common.hpp"

#if defined(__clang__)
  #pragma clang diagnostic push
  #pragma clang diagnostic ignored "-Wgnu-label-as-value"
  #pragma clang diagnostic ignored "-Wgnu-designator"
  #pragma clang diagnostic ignored "-Wc99-designator"
  #pragma clang diagnostic ignored "-Winitializer-overrides"
#elif defined(__GNUC__)
  #pragma GCC diagnostic push
  #pragma GCC diagnostic ignored "-Wpedantic"
#endif

extern "C" {

// Main execution routine
impulse_vm_status_t impulse_vm_execute(
    const impulse_instruction_t* bytecode,
    size_t instruction_count,
    impulse_vm_state_t* vm_state,
    uint64_t input_param
) {
    if (!bytecode || !vm_state) return IMPULSE_VM_ERR_NULL_SNAPSHOT;

#if HAS_COMPUTED_GOTO
    // Jump table containing addresses of labels
    static const void* dispatch_table[256];
    static bool dispatch_inited = false;
    if (!dispatch_inited) {
        for (int i = 0; i < 256; ++i) dispatch_table[i] = &&op_INVALID;
        dispatch_table[OP_INIT_MOCK_NODE_ATTR] = &&op_INIT_MOCK_NODE_ATTR;
        dispatch_table[OP_INIT_MOCK_EDGE_ATTR] = &&op_INIT_MOCK_EDGE_ATTR;
        for (int i = 0x0D; i <= 0x0F; ++i) dispatch_table[i] = &&op_RESERVED;
        dispatch_table[0x28] = &&op_RESERVED;
        dispatch_table[0x29] = &&op_RESERVED;
        dispatch_table[0x2B] = &&op_RESERVED;
        dispatch_table[0x2C] = &&op_RESERVED;
        for (int i = 0x3A; i <= 0x3F; ++i) dispatch_table[i] = &&op_RESERVED;
        for (int i = 0x4C; i <= 0x4F; ++i) dispatch_table[i] = &&op_RESERVED;
        for (int i = 0x59; i <= 0x59; ++i) dispatch_table[i] = &&op_RESERVED;
        for (int i = 0x5D; i <= 0x5F; ++i) dispatch_table[i] = &&op_RESERVED;
        for (int i = 0x6D; i <= 0x6F; ++i) dispatch_table[i] = &&op_RESERVED;
        for (int i = 0x76; i <= 0x7F; ++i) dispatch_table[i] = &&op_RESERVED;

        dispatch_table[OP_HALT] = &&op_HALT;
        dispatch_table[OP_NOP] = &&op_NOP;
        dispatch_table[OP_INIT_INPUT_NODE] = &&op_INIT_INPUT_NODE;
        dispatch_table[OP_INIT_INPUT_SET] = &&op_INIT_INPUT_SET;
        dispatch_table[OP_LOAD_CONST_INT] = &&op_LOAD_CONST_INT;
        dispatch_table[OP_MAP_KEYS_TO_DENSE] = &&op_MAP_KEYS_TO_DENSE;
        dispatch_table[OP_LOAD_CONST_FLOAT] = &&op_LOAD_CONST_FLOAT;
        dispatch_table[OP_LOAD_CONST_STR_PREFIX] = &&op_LOAD_CONST_STR_PREFIX;
        dispatch_table[OP_LOAD_INLINE_ARRAY] = &&op_LOAD_INLINE_ARRAY;
        dispatch_table[OP_INIT_MOCK_GRAPH] = &&op_INIT_MOCK_GRAPH;
        dispatch_table[OP_LOAD_INLINE_SET] = &&op_LOAD_INLINE_SET;
        dispatch_table[OP_CSR_WALK_2HOP] = &&op_CSR_WALK_2HOP;
        dispatch_table[OP_CSR_WALK_STATE] = &&op_CSR_WALK_STATE;
        dispatch_table[OP_PROJECT_STATE] = &&op_PROJECT_STATE;
        dispatch_table[OP_CSR_WALK] = &&op_CSR_WALK;
        dispatch_table[OP_CSR_WALK_FILTERED] = &&op_CSR_WALK_FILTERED;
        dispatch_table[OP_CSR_DEGREE] = &&op_CSR_DEGREE;
        dispatch_table[OP_CSR_WALK_PREDICATE] = &&op_CSR_WALK_PREDICATE;
        dispatch_table[OP_NODE_FILTER] = &&op_NODE_FILTER;
        dispatch_table[OP_NODE_FILTER_STR_PREFIX] = &&op_NODE_FILTER_STR_PREFIX;
        dispatch_table[OP_CSR_WALK_REDUCE_SUM] = &&op_CSR_WALK_REDUCE_SUM;
        dispatch_table[OP_CSR_WALK_REDUCE] = &&op_CSR_WALK_REDUCE;
        dispatch_table[OP_CSC_WALK] = &&op_CSC_WALK;
        dispatch_table[OP_HAS_CSR] = &&op_HAS_CSR;
        dispatch_table[OP_HAS_CSC] = &&op_HAS_CSC;
        dispatch_table[OP_HAS_COO] = &&op_HAS_COO;
        dispatch_table[OP_HAS_KEY_CATALOG] = &&op_HAS_KEY_CATALOG;
        dispatch_table[OP_ADAPTIVE_WALK] = &&op_ADAPTIVE_WALK;
        dispatch_table[OP_CREATE_SCRATCH_INDEX] = &&op_CREATE_SCRATCH_INDEX;
        dispatch_table[OP_DROP_SCRATCH_INDEX] = &&op_DROP_SCRATCH_INDEX;
        dispatch_table[OP_VEC_CMP_EQ] = &&op_VEC_CMP_EQ;
        dispatch_table[OP_VEC_CMP_GT] = &&op_VEC_CMP_GT;
        dispatch_table[OP_VEC_CMP_LT] = &&op_VEC_CMP_LT;
        dispatch_table[OP_VEC_CMP_BETWEEN] = &&op_VEC_CMP_BETWEEN;
        dispatch_table[OP_MASK_AND] = &&op_MASK_AND;
        dispatch_table[OP_MASK_OR] = &&op_MASK_OR;
        dispatch_table[OP_MASK_NOT] = &&op_MASK_NOT;
        dispatch_table[OP_VEC_BLEND] = &&op_VEC_BLEND;
        dispatch_table[OP_ASSERT_FINITE] = &&op_ASSERT_FINITE;
        dispatch_table[OP_VEC_MATH_UNARY] = &&op_VEC_MATH_UNARY;
        dispatch_table[OP_VEC_MATH_BINARY] = &&op_VEC_MATH_BINARY;
        dispatch_table[OP_VEC_MATH_TERNARY] = &&op_VEC_MATH_TERNARY;

        dispatch_table[OP_COALESCE] = &&op_COALESCE;
        dispatch_table[OP_EXTRACT_VALIDITY] = &&op_EXTRACT_VALIDITY;
        dispatch_table[OP_SET_UNION] = &&op_SET_UNION;

        dispatch_table[OP_SET_INTERSECT] = &&op_SET_INTERSECT;
        dispatch_table[OP_SET_DIFFERENCE] = &&op_SET_DIFFERENCE;
        dispatch_table[OP_SET_CARDINALITY] = &&op_SET_CARDINALITY;
        dispatch_table[OP_VECTOR_MUL_ATTR] = &&op_VECTOR_MUL_ATTR;
        dispatch_table[OP_VECTOR_REDUCE_SUM] = &&op_VECTOR_REDUCE_SUM;
        dispatch_table[OP_VECTOR_DIV] = &&op_VECTOR_DIV;
        dispatch_table[OP_VECTOR_STR_CONCAT] = &&op_VECTOR_STR_CONCAT;
        dispatch_table[OP_FLOAT_VECTOR_SCALE] = &&op_FLOAT_VECTOR_SCALE;
        dispatch_table[OP_L1_NORM_DIFF] = &&op_L1_NORM_DIFF;
        dispatch_table[OP_VECTOR_TIME_VALID_AT] = &&op_VECTOR_TIME_VALID_AT;
        dispatch_table[OP_CC_AFFOREST] = &&op_CC_AFFOREST;
        dispatch_table[OP_MXV] = &&op_MXV;
        dispatch_table[OP_VXM] = &&op_VXM;
        dispatch_table[OP_EWISE_ADD] = &&op_EWISE_ADD;
        dispatch_table[OP_EWISE_MULT] = &&op_EWISE_MULT;
        dispatch_table[OP_REDUCE] = &&op_REDUCE;
        dispatch_table[OP_JMP] = &&op_JMP;
        dispatch_table[OP_JZ] = &&op_JZ;
        dispatch_table[OP_JNZ] = &&op_JNZ;
        dispatch_table[OP_LOOP_DECR] = &&op_LOOP_DECR;
        dispatch_table[OP_STABLE_CHECK] = &&op_STABLE_CHECK;
        dispatch_table[OP_CALL] = &&op_CALL;
        dispatch_table[OP_RET] = &&op_RET;
        dispatch_table[OP_THROW] = &&op_THROW;
        dispatch_table[OP_ASSERT] = &&op_ASSERT;
        dispatch_table[OP_TRAP] = &&op_TRAP;
        dispatch_table[OP_ENTER_FRAME] = &&op_ENTER_FRAME;
        dispatch_table[OP_LEAVE_FRAME] = &&op_LEAVE_FRAME;
        dispatch_table[OP_SAMPLE_NEIGHBORS] = &&op_SAMPLE_NEIGHBORS;
        dispatch_table[OP_RANDOM_WALK] = &&op_RANDOM_WALK;
        dispatch_table[OP_SCATTER_GATHER] = &&op_SCATTER_GATHER;
        dispatch_table[OP_REBAC_CHECK] = &&op_REBAC_CHECK;
        dispatch_table[OP_ROARING_BITMAP_OR] = &&op_ROARING_BITMAP_OR;
        dispatch_table[OP_ROARING_BITMAP_AND] = &&op_ROARING_BITMAP_AND;
        dispatch_table[OP_ROARING_BITMAP_AND_NOT] = &&op_ROARING_BITMAP_AND_NOT;
        dispatch_table[OP_SPARSE_MATVEC] = &&op_SPARSE_MATVEC;
        dispatch_table[OP_LOUVAIN_MODULARITY] = &&op_LOUVAIN_MODULARITY;
        dispatch_table[OP_KCORE_DECOMPOSITION] = &&op_KCORE_DECOMPOSITION;
        dispatch_table[OP_MOTIF_MATCH_3] = &&op_MOTIF_MATCH_3;
        dispatch_table[OP_GRAPH_ISOMORPHISM] = &&op_GRAPH_ISOMORPHISM;
        dispatch_table[OP_ISLAND_DETECT] = &&op_ISLAND_DETECT;
        dispatch_table[OP_READ_EDGE_WEIGHT] = &&op_READ_EDGE_WEIGHT;
        dispatch_table[OP_CC_HOOK_COMPRESS] = &&op_CC_HOOK_COMPRESS;
        dispatch_table[OP_TC_SWEEP_BATCH] = &&op_TC_SWEEP_BATCH;
        dispatch_table[OP_BRANDES_FORWARD] = &&op_BRANDES_FORWARD;
        dispatch_table[OP_BRANDES_BACKWARD] = &&op_BRANDES_BACKWARD;
        dispatch_table[OP_DELTA_STEP_RELAX] = &&op_DELTA_STEP_RELAX;
        dispatch_table[OP_MOV] = &&op_MOV;
        dispatch_table[OP_CLEAR_REG] = &&op_CLEAR_REG;
        dispatch_table[OP_LOAD_INDIRECT] = &&op_LOAD_INDIRECT;
        dispatch_table[OP_ALLOC_SCRATCH] = &&op_ALLOC_SCRATCH;
        dispatch_table[OP_ASSERT_SCRATCH_BYTES] = &&op_ASSERT_SCRATCH_BYTES;
        dispatch_table[OP_SET_MAX_DOP] = &&op_SET_MAX_DOP;
        dispatch_table[OP_LOAD_COLUMN_VECTOR] = &&op_LOAD_COLUMN_VECTOR;
        dispatch_table[OP_GATHER_NODE_ATTR] = &&op_GATHER_NODE_ATTR;
        dispatch_table[OP_GATHER_EDGE_ATTR] = &&op_GATHER_EDGE_ATTR;
        dispatch_table[OP_BRIN_ZONE_SKIP] = &&op_BRIN_ZONE_SKIP;
        dispatch_table[OP_CSR_WALK_DIRECT_STORE] = &&op_CSR_WALK_DIRECT_STORE;
        dispatch_table[OP_CSR_WALK_DENSE_STREAM] = &&op_CSR_WALK_DENSE_STREAM;
        dispatch_table[OP_COO_WALK] = &&op_COO_WALK;
        dispatch_table[OP_CSC_WALK_DIRECT_STORE] = &&op_CSC_WALK_DIRECT_STORE;
        dispatch_table[OP_FIXPOINT_KLEENE_STAR] = &&op_FIXPOINT_KLEENE_STAR;
        dispatch_table[OP_SWAP_REG] = &&op_SWAP_REG;
        dispatch_table[OP_FRONTIER_DIFF] = &&op_FRONTIER_DIFF;
        dispatch_table[OP_COO_WALK_FILTERED] = &&op_COO_WALK_FILTERED;
        dispatch_table[OP_COO_WALK_REDUCE] = &&op_COO_WALK_REDUCE;
        dispatch_table[OP_COO_WALK_DIRECT_STORE] = &&op_COO_WALK_DIRECT_STORE;
        dispatch_table[OP_DENSE_WALK] = &&op_DENSE_WALK;
        dispatch_table[OP_DENSE_WALK_BITMATRIX] = &&op_DENSE_WALK_BITMATRIX;
        dispatch_table[OP_COLLECT_BITSET] = &&op_COLLECT_BITSET;
        dispatch_table[OP_COLLECT_ARRAY] = &&op_COLLECT_ARRAY;
        dispatch_table[OP_MAP_KEYS_TO_DENSE] = &&op_MAP_KEYS_TO_DENSE;
        dispatch_table[OP_MAP_DENSE_TO_KEYS] = &&op_MAP_DENSE_TO_KEYS;
        dispatch_table[OP_COLLECT_VALUE_MAP] = &&op_COLLECT_VALUE_MAP;
        dispatch_table[OP_DENSE_WALK_REDUCE] = &&op_DENSE_WALK_REDUCE;
        dispatch_table[OP_DENSE_WALK_DIRECT_STORE] = &&op_DENSE_WALK_DIRECT_STORE;
        dispatch_table[OP_CSR_WALK_STREAM] = &&op_CSR_WALK_STREAM;
        dispatch_table[OP_CSC_WALK_STREAM] = &&op_CSC_WALK_STREAM;
        dispatch_table[OP_COO_WALK_STREAM] = &&op_COO_WALK_STREAM;
        dispatch_table[OP_STREAM_FUNC_BEGIN] = &&op_NOP;
        dispatch_table[OP_STREAM_FUNC_END] = &&op_NOP;
        dispatch_table[OP_STREAM_LOAD_SRC] = &&op_NOP;
        dispatch_table[OP_STREAM_LOAD_TGT] = &&op_NOP;
        dispatch_table[OP_STREAM_LOAD_EDGE] = &&op_NOP;
        dispatch_table[OP_STREAM_MATH_ADD] = &&op_NOP;
        dispatch_table[OP_STREAM_MATH_SUB] = &&op_NOP;
        dispatch_table[OP_STREAM_MATH_MUL] = &&op_NOP;
        dispatch_table[OP_STREAM_MATH_DIV] = &&op_NOP;
        dispatch_table[OP_STREAM_MATH_MOD] = &&op_NOP;
        dispatch_table[OP_STREAM_MATH_UNARY] = &&op_NOP;
        dispatch_table[OP_STREAM_CMP_EQ] = &&op_NOP;
        dispatch_table[OP_STREAM_CMP_NEQ] = &&op_NOP;
        dispatch_table[OP_STREAM_CMP_GT] = &&op_NOP;
        dispatch_table[OP_STREAM_CMP_LT] = &&op_NOP;
        dispatch_table[OP_STREAM_LOGIC_AND] = &&op_NOP;
        dispatch_table[OP_STREAM_LOGIC_OR] = &&op_NOP;
        dispatch_table[OP_STREAM_LOGIC_NOT] = &&op_NOP;
        dispatch_table[OP_STREAM_SELECT] = &&op_NOP;
        dispatch_table[OP_STREAM_FILTER] = &&op_NOP;
        dispatch_table[OP_STREAM_YIELD] = &&op_NOP;
        dispatch_table[OP_STREAM_SCATTER_REDUCE] = &&op_NOP;
        dispatch_table[OP_STREAM_REDUCE] = &&op_NOP;
        dispatch_table[OP_STREAM_REDUCE_ARGMIN] = &&op_NOP;
        dispatch_table[OP_STREAM_REDUCE_ARGMAX] = &&op_NOP;
        dispatch_table[OP_STREAM_LOAD_SRC_ID] = &&op_NOP;
        dispatch_table[OP_STREAM_LOAD_TGT_ID] = &&op_NOP;
        dispatch_table[OP_STREAM_LOAD_EDGE_ID] = &&op_NOP;
        dispatch_table[OP_STREAM_LOAD_CONST] = &&op_NOP;
        dispatch_table[OP_LANE_INIT_SEEDS] = &&op_LANE_INIT_SEEDS;
        dispatch_table[OP_LANE_INIT_GROUPS] = &&op_LANE_INIT_GROUPS;
        dispatch_table[OP_LANE_SET_EDGE_MASK] = &&op_LANE_SET_EDGE_MASK;
        dispatch_table[OP_LANE_CSR_WALK] = &&op_LANE_CSR_WALK;
        dispatch_table[OP_LANE_CSC_WALK] = &&op_LANE_CSC_WALK;
        dispatch_table[OP_LANE_PRUNE_MASK] = &&op_LANE_PRUNE_MASK;
        dispatch_table[OP_LANE_FILTER_QUORUM] = &&op_LANE_FILTER_QUORUM;
        dispatch_table[OP_LANE_REDUCE_AND] = &&op_LANE_REDUCE_AND;
        dispatch_table[OP_LANE_REDUCE_ATTR] = &&op_LANE_REDUCE_ATTR;
        dispatch_table[OP_LANE_SET_COVER] = &&op_LANE_SET_COVER;
        dispatch_table[OP_VECTOR_ANN_SEARCH] = &&op_VECTOR_ANN_SEARCH;
        dispatch_table[OP_VECTOR_RERANK_DOT] = &&op_VECTOR_RERANK_DOT;
        dispatch_inited = true;
    }

    bool fuel_active = (vm_state->query_context != nullptr && vm_state->query_context->fuel_enabled);

    #define DISPATCH() \
        do { \
            if (vm_state->pc >= instruction_count) goto op_OUT_OF_BOUNDS; \
            if (fuel_active) { \
                if (vm_state->query_context->fuel == 0) goto op_GAS_EXHAUSTED; \
                vm_state->query_context->fuel--; \
            } \
            uint8_t op = bytecode[vm_state->pc].opcode; \
            goto *dispatch_table[op]; \
        } while(0)

    // Start execution
    DISPATCH();
#else
    bool fuel_active = (vm_state->query_context != nullptr && vm_state->query_context->fuel_enabled);

    #define DISPATCH() goto dispatch_loop_start

dispatch_loop_start:
    if (vm_state->pc >= instruction_count) goto op_OUT_OF_BOUNDS;
    if (fuel_active) {
        if (vm_state->query_context->fuel == 0) goto op_GAS_EXHAUSTED;
        vm_state->query_context->fuel--;
    }
    switch (bytecode[vm_state->pc].opcode) {
        case OP_INIT_MOCK_NODE_ATTR: goto op_INIT_MOCK_NODE_ATTR;
        case OP_INIT_MOCK_EDGE_ATTR: goto op_INIT_MOCK_EDGE_ATTR;
        case OP_HALT: goto op_HALT;
        case OP_NOP: goto op_NOP;
        case OP_INIT_INPUT_NODE: goto op_INIT_INPUT_NODE;
        case OP_INIT_INPUT_SET: goto op_INIT_INPUT_SET;
        case OP_LOAD_CONST_INT: goto op_LOAD_CONST_INT;
        case OP_MAP_KEYS_TO_DENSE: goto op_MAP_KEYS_TO_DENSE;
        case OP_LOAD_CONST_FLOAT: goto op_LOAD_CONST_FLOAT;
        case OP_LOAD_CONST_STR_PREFIX: goto op_LOAD_CONST_STR_PREFIX;
        case OP_LOAD_INLINE_ARRAY: goto op_LOAD_INLINE_ARRAY;
        case OP_INIT_MOCK_GRAPH: goto op_INIT_MOCK_GRAPH;
        case OP_LOAD_INLINE_SET: goto op_LOAD_INLINE_SET;
        case OP_CSR_WALK_2HOP: goto op_CSR_WALK_2HOP;
        case OP_CSR_WALK_STATE: goto op_CSR_WALK_STATE;
        case OP_PROJECT_STATE: goto op_PROJECT_STATE;
        case OP_CSR_WALK: goto op_CSR_WALK;
        case OP_CSR_WALK_FILTERED: goto op_CSR_WALK_FILTERED;
        case OP_CSR_DEGREE: goto op_CSR_DEGREE;
        case OP_CSR_WALK_PREDICATE: goto op_CSR_WALK_PREDICATE;
        case OP_NODE_FILTER: goto op_NODE_FILTER;
        case OP_NODE_FILTER_STR_PREFIX: goto op_NODE_FILTER_STR_PREFIX;
        case OP_CSR_WALK_REDUCE_SUM: goto op_CSR_WALK_REDUCE_SUM;
        case OP_CSR_WALK_REDUCE: goto op_CSR_WALK_REDUCE;
        case OP_CSC_WALK: goto op_CSC_WALK;
        case OP_HAS_CSR: goto op_HAS_CSR;
        case OP_HAS_CSC: goto op_HAS_CSC;
        case OP_HAS_COO: goto op_HAS_COO;
        case OP_HAS_KEY_CATALOG: goto op_HAS_KEY_CATALOG;
        case OP_ADAPTIVE_WALK: goto op_ADAPTIVE_WALK;
        case OP_CREATE_SCRATCH_INDEX: goto op_CREATE_SCRATCH_INDEX;
        case OP_DROP_SCRATCH_INDEX: goto op_DROP_SCRATCH_INDEX;
        case OP_VEC_CMP_EQ: goto op_VEC_CMP_EQ;
        case OP_VEC_CMP_GT: goto op_VEC_CMP_GT;
        case OP_VEC_CMP_LT: goto op_VEC_CMP_LT;
        case OP_VEC_CMP_BETWEEN: goto op_VEC_CMP_BETWEEN;
        case OP_MASK_AND: goto op_MASK_AND;
        case OP_MASK_OR: goto op_MASK_OR;
        case OP_MASK_NOT: goto op_MASK_NOT;
        case OP_VEC_BLEND: goto op_VEC_BLEND;
        case OP_ASSERT_FINITE: goto op_ASSERT_FINITE;
        case OP_VEC_MATH_UNARY: goto op_VEC_MATH_UNARY;
        case OP_VEC_MATH_BINARY: goto op_VEC_MATH_BINARY;
        case OP_VEC_MATH_TERNARY: goto op_VEC_MATH_TERNARY;
        case OP_COALESCE: goto op_COALESCE;
        case OP_EXTRACT_VALIDITY: goto op_EXTRACT_VALIDITY;
        case OP_SET_UNION: goto op_SET_UNION;
        case OP_SET_INTERSECT: goto op_SET_INTERSECT;
        case OP_SET_DIFFERENCE: goto op_SET_DIFFERENCE;
        case OP_SET_CARDINALITY: goto op_SET_CARDINALITY;
        case OP_VECTOR_MUL_ATTR: goto op_VECTOR_MUL_ATTR;
        case OP_VECTOR_REDUCE_SUM: goto op_VECTOR_REDUCE_SUM;
        case OP_VECTOR_DIV: goto op_VECTOR_DIV;
        case OP_VECTOR_STR_CONCAT: goto op_VECTOR_STR_CONCAT;
        case OP_FLOAT_VECTOR_SCALE: goto op_FLOAT_VECTOR_SCALE;
        case OP_L1_NORM_DIFF: goto op_L1_NORM_DIFF;
        case OP_VECTOR_TIME_VALID_AT: goto op_VECTOR_TIME_VALID_AT;
        case OP_CC_AFFOREST: goto op_CC_AFFOREST;
        case OP_MXV: goto op_MXV;
        case OP_VXM: goto op_VXM;
        case OP_EWISE_ADD: goto op_EWISE_ADD;
        case OP_EWISE_MULT: goto op_EWISE_MULT;
        case OP_REDUCE: goto op_REDUCE;
        case OP_JMP: goto op_JMP;
        case OP_JZ: goto op_JZ;
        case OP_JNZ: goto op_JNZ;
        case OP_LOOP_DECR: goto op_LOOP_DECR;
        case OP_STABLE_CHECK: goto op_STABLE_CHECK;
        case OP_CALL: goto op_CALL;
        case OP_RET: goto op_RET;
        case OP_THROW: goto op_THROW;
        case OP_ASSERT: goto op_ASSERT;
        case OP_TRAP: goto op_TRAP;
        case OP_ENTER_FRAME: goto op_ENTER_FRAME;
        case OP_LEAVE_FRAME: goto op_LEAVE_FRAME;
        case OP_SAMPLE_NEIGHBORS: goto op_SAMPLE_NEIGHBORS;
        case OP_RANDOM_WALK: goto op_RANDOM_WALK;
        case OP_SCATTER_GATHER: goto op_SCATTER_GATHER;
        case OP_REBAC_CHECK: goto op_REBAC_CHECK;
        case OP_ROARING_BITMAP_OR: goto op_ROARING_BITMAP_OR;
        case OP_ROARING_BITMAP_AND: goto op_ROARING_BITMAP_AND;
        case OP_ROARING_BITMAP_AND_NOT: goto op_ROARING_BITMAP_AND_NOT;
        case OP_SPARSE_MATVEC: goto op_SPARSE_MATVEC;
        case OP_LOUVAIN_MODULARITY: goto op_LOUVAIN_MODULARITY;
        case OP_KCORE_DECOMPOSITION: goto op_KCORE_DECOMPOSITION;
        case OP_MOTIF_MATCH_3: goto op_MOTIF_MATCH_3;
        case OP_GRAPH_ISOMORPHISM: goto op_GRAPH_ISOMORPHISM;
        case OP_ISLAND_DETECT: goto op_ISLAND_DETECT;
        case OP_READ_EDGE_WEIGHT: goto op_READ_EDGE_WEIGHT;
        case OP_CC_HOOK_COMPRESS: goto op_CC_HOOK_COMPRESS;
        case OP_TC_SWEEP_BATCH: goto op_TC_SWEEP_BATCH;
        case OP_BRANDES_FORWARD: goto op_BRANDES_FORWARD;
        case OP_BRANDES_BACKWARD: goto op_BRANDES_BACKWARD;
        case OP_DELTA_STEP_RELAX: goto op_DELTA_STEP_RELAX;
        case OP_MOV: goto op_MOV;
        case OP_CLEAR_REG: goto op_CLEAR_REG;
        case OP_LOAD_INDIRECT: goto op_LOAD_INDIRECT;
        case OP_ALLOC_SCRATCH: goto op_ALLOC_SCRATCH;
        case OP_ASSERT_SCRATCH_BYTES: goto op_ASSERT_SCRATCH_BYTES;
        case OP_SET_MAX_DOP: goto op_SET_MAX_DOP;
        case OP_LOAD_COLUMN_VECTOR: goto op_LOAD_COLUMN_VECTOR;
        case OP_GATHER_NODE_ATTR: goto op_GATHER_NODE_ATTR;
        case OP_GATHER_EDGE_ATTR: goto op_GATHER_EDGE_ATTR;
        case OP_BRIN_ZONE_SKIP: goto op_BRIN_ZONE_SKIP;
        case OP_CSR_WALK_DIRECT_STORE: goto op_CSR_WALK_DIRECT_STORE;
        case OP_CSR_WALK_DENSE_STREAM: goto op_CSR_WALK_DENSE_STREAM;
        case OP_COO_WALK: goto op_COO_WALK;
        case OP_CSC_WALK_DIRECT_STORE: goto op_CSC_WALK_DIRECT_STORE;
        case OP_FIXPOINT_KLEENE_STAR: goto op_FIXPOINT_KLEENE_STAR;
        case OP_SWAP_REG: goto op_SWAP_REG;
        case OP_FRONTIER_DIFF: goto op_FRONTIER_DIFF;
        case OP_COO_WALK_FILTERED: goto op_COO_WALK_FILTERED;
        case OP_COO_WALK_REDUCE: goto op_COO_WALK_REDUCE;
        case OP_COO_WALK_DIRECT_STORE: goto op_COO_WALK_DIRECT_STORE;
        case OP_DENSE_WALK: goto op_DENSE_WALK;
        case OP_DENSE_WALK_BITMATRIX: goto op_DENSE_WALK_BITMATRIX;
        case OP_COLLECT_BITSET: goto op_COLLECT_BITSET;
        case OP_COLLECT_ARRAY: goto op_COLLECT_ARRAY;
        case OP_MAP_DENSE_TO_KEYS: goto op_MAP_DENSE_TO_KEYS;
        case OP_COLLECT_VALUE_MAP: goto op_COLLECT_VALUE_MAP;
        case OP_DENSE_WALK_REDUCE: goto op_DENSE_WALK_REDUCE;
        case OP_DENSE_WALK_DIRECT_STORE: goto op_DENSE_WALK_DIRECT_STORE;
        case OP_CSR_WALK_STREAM: goto op_CSR_WALK_STREAM;
        case OP_CSC_WALK_STREAM: goto op_CSC_WALK_STREAM;
        case OP_COO_WALK_STREAM: goto op_COO_WALK_STREAM;
        case OP_STREAM_FUNC_BEGIN: case OP_STREAM_FUNC_END:
        case OP_STREAM_LOAD_SRC: case OP_STREAM_LOAD_TGT: case OP_STREAM_LOAD_EDGE:
        case OP_STREAM_MATH_ADD: case OP_STREAM_MATH_SUB: case OP_STREAM_MATH_MUL:
        case OP_STREAM_MATH_DIV: case OP_STREAM_MATH_MOD: case OP_STREAM_MATH_UNARY:
        case OP_STREAM_CMP_EQ: case OP_STREAM_CMP_NEQ: case OP_STREAM_CMP_GT:
        case OP_STREAM_CMP_LT: case OP_STREAM_LOGIC_AND: case OP_STREAM_LOGIC_OR:
        case OP_STREAM_LOGIC_NOT: case OP_STREAM_SELECT: case OP_STREAM_FILTER:
        case OP_STREAM_YIELD: case OP_STREAM_SCATTER_REDUCE: case OP_STREAM_REDUCE:
        case OP_STREAM_REDUCE_ARGMIN: case OP_STREAM_REDUCE_ARGMAX:
        case OP_STREAM_LOAD_SRC_ID: case OP_STREAM_LOAD_TGT_ID:
        case OP_STREAM_LOAD_EDGE_ID: case OP_STREAM_LOAD_CONST:
            goto op_NOP;
        case OP_LANE_INIT_SEEDS: goto op_LANE_INIT_SEEDS;
        case OP_LANE_INIT_GROUPS: goto op_LANE_INIT_GROUPS;
        case OP_LANE_SET_EDGE_MASK: goto op_LANE_SET_EDGE_MASK;
        case OP_LANE_CSR_WALK: goto op_LANE_CSR_WALK;
        case OP_LANE_CSC_WALK: goto op_LANE_CSC_WALK;
        case OP_LANE_PRUNE_MASK: goto op_LANE_PRUNE_MASK;
        case OP_LANE_FILTER_QUORUM: goto op_LANE_FILTER_QUORUM;
        case OP_LANE_REDUCE_AND: goto op_LANE_REDUCE_AND;
        case OP_LANE_REDUCE_ATTR: goto op_LANE_REDUCE_ATTR;
        case OP_LANE_SET_COVER: goto op_LANE_SET_COVER;
        case OP_VECTOR_ANN_SEARCH: goto op_VECTOR_ANN_SEARCH;
        case OP_VECTOR_RERANK_DOT: goto op_VECTOR_RERANK_DOT;
        case OP_RESERVED_0D: case OP_RESERVED_28: case OP_RESERVED_29:
        case OP_RESERVED_2B: case OP_RESERVED_2C: case OP_RESERVED_3E:
        case OP_RESERVED_3F: case OP_RESERVED_4C: case OP_RESERVED_4D:
        case OP_RESERVED_4E: case OP_RESERVED_4F: case OP_RESERVED_59:
        case OP_RESERVED_5D: case OP_RESERVED_5E: case OP_RESERVED_5F:
        case OP_RESERVED_6D: case OP_RESERVED_6E: case OP_RESERVED_6F:
        case OP_RESERVED_76: case OP_RESERVED_77: case OP_RESERVED_78:
        case OP_RESERVED_79: case OP_RESERVED_7A: case OP_RESERVED_7B:
        case OP_RESERVED_7C: case OP_RESERVED_7D: case OP_RESERVED_7E:
        case OP_RESERVED_7F:
            goto op_RESERVED;
        default:
            goto op_INVALID;
    }
#endif


#include "opcodes/op_00_halt.inc"
#include "opcodes/op_01_nop.inc"
#include "opcodes/op_02_init_input_node.inc"
#include "opcodes/op_03_init_input_set.inc"
#include "opcodes/op_04_load_const_int.inc"
#include "opcodes/op_05_map_keys_to_dense.inc"
#include "opcodes/op_06_load_const_float.inc"
#include "opcodes/op_07_load_const_str_prefix.inc"
#include "opcodes/op_08_load_inline_array.inc"
#include "opcodes/op_09_init_mock_graph.inc"
#include "opcodes/op_0a_load_inline_set.inc"
#include "opcodes/op_0b_init_mock_node_attr.inc"
#include "opcodes/op_0c_init_mock_edge_attr.inc"
#include "opcodes/op_0e_csr_walk_2hop.inc"
#include "opcodes/op_0f_csr_walk_state.inc"
#include "opcodes/op_10_csr_walk.inc"
#include "opcodes/op_11_csr_walk_filtered.inc"
#include "opcodes/op_12_csr_degree.inc"
#include "opcodes/op_13_csr_walk_predicate.inc"
#include "opcodes/op_14_node_filter.inc"
#include "opcodes/op_15_node_filter_str_prefix.inc"
#include "opcodes/op_16_csr_walk_reduce_sum.inc"
#include "opcodes/op_17_csr_walk_reduce.inc"
#include "opcodes/op_18_csc_walk.inc"
#include "opcodes/op_19_has_csr.inc"
#include "opcodes/op_1a_has_csc.inc"
#include "opcodes/op_1b_has_coo.inc"
#include "opcodes/op_1c_has_key_catalog.inc"
#include "opcodes/op_1d_adaptive_walk.inc"
#include "opcodes/op_1e_create_scratch_index.inc"
#include "opcodes/op_1f_drop_scratch_index.inc"
#include "opcodes/op_20_vec_cmp_eq.inc"
#include "opcodes/op_21_vec_cmp_gt.inc"
#include "opcodes/op_22_vec_cmp_lt.inc"
#include "opcodes/op_23_vec_cmp_between.inc"
#include "opcodes/op_24_mask_and.inc"
#include "opcodes/op_25_mask_or.inc"
#include "opcodes/op_26_mask_not.inc"
#include "opcodes/op_27_vec_blend.inc"
#include "opcodes/op_2a_assert_finite.inc"
#include "opcodes/op_2d_vec_math_unary.inc"
#include "opcodes/op_2e_vec_math_binary.inc"
#include "opcodes/op_2f_vec_math_ternary.inc"
#include "opcodes/op_30_set_union.inc"
#include "opcodes/op_31_set_intersect.inc"
#include "opcodes/op_32_set_difference.inc"
#include "opcodes/op_33_set_cardinality.inc"
#include "opcodes/op_34_vector_mul_attr.inc"
#include "opcodes/op_35_vector_reduce_sum.inc"
#include "opcodes/op_36_vector_div.inc"
#include "opcodes/op_37_vector_str_concat.inc"
#include "opcodes/op_38_float_vector_scale.inc"
#include "opcodes/op_39_l1_norm_diff.inc"
#include "opcodes/op_3a_project_state.inc"
#include "opcodes/op_3b_coalesce.inc"
#include "opcodes/op_3c_extract_validity.inc"
#include "opcodes/op_3d_vector_time_valid_at.inc"
#include "opcodes/op_40_cc_afforest.inc"
#include "opcodes/op_41_mxv.inc"
#include "opcodes/op_42_vxm.inc"
#include "opcodes/op_43_ewise_add.inc"
#include "opcodes/op_44_ewise_mult.inc"
#include "opcodes/op_45_reduce.inc"
#include "opcodes/op_46_cc_hook_compress.inc"
#include "opcodes/op_47_tc_sweep_batch.inc"
#include "opcodes/op_48_brandes_forward.inc"
#include "opcodes/op_49_brandes_backward.inc"
#include "opcodes/op_4a_delta_step_relax.inc"
#include "opcodes/op_4b_read_edge_weight.inc"
#include "opcodes/op_50_jmp.inc"
#include "opcodes/op_51_jz.inc"
#include "opcodes/op_52_jnz.inc"
#include "opcodes/op_53_loop_decr.inc"
#include "opcodes/op_54_stable_check.inc"
#include "opcodes/op_55_call.inc"
#include "opcodes/op_56_ret.inc"
#include "opcodes/op_57_enter_frame.inc"
#include "opcodes/op_58_leave_frame.inc"
#include "opcodes/op_5a_throw.inc"
#include "opcodes/op_5b_assert.inc"
#include "opcodes/op_5c_trap.inc"
#include "opcodes/op_60_sample_neighbors.inc"
#include "opcodes/op_61_random_walk.inc"
#include "opcodes/op_62_scatter_gather.inc"
#include "opcodes/op_63_rebac_check.inc"
#include "opcodes/op_64_roaring_bitmap_and.inc"
#include "opcodes/op_65_island_detect.inc"
#include "opcodes/op_66_sparse_matvec.inc"
#include "opcodes/op_67_louvain_modularity.inc"
#include "opcodes/op_68_kcore_decomposition.inc"
#include "opcodes/op_69_motif_match_3.inc"
#include "opcodes/op_6a_graph_isomorphism.inc"
#include "opcodes/op_6b_roaring_bitmap_or.inc"
#include "opcodes/op_6c_roaring_bitmap_and_not.inc"
#include "opcodes/op_70_mov.inc"
#include "opcodes/op_71_clear_reg.inc"
#include "opcodes/op_72_load_indirect.inc"
#include "opcodes/op_73_alloc_scratch.inc"
#include "opcodes/op_74_assert_scratch_bytes.inc"
#include "opcodes/op_75_set_max_dop.inc"
#include "opcodes/op_80_load_column_vector.inc"
#include "opcodes/op_81_gather_node_attr.inc"
#include "opcodes/op_82_gather_edge_attr.inc"
#include "opcodes/op_83_brin_zone_skip.inc"
#include "opcodes/op_84_csr_walk_direct_store.inc"
#include "opcodes/op_85_csr_walk_dense_stream.inc"
#include "opcodes/op_86_coo_walk.inc"
#include "opcodes/op_87_csc_walk_direct_store.inc"
#include "opcodes/op_88_fixpoint_kleene_star.inc"
#include "opcodes/op_89_swap_reg.inc"
#include "opcodes/op_8a_frontier_diff.inc"
#include "opcodes/op_8b_coo_walk_filtered.inc"
#include "opcodes/op_8c_coo_walk_reduce.inc"
#include "opcodes/op_8d_coo_walk_direct_store.inc"
#include "opcodes/op_8e_dense_walk.inc"
#include "opcodes/op_8f_dense_walk_bitmatrix.inc"
#include "opcodes/op_90_collect_bitset.inc"
#include "opcodes/op_91_collect_array.inc"
#include "opcodes/op_92_map_dense_to_keys.inc"
#include "opcodes/op_93_collect_value_map.inc"
#include "opcodes/op_94_dense_walk_reduce.inc"
#include "opcodes/op_95_dense_walk_direct_store.inc"
#include "opcodes/op_a0_coo_walk_stream.inc"
#include "opcodes/op_a9_csr_walk_stream.inc"
#include "opcodes/op_aa_csc_walk_stream.inc"
#include "opcodes/op_c0_lane_init_seeds.inc"
#include "opcodes/op_c1_lane_init_groups.inc"
#include "opcodes/op_c2_lane_set_edge_mask.inc"
#include "opcodes/op_c3_lane_csr_walk.inc"
#include "opcodes/op_c4_lane_csc_walk.inc"
#include "opcodes/op_c5_lane_prune_mask.inc"
#include "opcodes/op_c6_lane_filter_quorum.inc"
#include "opcodes/op_c7_lane_reduce_and.inc"
#include "opcodes/op_c8_lane_reduce_attr.inc"
#include "opcodes/op_c9_lane_set_cover.inc"
#include "opcodes/op_ca_vector_ann_search.inc"
#include "opcodes/op_cb_vector_rerank_dot.inc"

op_INVALID:
    return IMPULSE_VM_ERR_INVALID_OPCODE;

op_RESERVED:
    return IMPULSE_VM_ERR_RESERVED_OPCODE;

op_OUT_OF_BOUNDS:
    return IMPULSE_VM_ERR_OUT_OF_BOUNDS;

op_GAS_EXHAUSTED:
    return IMPULSE_VM_ERR_GAS_EXHAUSTED;
}

impulse_vm_status_t impulse_vm_validate(
    const impulse_instruction_t* bytecode,
    size_t instruction_count
) {
    if (!bytecode && instruction_count > 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    if (instruction_count == 0) return IMPULSE_VM_OK;

    uint8_t abstract_types[64] = {0};

    for (size_t pc = 0; pc < instruction_count; pc++) {
        const auto& inst = bytecode[pc];
        uint8_t opcode = inst.opcode;
        uint16_t dst = inst.dst_reg;
        uint16_t src = inst.payload & 0xFFFF;

        if (dst >= 64) return IMPULSE_VM_ERR_INVALID_REGISTER;

        switch (opcode) {
            case OP_NOP:
            case OP_HALT:
            case OP_JMP:
            case OP_JZ:
            case OP_JNZ:
            case OP_LOOP_DECR:
            case OP_STABLE_CHECK:
            case OP_CALL:
            case OP_RET:
            case OP_THROW:
            case OP_ASSERT:
            case OP_TRAP:
            case OP_ASSERT_FINITE:
            case OP_SET_MAX_DOP:
            case OP_ALLOC_SCRATCH:
            case OP_ASSERT_SCRATCH_BYTES:
            case OP_CREATE_SCRATCH_INDEX:
            case OP_DROP_SCRATCH_INDEX:
                break;

            case OP_VECTOR_TIME_VALID_AT:
                abstract_types[dst] = TYPE_FLOAT_VECTOR;
                break;

            case OP_INIT_INPUT_NODE:
                abstract_types[dst] = TYPE_NODE_ID;
                break;
            case OP_INIT_INPUT_SET:
            case OP_MAP_KEYS_TO_DENSE:
            case OP_ADAPTIVE_WALK:
            case OP_CSR_WALK:
            case OP_CSR_WALK_FILTERED:
            case OP_CSC_WALK:
            case OP_SET_UNION:
            case OP_SET_INTERSECT:
            case OP_SET_DIFFERENCE:
            case OP_VEC_CMP_EQ:
            case OP_VEC_CMP_GT:
            case OP_VEC_CMP_LT:
            case OP_VEC_CMP_BETWEEN:
            case OP_MASK_AND:
            case OP_MASK_OR:
            case OP_MASK_NOT:
            case OP_CSR_WALK_DIRECT_STORE:
            case OP_CSR_WALK_DENSE_STREAM:
            case OP_COO_WALK:
            case OP_CSC_WALK_DIRECT_STORE:
            case OP_FIXPOINT_KLEENE_STAR:
            case OP_FRONTIER_DIFF:
            case OP_COO_WALK_FILTERED:
            case OP_COO_WALK_DIRECT_STORE:
            case OP_DENSE_WALK:
            case OP_DENSE_WALK_BITMATRIX:
            case OP_DENSE_WALK_DIRECT_STORE:
            case OP_CSR_WALK_STREAM:
            case OP_CSC_WALK_STREAM:
            case OP_COO_WALK_STREAM:
                abstract_types[dst] = TYPE_BITSET_HANDLE;
                break;

            case OP_LOAD_CONST_INT:
            case OP_CSR_DEGREE:
            case OP_SET_CARDINALITY:
            case OP_BRIN_ZONE_SKIP:
                abstract_types[dst] = TYPE_INT64;
                break;

            case OP_LOAD_CONST_FLOAT:
            case OP_VECTOR_REDUCE_SUM:
            case OP_REDUCE: {
                uint16_t reduce_op = (inst.payload >> 16) & 0xFFFF;
                if (opcode == OP_REDUCE && (reduce_op == BINARY_OP_ARGMIN || reduce_op == BINARY_OP_ARGMAX)) {
                    abstract_types[dst] = TYPE_NODE_ID;
                } else {
                    abstract_types[dst] = TYPE_FLOAT;
                }
                break;
            }

            case OP_LOAD_INLINE_ARRAY:
            case OP_VEC_MATH_UNARY:
            case OP_VEC_MATH_BINARY:
            case OP_VEC_MATH_TERNARY:
            case OP_LOAD_COLUMN_VECTOR:
            case OP_GATHER_NODE_ATTR:
            case OP_GATHER_EDGE_ATTR:
            case OP_COO_WALK_REDUCE:
            case OP_DENSE_WALK_REDUCE:
            case OP_EWISE_ADD:
            case OP_EWISE_MULT:
            case OP_MXV:
            case OP_VXM:
                abstract_types[dst] = TYPE_FLOAT_VECTOR;
                break;

            case OP_MOV:
                if (src < 64) abstract_types[dst] = abstract_types[src];
                break;

            case OP_CLEAR_REG:
                abstract_types[dst] = TYPE_NULL;
                break;

            case OP_LANE_INIT_SEEDS:
            case OP_LANE_INIT_GROUPS:
            case OP_LANE_SET_EDGE_MASK:
            case OP_LANE_CSR_WALK:
            case OP_LANE_CSC_WALK:
            case OP_LANE_PRUNE_MASK:
                abstract_types[dst] = TYPE_LANE_FRONTIER;
                break;

            case OP_LANE_FILTER_QUORUM:
                abstract_types[dst] = TYPE_BITSET_HANDLE;
                break;

            case OP_LANE_REDUCE_AND:
                abstract_types[dst] = TYPE_INT64;
                break;

            case OP_LANE_REDUCE_ATTR: {
                uint16_t red_op = (inst.payload >> 16) & 0xFFFF;
                abstract_types[dst] = (red_op == BINARY_OP_ARGMIN || red_op == BINARY_OP_ARGMAX)
                    ? TYPE_UINT64_VECTOR
                    : TYPE_FLOAT_VECTOR;
                break;
            }

            case OP_LANE_SET_COVER:
                abstract_types[dst] = TYPE_NODE_VECTOR;
                break;

            case OP_VECTOR_ANN_SEARCH:
                abstract_types[dst] = ((inst.flags & 0x01) != 0)
                    ? TYPE_LANE_FRONTIER
                    : TYPE_BITSET_HANDLE;
                break;

            case OP_VECTOR_RERANK_DOT:
                abstract_types[dst] = TYPE_FLOAT_VECTOR;
                break;

            default:
                break;
        }
        if (inst.flags & 0x80) {
            if (pc + 1 >= instruction_count) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
            pc++;
        }
    }

    return IMPULSE_VM_OK;
}

size_t impulse_vm_get_required_buffer_size(const impulse_snapshot_t* snapshot, uint16_t domain_id) {
    (void)domain_id;
    if (!snapshot) return 0;
    uint64_t max_nodes = impulse_snapshot_max_node_count(snapshot);
    uint64_t bitset_capacity = std::max<uint64_t>(max_nodes, 65536ULL);
    return static_cast<size_t>((bitset_capacity + 63) / 64);
}

impulse_vm_status_t impulse_vm_execute_to_buffer(
    const impulse_instruction_t* bytecode,
    size_t instruction_count,
    impulse_vm_state_t* vm_state,
    uint64_t input_param,
    uint16_t target_domain_id,
    uint16_t result_reg,
    uint64_t* out_words,
    size_t out_words_capacity,
    size_t* out_words_written) {
    
    (void)target_domain_id;
    if (!out_words || !vm_state) return IMPULSE_VM_ERR_NULL_SNAPSHOT;
    size_t req_words = vm_state->query_context ? vm_state->query_context->words_per_bitset : 0;
    if (out_words_capacity < req_words) {
        return IMPULSE_VM_ERR_BUFFER_OVERFLOW;
    }

    impulse_vm_status_t st = impulse_vm_execute(bytecode, instruction_count, vm_state, input_param);
    if (st != IMPULSE_VM_OK) return st;
    
    if (result_reg < 64 && vm_state->query_context) {
        uint8_t rtype = vm_state->register_types[result_reg];
        if (rtype == TYPE_BITSET_HANDLE) {
            int h = static_cast<int>(vm_state->registers[result_reg]);
            if (h >= 0 && static_cast<size_t>(h) < VM_MAX_BITSET_HANDLES) {
                auto& bs = vm_state->query_context->bitsets[h];
                size_t count = std::min(vm_state->query_context->words_per_bitset, out_words_capacity);
                std::memcpy(out_words, bs.words, count * sizeof(uint64_t));
                if (out_words_written) *out_words_written = count;
            }
        } else if (rtype == TYPE_LANE_FRONTIER) {
            size_t h_lf = static_cast<size_t>(vm_state->registers[result_reg]);
            size_t count = std::min(vm_state->query_context->words_per_bitset, out_words_capacity);
            std::memset(out_words, 0, count * sizeof(uint64_t));
            if (h_lf < VM_MAX_LANE_FRONTIER_HANDLES && h_lf < vm_state->query_context->lane_frontiers_count) {
                const auto& lf = vm_state->query_context->lane_frontiers[h_lf];
                for (size_t w_idx = 0; w_idx < lf.active_words.size(); ++w_idx) {
                    uint64_t word = lf.active_words[w_idx];
                    while (word != 0ULL) {
                        uint32_t bit = static_cast<uint32_t>(std::countr_zero(word));
                        size_t u = (w_idx << 6) + bit;
                        word &= (word - 1ULL);
                        if (u >= lf.lanes.size()) break;
                        if ((lf.lanes[u] & lf.active_mask) != 0ULL && (u >> 6) < count) {
                            out_words[u >> 6] |= (1ULL << (u & 63));
                        }
                    }
                }
            }
            if (out_words_written) *out_words_written = count;
        } else if (rtype == TYPE_NODE_VECTOR || rtype == TYPE_UINT64_VECTOR) {
            size_t count = std::min(vm_state->query_context->words_per_bitset, out_words_capacity);
            std::memset(out_words, 0, count * sizeof(uint64_t));
            const uint64_t* ptr = nullptr;
            size_t len = 0;
            if (get_node_vector_slice(vm_state->query_context, vm_state->registers[result_reg], &ptr, &len) && ptr) {
                for (size_t i = 0; i < len; ++i) {
                    uint64_t u = ptr[i];
                    if (u != ~0ULL && (u >> 6) < count) {
                        out_words[u >> 6] |= (1ULL << (u & 63));
                    }
                }
            }
            if (out_words_written) *out_words_written = count;
        }
    }
    return IMPULSE_VM_OK;
}

} // extern "C"

#if defined(__clang__)
  #pragma clang diagnostic pop
#elif defined(__GNUC__)
  #pragma GCC diagnostic pop
#endif
