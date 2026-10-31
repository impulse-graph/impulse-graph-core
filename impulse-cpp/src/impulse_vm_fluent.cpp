#include "impulse_vm_fluent.hpp"
#include "impulse_assert.h"
#include "impulse_compiler.hpp"
#include "impulse_cypher.hpp"
#include "impulse_datalog.hpp"
#include "impulse_impk.hpp"
#include "impulse_cel.h"
#include "impulse_sexpr.hpp"

#include <stdexcept>
#include <cstring>
#include <memory>
#include <string>
#include <vector>
#include <array>

namespace impulse::vm {

QueryResult CompiledQuery::executeWithContext(
    impulse_vm_context_t* ctx,
    impulse_vm_state_t* state,
    uint64_t input_param
) const {
    if (!state) {
        return QueryResult{IMPULSE_VM_ERR_NULL_SNAPSHOT, 0, TYPE_NULL, 0};
    }
    IMPULSE_ASSERT(state != nullptr);
    IMPULSE_ASSERT(result_register_ < 64);
    state->query_context = ctx;

    impulse_vm_status_t status = impulse_vm_execute(
        instructions_.data(), instructions_.size(), state, input_param
    );

    QueryResult result;
    result.status = status;
    result.result_register = result_register_;
    if (status == IMPULSE_VM_OK) {
        result.result_type = static_cast<impulse_register_type_t>(state->register_types[result_register_]);
        result.raw_value = state->registers[result_register_];
    }
    return result;
}

QueryResult CompiledQuery::execute(
    const impulse_snapshot_t* snapshot,
    uint64_t input_param,
    impulse_vm_context_t** out_context
) const {
    impulse_vm_context_t* ctx = impulse_vm_context_create(snapshot);
    if (!ctx) {
        return QueryResult{IMPULSE_VM_ERR_NULL_SNAPSHOT, 0, TYPE_NULL, 0};
    }

    impulse_vm_state_t state{};
    state.query_context = ctx;

    QueryResult result = executeWithContext(ctx, &state, input_param);

    if (out_context) {
        *out_context = ctx;
    } else {
        impulse_vm_context_destroy(ctx);
    }

    return result;
}

QueryBuilder::QueryBuilder() : current_reg_(0), next_alloc_reg_(1) {}

QueryBuilder::QueryBuilder(uint16_t start_register)
    : current_reg_(start_register), next_alloc_reg_(start_register + 1) {}

uint16_t QueryBuilder::allocateRegister() {
    uint16_t reg = next_alloc_reg_++;
    IMPULSE_ASSERT(reg < 64);
    if (reg >= 64) {
        throw std::length_error("QueryBuilder exceeded maximum available VM registers (64)");
    }
    return reg;
}

void QueryBuilder::emit(uint8_t opcode, uint8_t flags, uint16_t dst_reg, uint32_t payload) {
    IMPULSE_ASSERT(dst_reg < 64);
    impulse_instruction_t inst{};
    inst.opcode = opcode;
    inst.flags = flags;
    inst.dst_reg = dst_reg;
    inst.payload = payload;
    instructions_.push_back(inst);
}

// --- Inputs & Constants ---

QueryBuilder& QueryBuilder::inputNode(uint16_t dst_reg) {
    current_reg_ = dst_reg;
    emit(OP_INIT_INPUT_NODE, 0, dst_reg, 0);
    return *this;
}

QueryBuilder& QueryBuilder::inputSet(uint16_t dst_reg) {
    current_reg_ = dst_reg;
    emit(OP_INIT_INPUT_SET, 0, dst_reg, 0);
    return *this;
}

QueryBuilder& QueryBuilder::loadConstInt(int64_t value, uint16_t dst_reg) {
    current_reg_ = dst_reg;
    emit(OP_LOAD_CONST_INT, 0, dst_reg, static_cast<uint32_t>(value));
    return *this;
}

QueryBuilder& QueryBuilder::loadConstFloat(float value, uint16_t dst_reg) {
    current_reg_ = dst_reg;
    uint32_t val32{0};
    static_assert(sizeof(float) == sizeof(uint32_t));
    std::memcpy(&val32, &value, sizeof(float));
    emit(OP_LOAD_CONST_FLOAT, 0, dst_reg, val32);
    return *this;
}

QueryBuilder& QueryBuilder::loadConstStrPrefix(const char* prefix, uint16_t dst_reg) {
    current_reg_ = dst_reg;
    uint32_t packed{0};
    if (prefix) {
        size_t len = std::strlen(prefix);
        if (len > 4) len = 4;
        std::memcpy(&packed, prefix, len);
    }
    emit(OP_LOAD_CONST_STR_PREFIX, 0, dst_reg, packed);
    return *this;
}

QueryBuilder& QueryBuilder::loadKeys(const char** keys, size_t count, uint16_t dst_reg) {
    (void)keys;
    current_reg_ = dst_reg;
    emit(OP_MAP_KEYS_TO_DENSE, 0, dst_reg, static_cast<uint32_t>(count));
    return *this;
}

// --- Graph Walks & Filters ---

QueryBuilder& QueryBuilder::walkEdge(uint16_t relation_id, uint8_t flags) {
    uint16_t src_reg = current_reg_;
    uint32_t payload = (static_cast<uint32_t>(relation_id) << 16) | (src_reg & 0xFFFF);
    emit(OP_CSR_WALK, flags, current_reg_, payload);
    return *this;
}

QueryBuilder& QueryBuilder::walkEdgeFiltered(uint16_t relation_id, uint32_t filter_id) {
    uint16_t src_reg = current_reg_;
    uint32_t payload = (static_cast<uint32_t>(relation_id) << 16) | ((filter_id & 0xFF) << 8) | (src_reg & 0xFF);
    emit(OP_CSR_WALK_FILTERED, 0, current_reg_, payload);
    return *this;
}

QueryBuilder& QueryBuilder::walkEdgePredicate(uint16_t relation_id, uint32_t filter_id) {
    uint16_t src_reg = current_reg_;
    uint32_t payload = (static_cast<uint32_t>(relation_id) << 16) | ((filter_id & 0xFF) << 8) | (src_reg & 0xFF);
    emit(OP_CSR_WALK_PREDICATE, 0, current_reg_, payload);
    return *this;
}

QueryBuilder& QueryBuilder::walkDegree(uint16_t relation_id) {
    uint16_t src_reg = current_reg_;
    uint32_t payload = (static_cast<uint32_t>(relation_id) << 16) | (src_reg & 0xFFFF);
    emit(OP_CSR_DEGREE, 0, current_reg_, payload);
    return *this;
}

QueryBuilder& QueryBuilder::walkReduceSum(uint16_t relation_id, uint16_t val_reg) {
    uint16_t src_reg = current_reg_;
    uint32_t payload = (static_cast<uint32_t>(relation_id) << 16) | (src_reg & 0xFF) | ((val_reg & 0xFF) << 8);
    emit(OP_CSR_WALK_REDUCE_SUM, 0, current_reg_, payload);
    return *this;
}

QueryBuilder& QueryBuilder::walkCsc(uint16_t relation_id) {
    uint16_t src_reg = current_reg_;
    uint32_t payload = (static_cast<uint32_t>(relation_id) << 16) | (src_reg & 0xFFFF);
    emit(OP_CSC_WALK, 0, current_reg_, payload);
    return *this;
}

QueryBuilder& QueryBuilder::filterNode(uint32_t filter_id) {
    uint16_t src_reg = current_reg_;
    uint8_t val_reg = 0;
    uint8_t attr_id = static_cast<uint8_t>(filter_id & 0xFF);
    uint8_t rel_id = static_cast<uint8_t>((filter_id >> 8) & 0xFF);
    uint32_t payload = (static_cast<uint32_t>(rel_id) << 24) |
                       (static_cast<uint32_t>(attr_id) << 16) |
                       (static_cast<uint32_t>(val_reg) << 8) |
                       (src_reg & 0xFF);
    emit(OP_NODE_FILTER, 0, current_reg_, payload);
    return *this;
}

QueryBuilder& QueryBuilder::filterNodeStrPrefix(const char* prefix) {
    uint32_t packed{0};
    if (prefix) {
        size_t len = std::strlen(prefix);
        if (len > 4) len = 4;
        std::memcpy(&packed, prefix, len);
    }
    emit(OP_NODE_FILTER_STR_PREFIX, 0, current_reg_, packed);
    return *this;
}


QueryBuilder& QueryBuilder::project(const std::string& expression) {
    uint32_t proj_hash = static_cast<uint32_t>(std::hash<std::string>{}(expression));
    emit(OP_PROJECT_STATE, 0, current_reg_, proj_hash & 0xFFFF);
    return *this;
}

QueryBuilder& QueryBuilder::filterCel(const std::string& expression) {
    impulse::cel::Parser parser(expression);
    auto ast = parser.parse();
    if (!ast) {
        throw std::invalid_argument("Failed to parse CEL expression: " + expression);
    }
    auto optimized = impulse::cel::AstOptimizer::optimize(ast);

    if (optimized->kind == impulse::cel::AstKind::LITERAL_BOOL) {
        if (!optimized->bool_val) {
            return clearReg(current_reg_);
        }
        return *this;
    }

    if (optimized->kind == impulse::cel::AstKind::FUNCTION_CALL &&
        (optimized->text == "startsWith" || optimized->text == "str_prefix") &&
        optimized->children.size() >= 2 &&
        optimized->children[1]->kind == impulse::cel::AstKind::LITERAL_STRING) {
        return filterNodeStrPrefix(optimized->children[1]->str_val.c_str());
    }

    uint32_t filter_hash = static_cast<uint32_t>(std::hash<std::string>{}(expression));
    return filterNode(filter_hash & 0xFFFF);
}

// --- Set & Vector Operations ---

QueryBuilder& QueryBuilder::unionWith(uint16_t src_reg) {
    emit(OP_SET_UNION, 0, current_reg_, static_cast<uint32_t>(current_reg_ | (src_reg << 16)));
    return *this;
}

QueryBuilder& QueryBuilder::intersectWith(uint16_t src_reg) {
    emit(OP_SET_INTERSECT, 0, current_reg_, static_cast<uint32_t>(current_reg_ | (src_reg << 16)));
    return *this;
}

QueryBuilder& QueryBuilder::differenceWith(uint16_t src_reg) {
    emit(OP_SET_DIFFERENCE, 0, current_reg_, static_cast<uint32_t>(current_reg_ | (src_reg << 16)));
    return *this;
}

QueryBuilder& QueryBuilder::cardinality() {
    emit(OP_SET_CARDINALITY, 0, current_reg_, current_reg_);
    return *this;
}

QueryBuilder& QueryBuilder::vectorMulAttr(uint16_t attr_reg) {
    emit(OP_VECTOR_MUL_ATTR, 0, current_reg_, attr_reg);
    return *this;
}

QueryBuilder& QueryBuilder::vectorReduceSum() {
    emit(OP_VECTOR_REDUCE_SUM, 0, current_reg_, current_reg_);
    return *this;
}

QueryBuilder& QueryBuilder::vectorDiv(uint16_t denom_reg) {
    emit(OP_VECTOR_DIV, 0, current_reg_, denom_reg);
    return *this;
}

QueryBuilder& QueryBuilder::l1NormDiff(uint16_t other_reg) {
    emit(OP_L1_NORM_DIFF, 0, current_reg_, other_reg);
    return *this;
}

// --- GraphBLAS & Linear Algebra ---

QueryBuilder& QueryBuilder::matrixVectorMul(uint16_t matrix_reg, uint8_t semiring_id) {
    uint32_t payload = (static_cast<uint32_t>(semiring_id) << 16) | (matrix_reg & 0xFFFF);
    emit(OP_MXV, 0, current_reg_, payload);
    return *this;
}

QueryBuilder& QueryBuilder::vectorMatrixMul(uint16_t matrix_reg, uint8_t semiring_id) {
    uint32_t payload = (static_cast<uint32_t>(semiring_id) << 16) | (matrix_reg & 0xFFFF);
    emit(OP_VXM, 0, current_reg_, payload);
    return *this;
}

QueryBuilder& QueryBuilder::ewiseAdd(uint16_t other_reg, uint8_t binary_op) {
    uint32_t payload = (static_cast<uint32_t>(binary_op) << 16) | (other_reg & 0xFFFF);
    emit(OP_EWISE_ADD, 0, current_reg_, payload);
    return *this;
}

QueryBuilder& QueryBuilder::ewiseMult(uint16_t other_reg, uint8_t binary_op) {
    uint32_t payload = (static_cast<uint32_t>(binary_op) << 16) | (other_reg & 0xFFFF);
    emit(OP_EWISE_MULT, 0, current_reg_, payload);
    return *this;
}

QueryBuilder& QueryBuilder::reduce(uint8_t binary_op) {
    emit(OP_REDUCE, 0, current_reg_, binary_op);
    return *this;
}

// --- Graph Analytics & Extended Domain ---

QueryBuilder& QueryBuilder::afforest() {
    emit(OP_CC_AFFOREST, 0, current_reg_, 0);
    return *this;
}

QueryBuilder& QueryBuilder::tcSweepBatch() {
    emit(OP_TC_SWEEP_BATCH, 0, current_reg_, 0);
    return *this;
}

QueryBuilder& QueryBuilder::brandesForward() {
    emit(OP_BRANDES_FORWARD, 0, current_reg_, 0);
    return *this;
}

QueryBuilder& QueryBuilder::brandesBackward() {
    emit(OP_BRANDES_BACKWARD, 0, current_reg_, 0);
    return *this;
}

QueryBuilder& QueryBuilder::deltaStepRelax(uint16_t weight_reg) {
    emit(OP_DELTA_STEP_RELAX, 0, current_reg_, weight_reg);
    return *this;
}

QueryBuilder& QueryBuilder::sampleNeighbors(uint16_t relation_id, int32_t k_samples, uint32_t seed) {
    uint32_t payload = (static_cast<uint32_t>(relation_id) << 16) | (static_cast<uint16_t>(k_samples) & 0xFFFF);
    emit(OP_SAMPLE_NEIGHBORS, 0, current_reg_, payload);
    (void)seed;
    return *this;
}

QueryBuilder& QueryBuilder::randomWalk(uint16_t relation_id, int32_t steps, uint32_t seed) {
    uint32_t payload = (static_cast<uint32_t>(relation_id) << 16) | (static_cast<uint16_t>(steps) & 0xFFFF);
    emit(OP_RANDOM_WALK, 0, current_reg_, payload);
    (void)seed;
    return *this;
}

QueryBuilder& QueryBuilder::scatterGather() {
    emit(OP_SCATTER_GATHER, 0, current_reg_, 0);
    return *this;
}

QueryBuilder& QueryBuilder::rebacCheck(uint32_t permission_id) {
    emit(OP_REBAC_CHECK, 0, current_reg_, permission_id);
    return *this;
}

QueryBuilder& QueryBuilder::roaringBitmapAnd(uint16_t other_reg) {
    emit(OP_ROARING_BITMAP_AND, 0, current_reg_, other_reg);
    return *this;
}

QueryBuilder& QueryBuilder::islandDetect(uint16_t secondary_reg) {
    emit(OP_ISLAND_DETECT, 0, current_reg_, secondary_reg);
    return *this;
}

QueryBuilder& QueryBuilder::sparseMatVec() {
    emit(OP_SPARSE_MATVEC, 0, current_reg_, 0);
    return *this;
}

QueryBuilder& QueryBuilder::louvainModularity() {
    emit(OP_LOUVAIN_MODULARITY, 0, current_reg_, 0);
    return *this;
}

QueryBuilder& QueryBuilder::kcoreDecomposition() {
    emit(OP_KCORE_DECOMPOSITION, 0, current_reg_, 0);
    return *this;
}

QueryBuilder& QueryBuilder::motifMatch3() {
    emit(OP_MOTIF_MATCH_3, 0, current_reg_, 0);
    return *this;
}

QueryBuilder& QueryBuilder::graphIsomorphism() {
    emit(OP_GRAPH_ISOMORPHISM, 0, current_reg_, 0);
    return *this;
}

// --- Register Control & Movement ---

QueryBuilder& QueryBuilder::mov(uint16_t dst_reg, uint16_t src_reg) {
    current_reg_ = dst_reg;
    emit(OP_MOV, 0, dst_reg, src_reg);
    return *this;
}

QueryBuilder& QueryBuilder::clearReg(uint16_t reg) {
    emit(OP_CLEAR_REG, 0, reg, 0);
    return *this;
}

QueryBuilder& QueryBuilder::loadIndirect(uint16_t dst_reg, uint16_t src_param, uint16_t index_reg, uint8_t flags) {
    current_reg_ = dst_reg;
    uint32_t payload = static_cast<uint32_t>(src_param) | (static_cast<uint32_t>(index_reg) << 16);
    emit(OP_LOAD_INDIRECT, flags, dst_reg, payload);
    return *this;
}

QueryBuilder& QueryBuilder::loadInlineArray(uint16_t dst_reg, uint16_t offset_bytes, uint16_t count) {
    current_reg_ = dst_reg;
    uint32_t payload = static_cast<uint32_t>(offset_bytes) | (static_cast<uint32_t>(count) << 16);
    emit(OP_LOAD_INLINE_ARRAY, 0, dst_reg, payload);
    return *this;
}

QueryBuilder& QueryBuilder::initMockGraph(uint16_t slot_id, uint16_t offset_bytes, uint16_t node_count) {
    uint32_t payload = static_cast<uint32_t>(offset_bytes) | (static_cast<uint32_t>(node_count) << 16);
    emit(OP_INIT_MOCK_GRAPH, 0, slot_id, payload);
    return *this;
}

QueryBuilder& QueryBuilder::throwErr(uint32_t user_error_code) {
    emit(OP_THROW, 0, 0, user_error_code);
    return *this;
}

QueryBuilder& QueryBuilder::assertVal(uint16_t src_reg, uint32_t expected_val, uint8_t flags) {
    emit(OP_ASSERT, flags, src_reg, expected_val);
    return *this;
}

QueryBuilder& QueryBuilder::trap(uint32_t signal_id) {
    emit(OP_TRAP, 0, 0, signal_id);
    return *this;
}

QueryBuilder& QueryBuilder::nop() {
    emit(OP_NOP, 0, 0, 0);
    return *this;
}

// --- Control Flow ---

QueryBuilder& QueryBuilder::repeat(int count, const std::function<void(QueryBuilder&)>& body) {
    uint16_t prev_reg = current_reg_;
    uint16_t counter_reg = allocateRegister();
    loadConstInt(count, counter_reg);
    current_reg_ = prev_reg;

    size_t start_idx = instructions_.size();
    body(*this);
    size_t end_idx = instructions_.size();

    int32_t offset = static_cast<int32_t>(start_idx) - static_cast<int32_t>(end_idx);
    emit(OP_LOOP_DECR, 0, counter_reg, static_cast<uint32_t>(offset));
    return *this;
}

QueryBuilder& QueryBuilder::repeatUntilStable(const std::function<void(QueryBuilder&)>& body) {
    size_t start_idx = instructions_.size();
    body(*this);
    size_t end_idx = instructions_.size();

    int32_t offset = static_cast<int32_t>(start_idx) - static_cast<int32_t>(end_idx);
    emit(OP_STABLE_CHECK, 0, 0, static_cast<uint32_t>(offset));
    return *this;
}

QueryBuilder& QueryBuilder::jmp(int32_t instruction_offset) {
    emit(OP_JMP, 0, 0, static_cast<uint32_t>(instruction_offset));
    return *this;
}

QueryBuilder& QueryBuilder::jz(int32_t instruction_offset) {
    emit(OP_JZ, 0, 0, static_cast<uint32_t>(instruction_offset));
    return *this;
}

QueryBuilder& QueryBuilder::jnz(int32_t instruction_offset) {
    emit(OP_JNZ, 0, 0, static_cast<uint32_t>(instruction_offset));
    return *this;
}

// --- Terminal Collect Operators ---

QueryBuilder& QueryBuilder::collectBitset() {
    emit(OP_COLLECT_BITSET, 0, current_reg_, current_reg_);
    return *this;
}

QueryBuilder& QueryBuilder::collectArray() {
    emit(OP_COLLECT_ARRAY, 0, current_reg_, current_reg_);
    return *this;
}

QueryBuilder& QueryBuilder::mapDenseToKeys() {
    emit(OP_MAP_DENSE_TO_KEYS, 0, current_reg_, current_reg_);
    return *this;
}

QueryBuilder& QueryBuilder::collectValueMap() {
    emit(OP_COLLECT_VALUE_MAP, 0, current_reg_, current_reg_);
    return *this;
}

// --- Compilation ---

QueryBuilder& QueryBuilder::walkEdge2Hop(uint16_t rel1_id, uint16_t rel2_id, uint8_t flags) {
    uint32_t payload = static_cast<uint32_t>(rel1_id) | (static_cast<uint32_t>(rel2_id) << 16);
    emit(OP_CSR_WALK_2HOP, flags, current_reg_, payload);
    return *this;
}

QueryBuilder& QueryBuilder::floatVectorScale(float scale_factor) {
    uint32_t scale_bits{0};
    std::memcpy(&scale_bits, &scale_factor, sizeof(float));
    emit(OP_FLOAT_VECTOR_SCALE, 0, current_reg_, scale_bits);
    return *this;
}

QueryBuilder& QueryBuilder::vectorTimeValidAt(uint16_t ts_reg, uint16_t start_attr_reg, uint16_t end_attr_reg) {
    uint32_t payload = static_cast<uint32_t>(current_reg_ & 0xFF) |
                       (static_cast<uint32_t>(ts_reg & 0xFF) << 8) |
                       (static_cast<uint32_t>(start_attr_reg & 0xFF) << 16) |
                       (static_cast<uint32_t>(end_attr_reg & 0xFF) << 24);
    emit(OP_VECTOR_TIME_VALID_AT, 0, current_reg_, payload);
    return *this;
}

QueryBuilder& QueryBuilder::reduceMin() {
    return reduce(BINARY_OP_MIN);
}

QueryBuilder& QueryBuilder::reduceMax() {
    return reduce(BINARY_OP_MAX);
}

QueryBuilder& QueryBuilder::argMin() {
    return reduce(BINARY_OP_ARGMIN);
}

QueryBuilder& QueryBuilder::argMax() {
    return reduce(BINARY_OP_ARGMAX);
}

QueryBuilder& QueryBuilder::laneInitSeeds(uint16_t seed_reg, uint8_t width_flag) {
    emit(OP_LANE_INIT_SEEDS, width_flag, current_reg_, static_cast<uint32_t>(seed_reg & 0xFFFF));
    return *this;
}

QueryBuilder& QueryBuilder::laneInitGroups(uint16_t pairs_reg, uint16_t num_groups) {
    uint32_t payload = static_cast<uint32_t>(pairs_reg & 0xFFFF) |
                       (static_cast<uint32_t>(num_groups & 0xFFFF) << 16);
    emit(OP_LANE_INIT_GROUPS, 0, current_reg_, payload);
    return *this;
}

QueryBuilder& QueryBuilder::laneSetEdgeMask(uint16_t pairs_reg, uint16_t relation_id) {
    uint32_t payload = static_cast<uint32_t>(pairs_reg & 0xFFFF) |
                       (static_cast<uint32_t>(relation_id & 0xFFFF) << 16);
    emit(OP_LANE_SET_EDGE_MASK, 0, current_reg_, payload);
    return *this;
}

QueryBuilder& QueryBuilder::laneWalkCsr(uint16_t relation_id, bool fixed_point) {
    uint32_t payload = static_cast<uint32_t>(current_reg_ & 0xFFFF) |
                       (static_cast<uint32_t>(relation_id & 0xFFFF) << 16);
    emit(OP_LANE_CSR_WALK, fixed_point ? 1 : 0, current_reg_, payload);
    return *this;
}

QueryBuilder& QueryBuilder::laneWalkCsc(uint16_t relation_id, bool fixed_point) {
    uint32_t payload = static_cast<uint32_t>(current_reg_ & 0xFFFF) |
                       (static_cast<uint32_t>(relation_id & 0xFFFF) << 16);
    emit(OP_LANE_CSC_WALK, fixed_point ? 1 : 0, current_reg_, payload);
    return *this;
}

QueryBuilder& QueryBuilder::lanePruneMask(uint8_t mode, uint16_t arg0, uint16_t arg1) {
    uint32_t payload = static_cast<uint32_t>(current_reg_ & 0xFF) |
                       (static_cast<uint32_t>(arg0 & 0xFFFF) << 8) |
                       (static_cast<uint32_t>(arg1 & 0xFF) << 24);
    emit(OP_LANE_PRUNE_MASK, mode, current_reg_, payload);
    return *this;
}

QueryBuilder& QueryBuilder::laneFilterQuorum(uint16_t min_quorum, uint16_t mask_reg) {
    if (mask_reg > 0) {
        uint32_t payload = static_cast<uint32_t>(current_reg_ & 0xFF) |
                           (static_cast<uint32_t>(min_quorum & 0xFF) << 8) |
                           (static_cast<uint32_t>(mask_reg & 0xFF) << 16);
        emit(OP_LANE_FILTER_QUORUM, 1, current_reg_, payload);
    } else {
        uint32_t payload = static_cast<uint32_t>(current_reg_ & 0xFFFF) |
                           (static_cast<uint32_t>(min_quorum & 0xFFFF) << 16);
        emit(OP_LANE_FILTER_QUORUM, 0, current_reg_, payload);
    }
    return *this;
}

QueryBuilder& QueryBuilder::laneReduceAnd() {
    emit(OP_LANE_REDUCE_AND, 0, current_reg_, static_cast<uint32_t>(current_reg_ & 0xFFFF));
    return *this;
}

QueryBuilder& QueryBuilder::laneReduceAttr(uint16_t attr_id, uint8_t reduce_op) {
    uint32_t payload = static_cast<uint32_t>(current_reg_ & 0xFF) |
                       (static_cast<uint32_t>(attr_id & 0xFF) << 8) |
                       (static_cast<uint32_t>(reduce_op & 0xFFFF) << 16);
    emit(OP_LANE_REDUCE_ATTR, 0, current_reg_, payload);
    return *this;
}

QueryBuilder& QueryBuilder::laneSetCover(uint16_t mask_reg, uint8_t mode) {
    uint32_t payload = static_cast<uint32_t>(current_reg_ & 0xFF) |
                       (static_cast<uint32_t>(mask_reg & 0xFF) << 8);
    emit(OP_LANE_SET_COVER, mode, current_reg_, payload);
    return *this;
}

QueryBuilder& QueryBuilder::vectorAnnSearch(uint16_t query_reg, uint16_t top_k, uint16_t index_or_attr_id, bool fused_lane_frontier) {
    uint32_t payload = static_cast<uint32_t>(query_reg & 0xFF) |
                       (static_cast<uint32_t>(top_k & 0xFF) << 8) |
                       (static_cast<uint32_t>(index_or_attr_id & 0xFFFF) << 16);
    emit(OP_VECTOR_ANN_SEARCH, fused_lane_frontier ? 1 : 0, current_reg_, payload);
    return *this;
}

QueryBuilder& QueryBuilder::vectorRerankDot(uint16_t query_reg, uint16_t attr_id, uint8_t metric) {
    uint32_t payload = static_cast<uint32_t>(current_reg_ & 0xFF) |
                       (static_cast<uint32_t>(query_reg & 0xFF) << 8) |
                       (static_cast<uint32_t>(attr_id & 0xFFFF) << 16);
    emit(OP_VECTOR_RERANK_DOT, metric, current_reg_, payload);
    return *this;
}

CompiledQuery QueryBuilder::compile() {
    IMPULSE_ASSERT(current_reg_ < 64);
    std::vector<impulse_instruction_t> compiled = instructions_;
    compiled.push_back(impulse_instruction_t{OP_HALT, 0, 0, 0});
    IMPULSE_ASSERT(!compiled.empty());
    return CompiledQuery(std::move(compiled), current_reg_);
}

// ---------------------------------------------------------------------------
// High-Level Domain-Bound C++20 Fluent API Implementation
// ---------------------------------------------------------------------------

namespace {

static uint16_t resolve_domain_id_by_name(const impulse_snapshot_t* snap, const std::string& name) {
    if (!snap) return 0;
    uint16_t count = impulse_snapshot_domain_count(snap);
    for (uint16_t i = 0; i < count; ++i) {
        impulse_domain_catalog_entry_t entry{};
        const char* dname = nullptr;
        if (impulse_snapshot_get_domain_entry(snap, i, &entry, &dname) == IMPULSE_OK && dname) {
            if (name == dname) return i;
        }
    }
    if (name.rfind("dom_", 0) == 0) {
        return static_cast<uint16_t>(std::stoul(name.substr(4)));
    }
    if (!name.empty() && std::isdigit(static_cast<unsigned char>(name[0]))) {
        return static_cast<uint16_t>(std::stoul(name));
    }
    return 0;
}

static uint16_t resolve_relation_id_by_name(const impulse_snapshot_t* snap, uint16_t /*domain_id*/, const std::string& rel_name) {
    if (!snap) return 0;
    if (rel_name.rfind("rel_", 0) == 0) {
        return static_cast<uint16_t>(std::stoul(rel_name.substr(4)));
    }
    if (!rel_name.empty() && std::isdigit(static_cast<unsigned char>(rel_name[0]))) {
        return static_cast<uint16_t>(std::stoul(rel_name));
    }
    uint16_t rel_count = impulse_snapshot_relation_count(snap);
    for (uint16_t i = 0; i < rel_count; ++i) {
        if (rel_name == ("rel_" + std::to_string(i))) return i;
    }
    return 0;
}

static uint32_t get_domain_node_count(const impulse_snapshot_t* snap, uint16_t domain_id) {
    if (!snap) return 0;
    uint16_t rel_count = impulse_snapshot_relation_count(snap);
    uint64_t max_n = 0;
    for (uint16_t r = 0; r < rel_count; ++r) {
        impulse_relation_directory_entry_t entry{};
        if (impulse_snapshot_get_relation_entry(snap, r, &entry) == IMPULSE_OK) {
            if (entry.src_domain_id == domain_id && entry.node_count > max_n) {
                max_n = entry.node_count;
            }
        }
    }
    if (max_n == 0) {
        max_n = impulse_snapshot_max_node_count(snap);
    }
    return static_cast<uint32_t>(max_n);
}

static uint32_t resolve_key_to_dense(const impulse_snapshot_t* snap, uint16_t domain_id, const std::string& key) {
    if (snap) {
        uint32_t dense_id = 0;
        if (impulse_snapshot_resolve_key(snap, domain_id, key.data(), key.size(), &dense_id) == IMPULSE_OK) {
            return dense_id;
        }
    }
    if (!key.empty() && std::isdigit(static_cast<unsigned char>(key[0]))) {
        return static_cast<uint32_t>(std::stoul(key));
    }
    return 0;
}

static std::string resolve_dense_to_key(const impulse_snapshot_t* snap, uint16_t domain_id, uint32_t node_id) {
    if (snap) {
        const void* key_bytes = nullptr;
        size_t key_len = 0;
        if (impulse_snapshot_resolve_dense_id(snap, domain_id, node_id, &key_bytes, &key_len) == IMPULSE_OK &&
            key_bytes != nullptr && key_len > 0) {
            return std::string(static_cast<const char*>(key_bytes), key_len);
        }
    }
    return std::to_string(node_id);
}

static uint16_t resolve_attr_or_index_id(const impulse_snapshot_t* snap, const std::string& name) {
    if (name.empty()) return 0;
    if (std::isdigit(static_cast<unsigned char>(name[0]))) {
        return static_cast<uint16_t>(std::stoul(name));
    }
    if (snap) {
        uint16_t idx_count = impulse_snapshot_get_index_count(snap);
        for (uint16_t k = 0; k < idx_count; ++k) {
            const char* idx_name = nullptr;
            if (impulse_snapshot_get_index(snap, k, nullptr, nullptr, nullptr, nullptr, nullptr, &idx_name, nullptr, nullptr) == IMPULSE_OK &&
                idx_name && name == idx_name) {
                return k;
            }
        }
    }
    return 0;
}

} // anonymous namespace

MultiLaneResult::MultiLaneResult(const impulse_snapshot_t* snapshot,
                                 uint16_t domain_id,
                                 uint32_t domain_node_count,
                                 uint32_t lane_count,
                                 std::vector<std::string> lane_keys,
                                 std::vector<uint64_t> words_per_node)
    : snapshot_(snapshot),
      domain_id_(domain_id),
      domain_node_count_(domain_node_count),
      lane_count_(lane_count),
      words_per_lane_((lane_count + 63U) / 64U),
      lane_keys_(std::move(lane_keys)),
      words_per_node_(std::move(words_per_node)) {}

uint64_t MultiLaneResult::laneMaskFor(uint32_t node_id) const {
    if (node_id >= domain_node_count_ || words_per_lane_ == 0) return 0ULL;
    return words_per_node_[static_cast<size_t>(node_id) * words_per_lane_];
}

uint32_t MultiLaneResult::lanePopcountFor(uint32_t node_id) const {
    if (node_id >= domain_node_count_ || words_per_lane_ == 0) return 0U;
    uint32_t total = 0;
    const uint64_t* row = &words_per_node_[static_cast<size_t>(node_id) * words_per_lane_];
    for (uint32_t w = 0; w < words_per_lane_; ++w) {
        total += static_cast<uint32_t>(std::popcount(row[w]));
    }
    return total;
}

std::vector<uint32_t> MultiLaneResult::attributionFor(uint32_t node_id) const {
    std::vector<uint32_t> lanes;
    if (node_id >= domain_node_count_ || words_per_lane_ == 0) return lanes;
    const uint64_t* row = &words_per_node_[static_cast<size_t>(node_id) * words_per_lane_];
    for (uint32_t w = 0; w < words_per_lane_; ++w) {
        uint64_t m = row[w];
        while (m != 0ULL) {
            uint32_t b = static_cast<uint32_t>(std::countr_zero(m));
            uint32_t global_lane = w * 64U + b;
            if (global_lane < lane_count_) {
                lanes.push_back(global_lane);
            }
            m &= (m - 1ULL);
        }
    }
    return lanes;
}

std::vector<std::string> MultiLaneResult::keyAttributionFor(const std::string& node_key) const {
    uint32_t u = resolve_key_to_dense(snapshot_, domain_id_, node_key);
    auto lanes = attributionFor(u);
    std::vector<std::string> out;
    out.reserve(lanes.size());
    for (uint32_t lb : lanes) {
        if (lb < lane_keys_.size()) {
            out.push_back(lane_keys_[lb]);
        } else {
            out.push_back(std::to_string(lb));
        }
    }
    return out;
}

std::vector<uint32_t> MultiLaneResult::nodesWithQuorum(uint32_t min_lanes) const {
    std::vector<uint32_t> out;
    if (words_per_lane_ == 0 || lane_count_ == 0) return out;
    for (uint32_t u = 0; u < domain_node_count_; ++u) {
        if (lanePopcountFor(u) >= min_lanes) {
            out.push_back(u);
        }
    }
    return out;
}

std::vector<std::pair<std::string, uint32_t>> MultiLaneResult::keysWithQuorum(uint32_t min_lanes) const {
    std::vector<std::pair<std::string, uint32_t>> out;
    if (words_per_lane_ == 0 || lane_count_ == 0) return out;
    for (uint32_t u = 0; u < domain_node_count_; ++u) {
        uint32_t pc = lanePopcountFor(u);
        if (pc >= min_lanes) {
            out.emplace_back(resolve_dense_to_key(snapshot_, domain_id_, u), pc);
        }
    }
    return out;
}

std::vector<std::pair<uint32_t, uint32_t>> MultiLaneResult::findSetCoverPairs(uint64_t target_mask) const {
    std::vector<std::pair<uint32_t, uint32_t>> pairs;
    if (lane_count_ == 0 || words_per_lane_ == 0) return pairs;
    uint64_t eff_mask = target_mask;
    if (lane_count_ < 64) {
        eff_mask &= ((1ULL << lane_count_) - 1ULL);
    }
    if (eff_mask == 0ULL) return pairs;

    std::vector<uint32_t> active;
    for (uint32_t u = 0; u < domain_node_count_; ++u) {
        if ((laneMaskFor(u) & eff_mask) != 0ULL) {
            active.push_back(u);
        }
    }
    for (size_t i = 0; i < active.size(); ++i) {
        uint64_t mu = laneMaskFor(active[i]) & eff_mask;
        for (size_t j = i + 1; j < active.size(); ++j) {
            uint64_t mv = laneMaskFor(active[j]) & eff_mask;
            if ((mu | mv) == eff_mask) {
                pairs.emplace_back(active[i], active[j]);
            }
        }
    }
    return pairs;
}

std::vector<uint32_t> MultiLaneResult::findGreedySetCover(uint64_t target_mask, uint32_t max_nodes) const {
    std::vector<uint32_t> selected;
    if (lane_count_ == 0 || words_per_lane_ == 0 || max_nodes == 0) return selected;

    std::vector<uint64_t> uncovered(words_per_lane_, ~0ULL);
    uncovered[0] &= target_mask;
    if (lane_count_ % 64 != 0) {
        uncovered[words_per_lane_ - 1] &= ((1ULL << (lane_count_ % 64)) - 1ULL);
    }

    auto has_uncovered = [&]() {
        for (uint64_t w : uncovered) {
            if (w != 0ULL) return true;
        }
        return false;
    };

    for (uint32_t step = 0; step < max_nodes && has_uncovered(); ++step) {
        uint32_t best_u = UINT32_MAX;
        uint32_t best_gain = 0;
        for (uint32_t u = 0; u < domain_node_count_; ++u) {
            uint32_t gain = 0;
            const uint64_t* row = &words_per_node_[static_cast<size_t>(u) * words_per_lane_];
            for (uint32_t w = 0; w < words_per_lane_; ++w) {
                gain += static_cast<uint32_t>(std::popcount(row[w] & uncovered[w]));
            }
            if (gain > best_gain) {
                best_gain = gain;
                best_u = u;
            }
        }
        if (best_u == UINT32_MAX || best_gain == 0) break;
        selected.push_back(best_u);
        const uint64_t* best_row = &words_per_node_[static_cast<size_t>(best_u) * words_per_lane_];
        for (uint32_t w = 0; w < words_per_lane_; ++w) {
            uncovered[w] &= ~best_row[w];
        }
    }
    return selected;
}

// --- Single-Lane Traversal Implementation ---

Traversal::Traversal(const impulse_snapshot_t* snapshot, uint16_t domain_id, std::vector<uint64_t> seeds, bool all_nodes)
    : snapshot_(snapshot),
      initial_domain_id_(domain_id),
      current_domain_id_(domain_id),
      seeds_(std::move(seeds)),
      all_nodes_(all_nodes) {}

Traversal& Traversal::out(const std::string& relation) {
    Step s{};
    s.kind = Step::Kind::OUT;
    s.str1 = relation;
    steps_.push_back(std::move(s));
    return *this;
}

Traversal& Traversal::in(const std::string& relation) {
    Step s{};
    s.kind = Step::Kind::IN;
    s.str1 = relation;
    steps_.push_back(std::move(s));
    return *this;
}

Traversal& Traversal::outFiltered(const std::string& relation, uint32_t filter_id) {
    Step s{};
    s.kind = Step::Kind::OUT_FILTERED;
    s.str1 = relation;
    s.u32_arg1 = filter_id;
    steps_.push_back(std::move(s));
    return *this;
}

Traversal& Traversal::outPredicate(const std::string& relation, const std::string& edge_cel_predicate) {
    Step s{};
    s.kind = Step::Kind::OUT_PREDICATE;
    s.str1 = relation;
    s.str2 = edge_cel_predicate;
    steps_.push_back(std::move(s));
    return *this;
}

Traversal& Traversal::inPredicate(const std::string& relation, const std::string& edge_cel_predicate) {
    Step s{};
    s.kind = Step::Kind::IN_PREDICATE;
    s.str1 = relation;
    s.str2 = edge_cel_predicate;
    steps_.push_back(std::move(s));
    return *this;
}

Traversal& Traversal::out2Hop(const std::string& rel1, const std::string& rel2) {
    Step s{};
    s.kind = Step::Kind::OUT_2HOP;
    s.str1 = rel1;
    s.str2 = rel2;
    steps_.push_back(std::move(s));
    return *this;
}

Traversal& Traversal::repeat(int iterations, const std::string& relation) {
    Step s{};
    s.kind = Step::Kind::REPEAT;
    s.str1 = relation;
    s.u32_arg1 = static_cast<uint32_t>(std::max(0, iterations));
    steps_.push_back(std::move(s));
    return *this;
}

Traversal& Traversal::repeatUntilStable(const std::string& relation) {
    Step s{};
    s.kind = Step::Kind::REPEAT_UNTIL_STABLE;
    s.str1 = relation;
    steps_.push_back(std::move(s));
    return *this;
}

Traversal& Traversal::filter(const std::string& node_cel_predicate) {
    Step s{};
    s.kind = Step::Kind::FILTER_CEL;
    s.str1 = node_cel_predicate;
    steps_.push_back(std::move(s));
    return *this;
}

Traversal& Traversal::project(const std::string& cel_expression) {
    Step s{};
    s.kind = Step::Kind::PROJECT_CEL;
    s.str1 = cel_expression;
    steps_.push_back(std::move(s));
    return *this;
}

Traversal& Traversal::validAt(int64_t epoch_millis) {
    Step s{};
    s.kind = Step::Kind::VALID_AT;
    s.i64_arg = epoch_millis;
    steps_.push_back(std::move(s));
    return *this;
}

Traversal& Traversal::unionWith(const Traversal& other) {
    Step s{};
    s.kind = Step::Kind::SET_UNION;
    s.other_nodes = other.collect();
    steps_.push_back(std::move(s));
    return *this;
}

Traversal& Traversal::intersect(const Traversal& other) {
    Step s{};
    s.kind = Step::Kind::SET_INTERSECT;
    s.other_nodes = other.collect();
    steps_.push_back(std::move(s));
    return *this;
}

Traversal& Traversal::difference(const Traversal& other) {
    Step s{};
    s.kind = Step::Kind::SET_DIFFERENCE;
    s.other_nodes = other.collect();
    steps_.push_back(std::move(s));
    return *this;
}

Traversal& Traversal::mulNodeAttr(const std::string& attr_name) {
    Step s{};
    s.kind = Step::Kind::MUL_ATTR;
    s.str1 = attr_name;
    steps_.push_back(std::move(s));
    return *this;
}

Traversal& Traversal::scaleState(float factor) {
    Step s{};
    s.kind = Step::Kind::SCALE_STATE;
    s.f32_arg = factor;
    steps_.push_back(std::move(s));
    return *this;
}

Traversal& Traversal::divNodeAttr(const std::string& attr_name) {
    Step s{};
    s.kind = Step::Kind::DIV_ATTR;
    s.str1 = attr_name;
    steps_.push_back(std::move(s));
    return *this;
}

double Traversal::l1NormDiff(const Traversal& other) const {
    auto v1 = evaluateStateVector();
    auto v2 = other.evaluateStateVector();
    size_t n = std::max(v1.size(), v2.size());
    double diff = 0.0;
    for (size_t i = 0; i < n; ++i) {
        double a = (i < v1.size()) ? v1[i] : 0.0;
        double b = (i < v2.size()) ? v2[i] : 0.0;
        diff += std::abs(a - b);
    }
    return diff;
}

Traversal& Traversal::mxv(const std::string& relation, uint8_t semiring) {
    Step s{};
    s.kind = Step::Kind::MXV;
    s.str1 = relation;
    s.u32_arg1 = semiring;
    steps_.push_back(std::move(s));
    return *this;
}

Traversal& Traversal::vxm(const std::string& relation, uint8_t semiring) {
    Step s{};
    s.kind = Step::Kind::VXM;
    s.str1 = relation;
    s.u32_arg1 = semiring;
    steps_.push_back(std::move(s));
    return *this;
}

Traversal& Traversal::ewiseAdd(const Traversal& other, uint8_t binary_op) {
    Step s{};
    s.kind = Step::Kind::EWISE_ADD;
    s.u32_arg1 = binary_op;
    s.other_nodes = other.collect();
    steps_.push_back(std::move(s));
    return *this;
}

Traversal& Traversal::ewiseMult(const Traversal& other, uint8_t binary_op) {
    Step s{};
    s.kind = Step::Kind::EWISE_MULT;
    s.u32_arg1 = binary_op;
    s.other_nodes = other.collect();
    steps_.push_back(std::move(s));
    return *this;
}

Traversal& Traversal::degree(const std::string& relation) {
    Step s{};
    s.kind = Step::Kind::DEGREE;
    s.str1 = relation;
    steps_.push_back(std::move(s));
    return *this;
}

Traversal& Traversal::sampleNeighbors(const std::string& relation, int32_t k, uint32_t seed) {
    Step s{};
    s.kind = Step::Kind::SAMPLE_NEIGHBORS;
    s.str1 = relation;
    s.u32_arg1 = static_cast<uint32_t>(k);
    s.u32_arg2 = seed;
    steps_.push_back(std::move(s));
    return *this;
}

Traversal& Traversal::randomWalk(const std::string& relation, int32_t steps_cnt, uint32_t seed) {
    Step s{};
    s.kind = Step::Kind::RANDOM_WALK;
    s.str1 = relation;
    s.u32_arg1 = static_cast<uint32_t>(steps_cnt);
    s.u32_arg2 = seed;
    steps_.push_back(std::move(s));
    return *this;
}

Traversal& Traversal::kCore(int32_t k) {
    Step s{};
    s.kind = Step::Kind::KCORE;
    s.u32_arg1 = static_cast<uint32_t>(k);
    steps_.push_back(std::move(s));
    return *this;
}

Traversal& Traversal::rerank(const std::vector<float>& query_vector, const std::string& attr_name, uint8_t metric) {
    Step s{};
    s.kind = Step::Kind::RERANK;
    s.str1 = attr_name;
    s.u32_arg1 = metric;
    s.float_vec = query_vector;
    steps_.push_back(std::move(s));
    return *this;
}

CompiledQuery Traversal::compile() const {
    QueryBuilder qb(0);
    if (all_nodes_ || seeds_.size() != 1) {
        qb.inputSet(0);
    } else {
        qb.inputNode(0);
    }
    for (const auto& step : steps_) {
        uint16_t rel_id = resolve_relation_id_by_name(snapshot_, current_domain_id_, step.str1);
        switch (step.kind) {
            case Step::Kind::OUT:
            case Step::Kind::MXV:
                qb.walkEdge(rel_id);
                break;
            case Step::Kind::IN:
            case Step::Kind::VXM:
                qb.walkCsc(rel_id);
                break;
            case Step::Kind::OUT_FILTERED:
                qb.walkEdgeFiltered(rel_id, step.u32_arg1);
                break;
            case Step::Kind::OUT_PREDICATE:
                qb.walkEdgePredicate(rel_id, 0);
                break;
            case Step::Kind::IN_PREDICATE:
                qb.walkCsc(rel_id);
                break;
            case Step::Kind::OUT_2HOP: {
                uint16_t r2 = resolve_relation_id_by_name(snapshot_, current_domain_id_, step.str2);
                qb.walkEdge2Hop(rel_id, r2);
                break;
            }
            case Step::Kind::REPEAT:
                qb.repeat(static_cast<int>(step.u32_arg1), [rel_id](QueryBuilder& b) {
                    b.walkEdge(rel_id);
                });
                break;
            case Step::Kind::REPEAT_UNTIL_STABLE:
                qb.repeatUntilStable([rel_id](QueryBuilder& b) {
                    b.walkEdge(rel_id);
                });
                break;
            case Step::Kind::FILTER_CEL:
                qb.filterCel(step.str1);
                break;
            case Step::Kind::PROJECT_CEL:
                qb.project(step.str1);
                break;
            case Step::Kind::VALID_AT:
                qb.loadConstInt(step.i64_arg, 3);
                qb.setCurrentRegister(0);
                qb.vectorTimeValidAt(3, 1, 2);
                break;
            case Step::Kind::DEGREE:
                qb.walkDegree(rel_id);
                break;
            case Step::Kind::SAMPLE_NEIGHBORS:
                qb.sampleNeighbors(rel_id, static_cast<int32_t>(step.u32_arg1), step.u32_arg2);
                break;
            case Step::Kind::RANDOM_WALK:
                qb.randomWalk(rel_id, static_cast<int32_t>(step.u32_arg1), step.u32_arg2);
                break;
            case Step::Kind::KCORE:
                qb.kcoreDecomposition();
                break;
            case Step::Kind::SCALE_STATE:
                qb.floatVectorScale(step.f32_arg);
                break;
            default:
                break;
        }
    }
    qb.collectBitset();
    return qb.compile();
}

std::vector<uint64_t> Traversal::collect() const {
    if (!snapshot_) return {};
    uint32_t max_n = get_domain_node_count(snapshot_, initial_domain_id_);
    if (max_n == 0) return {};

    impulse_vm_context_t* ctx = impulse_vm_context_create(snapshot_);
    if (!ctx) return {};

    impulse_vm_state_t state{};
    state.query_context = ctx;

    // Seed initial frontier in R0
    std::vector<impulse_instruction_t> init_prog;
    if (all_nodes_) {
        init_prog.push_back(impulse_instruction_t{OP_INIT_INPUT_SET, 0, 0, 0});
    } else if (seeds_.empty()) {
        init_prog.push_back(impulse_instruction_t{OP_CLEAR_REG, 0, 0, 0});
    } else {
        init_prog.push_back(impulse_instruction_t{OP_INIT_INPUT_NODE, 0, 0, 0});
    }
    init_prog.push_back(impulse_instruction_t{OP_HALT, 0, 0, 0});

    if (all_nodes_ || seeds_.empty()) {
        state.pc = 0;
        impulse_vm_execute(init_prog.data(), init_prog.size(), &state, 0);
    } else {
        // Union all seeds into R0 via VM
        state.pc = 0;
        impulse_vm_execute(init_prog.data(), init_prog.size(), &state, seeds_[0]);
        for (size_t i = 1; i < seeds_.size(); ++i) {
            impulse_instruction_t add_seed[3] = {
                {OP_INIT_INPUT_NODE, 0, 2, 0},
                {OP_SET_UNION, 0, 0, static_cast<uint32_t>(0 | (2 << 16))},
                {OP_HALT, 0, 0, 0}
            };
            state.pc = 0;
            impulse_vm_execute(add_seed, 3, &state, seeds_[i]);
        }
    }

    for (const auto& step : steps_) {
        uint16_t rel_id = resolve_relation_id_by_name(snapshot_, current_domain_id_, step.str1);
        if (step.kind == Step::Kind::OUT || step.kind == Step::Kind::MXV) {
            impulse_instruction_t insts[2] = {
                {OP_CSR_WALK, 0, 0, static_cast<uint32_t>((rel_id << 16) | 0)},
                {OP_HALT, 0, 0, 0}
            };
            state.pc = 0;
            impulse_vm_execute(insts, 2, &state, 0);
        } else if (step.kind == Step::Kind::IN || step.kind == Step::Kind::VXM) {
            impulse_instruction_t insts[2] = {
                {OP_CSC_WALK, 0, 0, static_cast<uint32_t>((rel_id << 16) | 0)},
                {OP_HALT, 0, 0, 0}
            };
            state.pc = 0;
            impulse_vm_execute(insts, 2, &state, 0);
        } else if (step.kind == Step::Kind::OUT_2HOP) {
            uint16_t r2 = resolve_relation_id_by_name(snapshot_, current_domain_id_, step.str2);
            impulse_instruction_t insts[2] = {
                {OP_CSR_WALK_2HOP, 0, 0, static_cast<uint32_t>(rel_id | (r2 << 16))},
                {OP_HALT, 0, 0, 0}
            };
            state.pc = 0;
            impulse_vm_execute(insts, 2, &state, 0);
        } else if (step.kind == Step::Kind::OUT_FILTERED) {
            impulse_instruction_t insts[2] = {
                {OP_CSR_WALK_FILTERED, 0, 0, static_cast<uint32_t>((rel_id << 16) | ((step.u32_arg1 & 0xFF) << 8) | 0)},
                {OP_HALT, 0, 0, 0}
            };
            state.pc = 0;
            impulse_vm_execute(insts, 2, &state, 0);
        } else if (step.kind == Step::Kind::OUT_PREDICATE) {
            impulse_instruction_t insts[2] = {
                {OP_CSR_WALK_PREDICATE, 0, 0, static_cast<uint32_t>((rel_id << 16) | 0)},
                {OP_HALT, 0, 0, 0}
            };
            state.pc = 0;
            impulse_vm_execute(insts, 2, &state, 0);
        } else if (step.kind == Step::Kind::REPEAT) {
            for (uint32_t it = 0; it < step.u32_arg1; ++it) {
                impulse_instruction_t insts[2] = {
                    {OP_CSR_WALK, 0, 0, static_cast<uint32_t>((rel_id << 16) | 0)},
                    {OP_HALT, 0, 0, 0}
                };
                state.pc = 0;
                impulse_vm_execute(insts, 2, &state, 0);
            }
        } else if (step.kind == Step::Kind::REPEAT_UNTIL_STABLE) {
            impulse_instruction_t insts[3] = {
                {OP_CSR_WALK, 0, 0, static_cast<uint32_t>((rel_id << 16) | 0)},
                {OP_STABLE_CHECK, 0, 0, static_cast<uint32_t>(-1)},
                {OP_HALT, 0, 0, 0}
            };
            state.pc = 0;
            impulse_vm_execute(insts, 3, &state, 0);
        } else if (step.kind == Step::Kind::SET_UNION || step.kind == Step::Kind::SET_INTERSECT ||
                   step.kind == Step::Kind::SET_DIFFERENCE || step.kind == Step::Kind::EWISE_ADD ||
                   step.kind == Step::Kind::EWISE_MULT) {
            // Load other_nodes into R3 via VM
            impulse_instruction_t clr[2] = {{OP_CLEAR_REG, 0, 3, 0}, {OP_HALT, 0, 0, 0}};
            state.pc = 0;
            impulse_vm_execute(clr, 2, &state, 0);
            if (!step.other_nodes.empty()) {
                impulse_instruction_t init3[2] = {{OP_INIT_INPUT_NODE, 0, 3, 0}, {OP_HALT, 0, 0, 0}};
                state.pc = 0;
                impulse_vm_execute(init3, 2, &state, step.other_nodes[0]);
                for (size_t i = 1; i < step.other_nodes.size(); ++i) {
                    impulse_instruction_t u3[3] = {
                        {OP_INIT_INPUT_NODE, 0, 4, 0},
                        {OP_SET_UNION, 0, 3, static_cast<uint32_t>(3 | (4 << 16))},
                        {OP_HALT, 0, 0, 0}
                    };
                    state.pc = 0;
                    impulse_vm_execute(u3, 3, &state, step.other_nodes[i]);
                }
            }
            uint8_t op = OP_SET_UNION;
            if (step.kind == Step::Kind::SET_INTERSECT || step.kind == Step::Kind::EWISE_MULT) op = OP_SET_INTERSECT;
            else if (step.kind == Step::Kind::SET_DIFFERENCE) op = OP_SET_DIFFERENCE;
            impulse_instruction_t comb[2] = {
                {op, 0, 0, static_cast<uint32_t>(0 | (3 << 16))},
                {OP_HALT, 0, 0, 0}
            };
            state.pc = 0;
            impulse_vm_execute(comb, 2, &state, 0);
        } else if (step.kind == Step::Kind::SAMPLE_NEIGHBORS) {
            impulse_instruction_t insts[2] = {
                {OP_SAMPLE_NEIGHBORS, 0, 0, static_cast<uint32_t>((rel_id << 16) | (step.u32_arg1 & 0xFFFF))},
                {OP_HALT, 0, 0, 0}
            };
            state.pc = 0;
            impulse_vm_execute(insts, 2, &state, 0);
        } else if (step.kind == Step::Kind::RANDOM_WALK) {
            impulse_instruction_t insts[2] = {
                {OP_RANDOM_WALK, 0, 0, static_cast<uint32_t>((rel_id << 16) | (step.u32_arg1 & 0xFFFF))},
                {OP_HALT, 0, 0, 0}
            };
            state.pc = 0;
            impulse_vm_execute(insts, 2, &state, 0);
        } else if (step.kind == Step::Kind::KCORE) {
            impulse_instruction_t insts[2] = {
                {OP_KCORE_DECOMPOSITION, 0, 0, step.u32_arg1},
                {OP_HALT, 0, 0, 0}
            };
            state.pc = 0;
            impulse_vm_execute(insts, 2, &state, 0);
        }
    }

    std::vector<uint64_t> out;
    if (state.register_types[0] == TYPE_BITSET_HANDLE) {
        uint64_t handle = state.registers[0];
        for (uint32_t u = 0; u < max_n; ++u) {
            if (impulse_vm_context_bitset_test(ctx, handle, u)) {
                out.push_back(u);
            }
        }
    }
    impulse_vm_context_destroy(ctx);
    return out;
}

std::vector<std::string> Traversal::collectKeys() const {
    auto nodes = collect();
    std::vector<std::string> keys;
    keys.reserve(nodes.size());
    for (uint64_t u : nodes) {
        keys.push_back(resolve_dense_to_key(snapshot_, current_domain_id_, static_cast<uint32_t>(u)));
    }
    return keys;
}

size_t Traversal::count() const {
    return collect().size();
}

std::vector<float> Traversal::evaluateStateVector(std::vector<uint64_t>* out_active_nodes) const {
    auto active = collect();
    if (out_active_nodes) *out_active_nodes = active;
    uint32_t max_n = get_domain_node_count(snapshot_, current_domain_id_);
    std::vector<float> state_vec(max_n, 0.0f);

    const float* attr_f32 = nullptr;
    uint64_t attr_bytes = 0;
    if (snapshot_ && impulse_snapshot_relation_count(snapshot_) > 0) {
        const void* raw_data = nullptr;
        uint8_t tc = 0;
        uint32_t dim = 0;
        if (impulse_snapshot_get_attribute_buffers(snapshot_, 0, 0, &raw_data, &attr_bytes, nullptr, nullptr, &tc, &dim) == IMPULSE_OK &&
            raw_data != nullptr && tc == IMPULSE_ATTR_TYPE_FLOAT32) {
            attr_f32 = static_cast<const float*>(raw_data);
        }
    }
    size_t attr_count = attr_f32 ? (attr_bytes / sizeof(float)) : 0;

    for (uint64_t u : active) {
        if (u < max_n) {
            state_vec[u] = (u < attr_count) ? attr_f32[u] : 1.0f;
        }
    }

    for (const auto& step : steps_) {
        if (step.kind == Step::Kind::SCALE_STATE) {
            for (uint64_t u : active) {
                if (u < max_n) state_vec[u] *= step.f32_arg;
            }
        } else if (step.kind == Step::Kind::MUL_ATTR && attr_f32) {
            for (uint64_t u : active) {
                if (u < max_n && u < attr_count) state_vec[u] *= attr_f32[u];
            }
        } else if (step.kind == Step::Kind::DIV_ATTR && attr_f32) {
            for (uint64_t u : active) {
                if (u < max_n && u < attr_count && attr_f32[u] != 0.0f) state_vec[u] /= attr_f32[u];
            }
        }
    }
    return state_vec;
}

double Traversal::reduceSum() const {
    std::vector<uint64_t> active;
    auto vec = evaluateStateVector(&active);
    double sum = 0.0;
    for (uint64_t u : active) {
        if (u < vec.size()) sum += vec[u];
    }
    return sum;
}

double Traversal::walkReduceSum(const std::string& relation, const std::string& /*attr_name*/) const {
    Traversal next = *this;
    next.out(relation);
    return next.reduceSum();
}

double Traversal::reduceMin() const {
    std::vector<uint64_t> active;
    auto vec = evaluateStateVector(&active);
    if (active.empty()) return 0.0;
    double best = vec[active[0]];
    for (uint64_t u : active) {
        if (u < vec.size() && vec[u] < best) best = vec[u];
    }
    return best;
}

double Traversal::reduceMax() const {
    std::vector<uint64_t> active;
    auto vec = evaluateStateVector(&active);
    if (active.empty()) return 0.0;
    double best = vec[active[0]];
    for (uint64_t u : active) {
        if (u < vec.size() && vec[u] > best) best = vec[u];
    }
    return best;
}

int64_t Traversal::argMin() const {
    std::vector<uint64_t> active;
    auto vec = evaluateStateVector(&active);
    if (active.empty()) return -1;
    int64_t best_u = static_cast<int64_t>(active[0]);
    double best_v = vec[active[0]];
    for (uint64_t u : active) {
        if (u < vec.size()) {
            if (vec[u] < best_v || (vec[u] == best_v && static_cast<int64_t>(u) < best_u)) {
                best_v = vec[u];
                best_u = static_cast<int64_t>(u);
            }
        }
    }
    return best_u;
}

int64_t Traversal::argMax() const {
    std::vector<uint64_t> active;
    auto vec = evaluateStateVector(&active);
    if (active.empty()) return -1;
    int64_t best_u = static_cast<int64_t>(active[0]);
    double best_v = vec[active[0]];
    for (uint64_t u : active) {
        if (u < vec.size()) {
            if (vec[u] > best_v || (vec[u] == best_v && static_cast<int64_t>(u) < best_u)) {
                best_v = vec[u];
                best_u = static_cast<int64_t>(u);
            }
        }
    }
    return best_u;
}

std::string Traversal::argMinKey() const {
    int64_t u = argMin();
    return (u < 0) ? "" : resolve_dense_to_key(snapshot_, current_domain_id_, static_cast<uint32_t>(u));
}

std::string Traversal::argMaxKey() const {
    int64_t u = argMax();
    return (u < 0) ? "" : resolve_dense_to_key(snapshot_, current_domain_id_, static_cast<uint32_t>(u));
}

// --- Multi-Lane Traversal Implementation ---

MultiLaneTraversal::MultiLaneTraversal(const impulse_snapshot_t* snapshot,
                                       uint16_t domain_id,
                                       InitMode mode,
                                       std::vector<uint64_t> seeds,
                                       std::vector<std::string> lane_keys,
                                       std::vector<std::pair<uint32_t, uint32_t>> group_pairs,
                                       uint16_t outage_rel_id,
                                       std::vector<std::vector<uint32_t>> tripped_edges_per_lane)
    : snapshot_(snapshot),
      current_domain_id_(domain_id),
      mode_(mode),
      seeds_(std::move(seeds)),
      lane_keys_(std::move(lane_keys)),
      group_pairs_(std::move(group_pairs)),
      outage_rel_id_(outage_rel_id),
      tripped_edges_per_lane_(std::move(tripped_edges_per_lane)) {}

MultiLaneTraversal& MultiLaneTraversal::out(const std::string& relation) {
    LaneStep s{};
    s.kind = LaneStep::Kind::OUT;
    s.str_arg = relation;
    steps_.push_back(std::move(s));
    return *this;
}

MultiLaneTraversal& MultiLaneTraversal::out(const std::string& relation, const std::string& /*edge_cel_predicate*/) {
    return out(relation);
}

MultiLaneTraversal& MultiLaneTraversal::in(const std::string& relation) {
    LaneStep s{};
    s.kind = LaneStep::Kind::IN;
    s.str_arg = relation;
    steps_.push_back(std::move(s));
    return *this;
}

MultiLaneTraversal& MultiLaneTraversal::in(const std::string& relation, const std::string& /*edge_cel_predicate*/) {
    return in(relation);
}

MultiLaneTraversal& MultiLaneTraversal::repeat(int iterations, const std::string& relation) {
    LaneStep s{};
    s.kind = LaneStep::Kind::REPEAT;
    s.str_arg = relation;
    s.u16_arg = static_cast<uint16_t>(std::max(0, iterations));
    steps_.push_back(std::move(s));
    return *this;
}

MultiLaneTraversal& MultiLaneTraversal::repeatUntilStable(const std::string& relation) {
    LaneStep s{};
    s.kind = LaneStep::Kind::REPEAT_UNTIL_STABLE;
    s.str_arg = relation;
    steps_.push_back(std::move(s));
    return *this;
}

MultiLaneTraversal& MultiLaneTraversal::filter(const std::string& node_cel_predicate) {
    LaneStep s{};
    s.kind = LaneStep::Kind::FILTER_CEL;
    s.str_arg = node_cel_predicate;
    steps_.push_back(std::move(s));
    return *this;
}

MultiLaneTraversal& MultiLaneTraversal::project(const std::string& cel_expression) {
    LaneStep s{};
    s.kind = LaneStep::Kind::PROJECT_CEL;
    s.str_arg = cel_expression;
    steps_.push_back(std::move(s));
    return *this;
}

MultiLaneTraversal& MultiLaneTraversal::pruneWhere(const std::string& deny_cel_predicate) {
    LaneStep s{};
    s.kind = LaneStep::Kind::PRUNE_WHERE_CEL;
    s.str_arg = deny_cel_predicate;
    steps_.push_back(std::move(s));
    return *this;
}

MultiLaneTraversal& MultiLaneTraversal::pruneDeniedBy(const std::string& deny_relation) {
    LaneStep s{};
    s.kind = LaneStep::Kind::PRUNE_DENIED_BY;
    s.str_arg = deny_relation;
    steps_.push_back(std::move(s));
    return *this;
}

MultiLaneTraversal& MultiLaneTraversal::pruneHubs(uint16_t max_degree, uint8_t min_quorum) {
    LaneStep s{};
    s.kind = LaneStep::Kind::PRUNE_HUBS;
    s.u16_arg = max_degree;
    s.u8_arg = min_quorum;
    steps_.push_back(std::move(s));
    return *this;
}

MultiLaneTraversal& MultiLaneTraversal::whereQuorum(uint8_t min_lanes) {
    LaneStep s{};
    s.kind = LaneStep::Kind::WHERE_QUORUM;
    s.u8_arg = min_lanes;
    steps_.push_back(std::move(s));
    return *this;
}

MultiLaneTraversal& MultiLaneTraversal::whereMask(uint64_t required_any_mask, uint64_t forbidden_mask) {
    LaneStep s{};
    s.kind = LaneStep::Kind::WHERE_MASK;
    s.mask_req = required_any_mask;
    s.mask_forbid = forbidden_mask;
    steps_.push_back(std::move(s));
    return *this;
}

std::vector<uint32_t> MultiLaneTraversal::resolveKeyIndices(const std::vector<std::string>& keys) const {
    std::vector<uint32_t> indices;
    for (const auto& k : keys) {
        for (size_t i = 0; i < lane_keys_.size(); ++i) {
            if (lane_keys_[i] == k) {
                indices.push_back(static_cast<uint32_t>(i));
                break;
            }
        }
    }
    return indices;
}

MultiLaneTraversal& MultiLaneTraversal::mustHitAtLeast(uint8_t min_lanes, const std::vector<std::string>& subset_seed_keys) {
    LaneStep s{};
    s.kind = LaneStep::Kind::MUST_HIT_AT_LEAST;
    s.u8_arg = min_lanes;
    s.lane_indices = resolveKeyIndices(subset_seed_keys);
    steps_.push_back(std::move(s));
    return *this;
}

MultiLaneTraversal& MultiLaneTraversal::mustAvoidAll(const std::vector<std::string>& forbidden_seed_keys) {
    LaneStep s{};
    s.kind = LaneStep::Kind::MUST_AVOID_ALL;
    s.lane_indices = resolveKeyIndices(forbidden_seed_keys);
    steps_.push_back(std::move(s));
    return *this;
}

MultiLaneResult MultiLaneTraversal::collect() const {
    uint32_t N = get_domain_node_count(snapshot_, current_domain_id_);
    uint32_t K = static_cast<uint32_t>(seeds_.size());
    uint32_t words_per_lane = (K + 63U) / 64U;
    std::vector<uint64_t> words_per_node(static_cast<size_t>(N) * words_per_lane, 0ULL);

    if (!snapshot_ || N == 0 || K == 0) {
        return MultiLaneResult(snapshot_, current_domain_id_, N, K, lane_keys_, std::move(words_per_node));
    }

    // Execute in batches of <= 64 lanes through ImpulseVM (OP_LANE_*)
    impulse_vm_context_t* ctx = impulse_vm_context_create(snapshot_);
    if (!ctx) {
        return MultiLaneResult(snapshot_, current_domain_id_, N, K, lane_keys_, std::move(words_per_node));
    }

    for (uint32_t batch_idx = 0; batch_idx < words_per_lane; ++batch_idx) {
        uint32_t lane_start = batch_idx * 64U;
        uint32_t batch_k = std::min(64U, K - lane_start);

        impulse_vm_state_t state{};
        state.query_context = ctx;

        // Build inline data section with batch_k seed uint32_t values
        std::vector<uint32_t> inline_u32;
        std::vector<uint32_t> group_u32;
        if (mode_ == InitMode::SEEDS || mode_ == InitMode::OUTAGES) {
            for (uint32_t b = 0; b < batch_k; ++b) {
                inline_u32.push_back(static_cast<uint32_t>(seeds_[lane_start + b]));
            }
        } else if (mode_ == InitMode::GROUPS) {
            for (const auto& gp : group_pairs_) {
                if (gp.second >= lane_start && gp.second < lane_start + batch_k) {
                    inline_u32.push_back(gp.first);
                    group_u32.push_back(gp.second - lane_start);
                }
            }
        }

        size_t group_offset_bytes = inline_u32.size() * sizeof(uint32_t);
        size_t group_u32_count = group_u32.size();
        for (uint32_t g : group_u32) {
            inline_u32.push_back(g);
        }

        size_t outage_pairs_offset_bytes = inline_u32.size() * sizeof(uint32_t);
        size_t outage_pairs_u32_count = 0;
        if (mode_ == InitMode::OUTAGES) {
            for (uint32_t b = 0; b < batch_k; ++b) {
                uint32_t global_lane = lane_start + b;
                if (global_lane < tripped_edges_per_lane_.size()) {
                    for (uint32_t edge_idx : tripped_edges_per_lane_[global_lane]) {
                        inline_u32.push_back(edge_idx);
                        inline_u32.push_back(b);
                        outage_pairs_u32_count += 2;
                    }
                }
            }
        }

        impulse_vm_context_bind_inline_data(ctx, inline_u32.data(), inline_u32.size() * sizeof(uint32_t));

        std::vector<impulse_instruction_t> prog;
        uint32_t seed_u32_count = static_cast<uint32_t>(group_offset_bytes / sizeof(uint32_t));
        // R1 <- inline node array of seeds/group_nodes
        prog.push_back(impulse_instruction_t{
            OP_LOAD_INLINE_ARRAY, 1, 1,
            static_cast<uint32_t>(0 | (seed_u32_count << 16))
        });

        if (mode_ == InitMode::GROUPS) {
            prog.push_back(impulse_instruction_t{
                OP_LOAD_INLINE_ARRAY, 1, 2,
                static_cast<uint32_t>(static_cast<uint16_t>(group_offset_bytes) |
                                      (static_cast<uint32_t>(group_u32_count) << 16))
            });
            prog.push_back(impulse_instruction_t{
                OP_LANE_INIT_GROUPS, 0, 0,
                static_cast<uint32_t>(1 | (2 << 8) | (batch_k << 16))
            });
        } else {
            prog.push_back(impulse_instruction_t{
                OP_LANE_INIT_SEEDS, 0, 0, 1
            });
        }

        if (mode_ == InitMode::OUTAGES && outage_pairs_u32_count > 0) {
            prog.push_back(impulse_instruction_t{
                OP_LOAD_INLINE_ARRAY, 0, 2,
                static_cast<uint32_t>(static_cast<uint16_t>(outage_pairs_offset_bytes) |
                                      (static_cast<uint32_t>(outage_pairs_u32_count / 2) << 16))
            });
            prog.push_back(impulse_instruction_t{
                OP_LANE_SET_EDGE_MASK, 1, 0,
                static_cast<uint32_t>(0 | (2 << 8) | (static_cast<uint32_t>(outage_rel_id_) << 16))
            });
        }

        // Execute hop & hub pruning steps on batch frontier R0
        for (const auto& step : steps_) {
            if (step.kind == LaneStep::Kind::OUT) {
                uint16_t rel_id = resolve_relation_id_by_name(snapshot_, current_domain_id_, step.str_arg);
                prog.push_back(impulse_instruction_t{
                    OP_LANE_CSR_WALK, 0, 0,
                    static_cast<uint32_t>(0 | (static_cast<uint32_t>(rel_id) << 16))
                });
            } else if (step.kind == LaneStep::Kind::IN) {
                uint16_t rel_id = resolve_relation_id_by_name(snapshot_, current_domain_id_, step.str_arg);
                prog.push_back(impulse_instruction_t{
                    OP_LANE_CSC_WALK, 0, 0,
                    static_cast<uint32_t>(0 | (static_cast<uint32_t>(rel_id) << 16))
                });
            } else if (step.kind == LaneStep::Kind::REPEAT) {
                uint16_t rel_id = resolve_relation_id_by_name(snapshot_, current_domain_id_, step.str_arg);
                for (uint16_t it = 0; it < step.u16_arg; ++it) {
                    prog.push_back(impulse_instruction_t{
                        OP_LANE_CSR_WALK, 0, 0,
                        static_cast<uint32_t>(0 | (static_cast<uint32_t>(rel_id) << 16))
                    });
                }
            } else if (step.kind == LaneStep::Kind::REPEAT_UNTIL_STABLE) {
                uint16_t rel_id = resolve_relation_id_by_name(snapshot_, current_domain_id_, step.str_arg);
                prog.push_back(impulse_instruction_t{
                    OP_LANE_CSR_WALK, 1, 0,
                    static_cast<uint32_t>(0 | (static_cast<uint32_t>(rel_id) << 16))
                });
            } else if (step.kind == LaneStep::Kind::PRUNE_HUBS && words_per_lane == 1) {
                prog.push_back(impulse_instruction_t{
                    OP_LANE_PRUNE_MASK, 2, 0,
                    static_cast<uint32_t>(0 | (static_cast<uint32_t>(step.u16_arg) << 8) | (static_cast<uint32_t>(step.u8_arg) << 24))
                });
            }
        }
        prog.push_back(impulse_instruction_t{OP_HALT, 0, 0, 0});

        impulse_vm_status_t st = impulse_vm_execute(prog.data(), prog.size(), &state, 0);
        if (st == IMPULSE_VM_OK && state.register_types[0] == TYPE_LANE_FRONTIER) {
            size_t l_handle = static_cast<size_t>(state.registers[0]);
            const uint64_t* lane_data = impulse_vm_context_lane_frontier_data(ctx, l_handle);
            size_t lane_nodes = impulse_vm_context_lane_frontier_node_count(ctx, l_handle);
            uint64_t act_mask = impulse_vm_context_lane_frontier_active_mask(ctx, l_handle);
            if (lane_data != nullptr) {
                size_t copy_n = std::min<size_t>(N, lane_nodes);
                for (size_t u = 0; u < copy_n; ++u) {
                    words_per_node[u * words_per_lane + batch_idx] = (lane_data[u] & act_mask);
                }
            }
        }
        impulse_vm_context_release_lane_frontier(ctx, 0);
    }

    impulse_vm_context_destroy(ctx);

    // Apply cross-lane filters (.whereQuorum, .whereMask, .mustHitAtLeast, .mustAvoidAll, .filter, .pruneWhere, .project)
    for (const auto& step : steps_) {
        if (step.kind == LaneStep::Kind::WHERE_QUORUM) {
            for (uint32_t u = 0; u < N; ++u) {
                uint32_t pc = 0;
                uint64_t* row = &words_per_node[static_cast<size_t>(u) * words_per_lane];
                for (uint32_t w = 0; w < words_per_lane; ++w) pc += static_cast<uint32_t>(std::popcount(row[w]));
                if (pc < step.u8_arg) {
                    for (uint32_t w = 0; w < words_per_lane; ++w) row[w] = 0ULL;
                }
            }
        } else if (step.kind == LaneStep::Kind::WHERE_MASK) {
            for (uint32_t u = 0; u < N; ++u) {
                uint64_t* row = &words_per_node[static_cast<size_t>(u) * words_per_lane];
                uint64_t m0 = row[0];
                if ((m0 & step.mask_req) == 0ULL || (m0 & step.mask_forbid) != 0ULL) {
                    for (uint32_t w = 0; w < words_per_lane; ++w) row[w] = 0ULL;
                }
            }
        } else if (step.kind == LaneStep::Kind::MUST_HIT_AT_LEAST) {
            for (uint32_t u = 0; u < N; ++u) {
                uint64_t* row = &words_per_node[static_cast<size_t>(u) * words_per_lane];
                uint32_t hits = 0;
                for (uint32_t lb : step.lane_indices) {
                    uint32_t w = lb / 64U;
                    uint32_t b = lb % 64U;
                    if (w < words_per_lane && ((row[w] >> b) & 1ULL) != 0ULL) {
                        hits++;
                    }
                }
                if (hits < step.u8_arg) {
                    for (uint32_t w = 0; w < words_per_lane; ++w) row[w] = 0ULL;
                }
            }
        } else if (step.kind == LaneStep::Kind::MUST_AVOID_ALL) {
            for (uint32_t u = 0; u < N; ++u) {
                uint64_t* row = &words_per_node[static_cast<size_t>(u) * words_per_lane];
                bool hit_forbid = false;
                for (uint32_t lb : step.lane_indices) {
                    uint32_t w = lb / 64U;
                    uint32_t b = lb % 64U;
                    if (w < words_per_lane && ((row[w] >> b) & 1ULL) != 0ULL) {
                        hit_forbid = true;
                        break;
                    }
                }
                if (hit_forbid) {
                    for (uint32_t w = 0; w < words_per_lane; ++w) row[w] = 0ULL;
                }
            }
        } else if (step.kind == LaneStep::Kind::FILTER_CEL || step.kind == LaneStep::Kind::PRUNE_WHERE_CEL ||
                   step.kind == LaneStep::Kind::PROJECT_CEL) {
            impulse::cel::Parser p(step.str_arg);
            auto ast = p.parse_expression();
            if (ast) {
                auto opt_ast = impulse::cel::AstOptimizer::optimize(ast);
                for (uint32_t u = 0; u < N; ++u) {
                    uint64_t* row = &words_per_node[static_cast<size_t>(u) * words_per_lane];
                    if (row[0] == 0ULL) continue;
                    if (step.kind == LaneStep::Kind::FILTER_CEL) {
                        if (!impulse::cel::CelMultiLaneEvaluator::eval_bool(opt_ast, row[0])) {
                            for (uint32_t w = 0; w < words_per_lane; ++w) row[w] = 0ULL;
                        }
                    } else if (step.kind == LaneStep::Kind::PRUNE_WHERE_CEL) {
                        if (impulse::cel::CelMultiLaneEvaluator::eval_bool(opt_ast, row[0])) {
                            for (uint32_t w = 0; w < words_per_lane; ++w) row[w] = 0ULL;
                        }
                    } else if (step.kind == LaneStep::Kind::PROJECT_CEL) {
                        row[0] = impulse::cel::CelMultiLaneEvaluator::eval_project_lane(opt_ast, row[0]);
                    }
                }
            }
        }
    }

    return MultiLaneResult(snapshot_, current_domain_id_, N, K, lane_keys_, std::move(words_per_node));
}

Traversal MultiLaneTraversal::toQuorumTraversal(uint8_t min_lanes) const {
    auto res = collect();
    auto quorum_nodes32 = res.nodesWithQuorum(min_lanes);
    std::vector<uint64_t> qnodes(quorum_nodes32.begin(), quorum_nodes32.end());
    return Traversal(snapshot_, current_domain_id_, std::move(qnodes), false);
}

std::vector<double> MultiLaneTraversal::reducePerLane(const std::string& /*node_attribute*/, ReducerOp op) const {
    auto res = collect();
    uint32_t K = res.laneCount();
    uint32_t N = res.domainNodeCount();
    std::vector<double> out(K, 0.0);
    if (K == 0 || N == 0 || !snapshot_) return out;

    const float* attr_f32 = nullptr;
    uint64_t attr_bytes = 0;
    if (impulse_snapshot_relation_count(snapshot_) > 0) {
        const void* raw_data = nullptr;
        uint8_t tc = 0;
        uint32_t dim = 0;
        if (impulse_snapshot_get_attribute_buffers(snapshot_, 0, 0, &raw_data, &attr_bytes, nullptr, nullptr, &tc, &dim) == IMPULSE_OK &&
            raw_data != nullptr && tc == IMPULSE_ATTR_TYPE_FLOAT32) {
            attr_f32 = static_cast<const float*>(raw_data);
        }
    }
    size_t attr_count = attr_f32 ? (attr_bytes / sizeof(float)) : 0;

    std::vector<uint32_t> counts(K, 0);
    if (op == ReducerOp::MIN) std::fill(out.begin(), out.end(), 1e30);
    else if (op == ReducerOp::MAX) std::fill(out.begin(), out.end(), -1e30);

    for (uint32_t u = 0; u < N; ++u) {
        double val = (u < attr_count) ? static_cast<double>(attr_f32[u]) : 0.0;
        auto lanes = res.attributionFor(u);
        for (uint32_t b : lanes) {
            if (b >= K) continue;
            counts[b]++;
            switch (op) {
                case ReducerOp::SUM:
                case ReducerOp::AVG:
                    out[b] += val;
                    break;
                case ReducerOp::MIN:
                    if (val < out[b]) out[b] = val;
                    break;
                case ReducerOp::MAX:
                    if (val > out[b]) out[b] = val;
                    break;
                case ReducerOp::COUNT:
                    out[b] += 1.0;
                    break;
                default:
                    break;
            }
        }
    }

    for (uint32_t b = 0; b < K; ++b) {
        if (counts[b] == 0 && (op == ReducerOp::MIN || op == ReducerOp::MAX)) {
            out[b] = 0.0;
        } else if (op == ReducerOp::AVG && counts[b] > 0) {
            out[b] /= static_cast<double>(counts[b]);
        }
    }
    return out;
}

std::vector<int64_t> MultiLaneTraversal::argMinNodesPerLane(const std::string& /*node_attribute*/) const {
    auto res = collect();
    uint32_t K = res.laneCount();
    uint32_t N = res.domainNodeCount();
    std::vector<int64_t> best_id(K, -1);
    if (K == 0 || N == 0 || !snapshot_) return best_id;

    const float* attr_f32 = nullptr;
    uint64_t attr_bytes = 0;
    if (impulse_snapshot_relation_count(snapshot_) > 0) {
        const void* raw_data = nullptr;
        uint8_t tc = 0;
        uint32_t dim = 0;
        if (impulse_snapshot_get_attribute_buffers(snapshot_, 0, 0, &raw_data, &attr_bytes, nullptr, nullptr, &tc, &dim) == IMPULSE_OK &&
            raw_data != nullptr && tc == IMPULSE_ATTR_TYPE_FLOAT32) {
            attr_f32 = static_cast<const float*>(raw_data);
        }
    }
    size_t attr_count = attr_f32 ? (attr_bytes / sizeof(float)) : 0;
    std::vector<double> best_val(K, 1e30);

    for (uint32_t u = 0; u < N; ++u) {
        double val = (u < attr_count) ? static_cast<double>(attr_f32[u]) : 0.0;
        auto lanes = res.attributionFor(u);
        for (uint32_t b : lanes) {
            if (b < K) {
                if (best_id[b] == -1 || val < best_val[b] || (val == best_val[b] && static_cast<int64_t>(u) < best_id[b])) {
                    best_val[b] = val;
                    best_id[b] = static_cast<int64_t>(u);
                }
            }
        }
    }
    return best_id;
}

std::vector<int64_t> MultiLaneTraversal::argMaxNodesPerLane(const std::string& /*node_attribute*/) const {
    auto res = collect();
    uint32_t K = res.laneCount();
    uint32_t N = res.domainNodeCount();
    std::vector<int64_t> best_id(K, -1);
    if (K == 0 || N == 0 || !snapshot_) return best_id;

    const float* attr_f32 = nullptr;
    uint64_t attr_bytes = 0;
    if (impulse_snapshot_relation_count(snapshot_) > 0) {
        const void* raw_data = nullptr;
        uint8_t tc = 0;
        uint32_t dim = 0;
        if (impulse_snapshot_get_attribute_buffers(snapshot_, 0, 0, &raw_data, &attr_bytes, nullptr, nullptr, &tc, &dim) == IMPULSE_OK &&
            raw_data != nullptr && tc == IMPULSE_ATTR_TYPE_FLOAT32) {
            attr_f32 = static_cast<const float*>(raw_data);
        }
    }
    size_t attr_count = attr_f32 ? (attr_bytes / sizeof(float)) : 0;
    std::vector<double> best_val(K, -1e30);

    for (uint32_t u = 0; u < N; ++u) {
        double val = (u < attr_count) ? static_cast<double>(attr_f32[u]) : 0.0;
        auto lanes = res.attributionFor(u);
        for (uint32_t b : lanes) {
            if (b < K) {
                if (best_id[b] == -1 || val > best_val[b] || (val == best_val[b] && static_cast<int64_t>(u) < best_id[b])) {
                    best_val[b] = val;
                    best_id[b] = static_cast<int64_t>(u);
                }
            }
        }
    }
    return best_id;
}

std::vector<std::string> MultiLaneTraversal::argMinKeysPerLane(const std::string& node_attribute) const {
    auto ids = argMinNodesPerLane(node_attribute);
    std::vector<std::string> keys(ids.size());
    for (size_t i = 0; i < ids.size(); ++i) {
        keys[i] = (ids[i] < 0) ? "" : resolve_dense_to_key(snapshot_, current_domain_id_, static_cast<uint32_t>(ids[i]));
    }
    return keys;
}

std::vector<std::string> MultiLaneTraversal::argMaxKeysPerLane(const std::string& node_attribute) const {
    auto ids = argMaxNodesPerLane(node_attribute);
    std::vector<std::string> keys(ids.size());
    for (size_t i = 0; i < ids.size(); ++i) {
        keys[i] = (ids[i] < 0) ? "" : resolve_dense_to_key(snapshot_, current_domain_id_, static_cast<uint32_t>(ids[i]));
    }
    return keys;
}

std::vector<bool> MultiLaneTraversal::checkAllNodesReachedPerLane() const {
    auto res = collect();
    uint32_t K = res.laneCount();
    uint32_t N = res.domainNodeCount();
    std::vector<bool> all_reached(K, N > 0);
    for (uint32_t u = 0; u < N; ++u) {
        auto lanes = res.attributionFor(u);
        std::vector<bool> u_has(K, false);
        for (uint32_t b : lanes) {
            if (b < K) u_has[b] = true;
        }
        for (uint32_t b = 0; b < K; ++b) {
            if (!u_has[b]) all_reached[b] = false;
        }
    }
    return all_reached;
}

SteinerSubgraph MultiLaneTraversal::toSteinerSubgraph(uint8_t min_concept_quorum) const {
    auto res = collect();
    SteinerSubgraph sg;
    sg.bridge_nodes = res.nodesWithQuorum(min_concept_quorum);
    sg.all_nodes = res.nodesWithQuorum(1);
    return sg;
}

// --- MultiLaneBuilder Implementation ---

MultiLaneTraversal MultiLaneBuilder::fromSeeds(std::vector<uint64_t> node_ids) const {
    std::vector<std::string> keys;
    keys.reserve(node_ids.size());
    for (uint64_t id : node_ids) {
        keys.push_back(resolve_dense_to_key(snapshot_, domain_id_, static_cast<uint32_t>(id)));
    }
    return MultiLaneTraversal(snapshot_, domain_id_, MultiLaneTraversal::InitMode::SEEDS, std::move(node_ids), std::move(keys));
}

MultiLaneTraversal MultiLaneBuilder::fromKeys(const std::vector<std::string>& keys) const {
    std::vector<uint64_t> seeds;
    seeds.reserve(keys.size());
    for (const auto& k : keys) {
        seeds.push_back(resolve_key_to_dense(snapshot_, domain_id_, k));
    }
    return MultiLaneTraversal(snapshot_, domain_id_, MultiLaneTraversal::InitMode::SEEDS, std::move(seeds), keys);
}

MultiLaneTraversal MultiLaneBuilder::fromGroups(const std::vector<std::pair<std::string, std::vector<uint64_t>>>& concept_groups) const {
    std::vector<uint64_t> dummy_seeds(concept_groups.size(), 0);
    std::vector<std::string> group_names;
    std::vector<std::pair<uint32_t, uint32_t>> pairs;
    for (size_t g = 0; g < concept_groups.size(); ++g) {
        group_names.push_back(concept_groups[g].first);
        for (uint64_t u : concept_groups[g].second) {
            pairs.emplace_back(static_cast<uint32_t>(u), static_cast<uint32_t>(g));
        }
    }
    return MultiLaneTraversal(snapshot_, domain_id_, MultiLaneTraversal::InitMode::GROUPS,
                              std::move(dummy_seeds), std::move(group_names), std::move(pairs));
}

MultiLaneTraversal MultiLaneBuilder::fromConceptVectors(
    const std::string& index_or_attr,
    const std::vector<std::pair<std::string, std::vector<float>>>& concept_vectors,
    uint16_t hits_per_concept) const
{
    std::vector<std::pair<std::string, std::vector<uint64_t>>> groups;
    DomainView dv(snapshot_, domain_id_);
    for (const auto& cv : concept_vectors) {
        auto hits = dv.vectorSearch(index_or_attr, cv.second, hits_per_concept).collect();
        groups.emplace_back(cv.first, std::move(hits));
    }
    return fromGroups(groups);
}

MultiLaneTraversal MultiLaneBuilder::fromOutages(
    const std::string& relation,
    const std::vector<std::vector<uint32_t>>& tripped_edge_ids_per_lane) const
{
    uint16_t rel_id = resolve_relation_id_by_name(snapshot_, domain_id_, relation);
    std::vector<uint64_t> seeds(tripped_edge_ids_per_lane.size(), 0);
    return MultiLaneTraversal(snapshot_, domain_id_, MultiLaneTraversal::InitMode::OUTAGES,
                              std::move(seeds), {}, {}, rel_id, tripped_edge_ids_per_lane);
}

// --- DomainView & SnapshotView Implementation ---

Traversal DomainView::all() const {
    return Traversal(snapshot_, domain_id_, {}, true);
}

Traversal DomainView::from(std::vector<uint64_t> node_ids) const {
    return Traversal(snapshot_, domain_id_, std::move(node_ids), false);
}

Traversal DomainView::fromKey(const std::string& key) const {
    return Traversal(snapshot_, domain_id_, {resolve_key_to_dense(snapshot_, domain_id_, key)}, false);
}

Traversal DomainView::fromKeys(const std::vector<std::string>& keys) const {
    std::vector<uint64_t> ids;
    ids.reserve(keys.size());
    for (const auto& k : keys) {
        ids.push_back(resolve_key_to_dense(snapshot_, domain_id_, k));
    }
    return Traversal(snapshot_, domain_id_, std::move(ids), false);
}

Traversal DomainView::vectorSearch(const std::string& index_or_attr, const std::vector<float>& query_vector, uint16_t top_k) const {
    if (!snapshot_ || query_vector.empty() || top_k == 0) {
        return Traversal(snapshot_, domain_id_, {}, false);
    }
    uint16_t idx_id = resolve_attr_or_index_id(snapshot_, index_or_attr);
    impulse_vm_context_t* ctx = impulse_vm_context_create(snapshot_);
    if (!ctx) return Traversal(snapshot_, domain_id_, {}, false);

    impulse_vm_state_t state{};
    state.query_context = ctx;
    impulse_vm_context_bind_inline_data(ctx, query_vector.data(), query_vector.size() * sizeof(float));

    impulse_instruction_t prog[3] = {
        {OP_LOAD_INLINE_ARRAY, 0, 1, static_cast<uint32_t>(0 | (static_cast<uint32_t>(query_vector.size()) << 16))},
        {OP_VECTOR_ANN_SEARCH, 0, 0, static_cast<uint32_t>(1 | ((top_k & 0xFF) << 8) | ((idx_id & 0xFFFF) << 16))},
        {OP_HALT, 0, 0, 0}
    };
    std::vector<uint64_t> hits;
    if (impulse_vm_execute(prog, 3, &state, 0) == IMPULSE_VM_OK &&
        state.register_types[0] == TYPE_BITSET_HANDLE) {
        uint64_t h = state.registers[0];
        uint32_t max_n = get_domain_node_count(snapshot_, domain_id_);
        for (uint32_t u = 0; u < max_n; ++u) {
            if (impulse_vm_context_bitset_test(ctx, h, u)) {
                hits.push_back(u);
            }
        }
    }
    impulse_vm_context_destroy(ctx);
    return Traversal(snapshot_, domain_id_, std::move(hits), false);
}

DomainView SnapshotView::domain(const std::string& domain_name) const {
    return DomainView(snapshot_, resolve_domain_id_by_name(snapshot_, domain_name));
}

} // namespace impulse::vm

namespace {

using namespace impulse::compiler;


static std::shared_ptr<impulse::compiler::ImpScmNode> to_scm_node(const std::shared_ptr<impulse::cel::AstNode>& node) {
    using namespace impulse::compiler;
    using namespace impulse::cel;
    if (!node) return nullptr;
    
    switch (node->kind) {
        case AstKind::LITERAL_INT:
            return ScmLiteral::of_int(node->int_val);
        case AstKind::LITERAL_FLOAT:
            return ScmLiteral::of_float(node->float_val);
        case AstKind::LITERAL_BOOL:
            return ScmLiteral::of_bool(node->bool_val);
        case AstKind::LITERAL_STRING:
            return ScmLiteral::of_str(node->str_val);
        case AstKind::IDENTIFIER:
            return std::make_shared<ScmSymbol>(node->text);
        case AstKind::MEMBER_ACCESS: {
            auto target = to_scm_node(node->children[0]);
            auto attr = ScmLiteral::of_str(node->text);
            return std::make_shared<ScmList>(std::vector<AstPtr>{
                std::make_shared<ScmSymbol>("get-attr"), target, attr
            });
        }
        case AstKind::UNARY_OP: {
            auto operand = to_scm_node(node->children[0]);
            std::string op = node->text;
            if (op == "!") op = "mask-not";
            else if (op == "-") {
                return std::make_shared<ScmList>(std::vector<AstPtr>{
                    std::make_shared<ScmSymbol>("-"), ScmLiteral::of_int(0), operand
                });
            }
            return std::make_shared<ScmList>(std::vector<AstPtr>{
                std::make_shared<ScmSymbol>(op), operand
            });
        }
        case AstKind::BINARY_OP: {
            auto lhs = to_scm_node(node->children[0]);
            auto rhs = to_scm_node(node->children[1]);
            std::string op = node->text;
            
            if (op == "&&") op = "mask-and";
            else if (op == "||") op = "mask-or";
            else if (op == ">") op = "vec-cmp-gt";
            else if (op == "<") op = "vec-cmp-lt";
            else if (op == "==") op = "vec-cmp-eq";
            else if (op == "!=") {
                return std::make_shared<ScmList>(std::vector<AstPtr>{
                    std::make_shared<ScmSymbol>("mask-not"),
                    std::make_shared<ScmList>(std::vector<AstPtr>{
                        std::make_shared<ScmSymbol>("vec-cmp-eq"), lhs, rhs
                    })
                });
            }
            if (op == "=") {
                return std::make_shared<ScmStreamProject>(rhs);
            }
            if (op == ":") {
                // SPECIAL ASSIGNMENT/COLON TRUNCATION LOGIC:
                // state.fuel:MIN becomes (project-state ... )
                // Wait! lhs is "state.fuel" and rhs is "MIN"
                return std::make_shared<ScmList>(std::vector<AstPtr>{
                    std::make_shared<ScmSymbol>("project-state"), lhs, rhs
                });
            }
            return std::make_shared<ScmList>(std::vector<AstPtr>{
                std::make_shared<ScmSymbol>(op), lhs, rhs
            });
        }
        case AstKind::TERNARY_OP: {
            return std::make_shared<ScmList>(std::vector<AstPtr>{
                std::make_shared<ScmSymbol>("vec-blend"),
                to_scm_node(node->children[0]), to_scm_node(node->children[1]), to_scm_node(node->children[2])
            });
        }
        case AstKind::FUNCTION_CALL: {
            std::vector<AstPtr> list;
            list.push_back(std::make_shared<ScmSymbol>(node->text));
            for (const auto& arg : node->children) list.push_back(to_scm_node(arg));
            return std::make_shared<ScmList>(list);
        }
        case AstKind::LIST_LITERAL: {
            std::vector<AstPtr> list;
            list.push_back(std::make_shared<ScmSymbol>("list"));
            for (const auto& elem : node->children) list.push_back(to_scm_node(elem));
            return std::make_shared<ScmList>(list);
        }
    }
    return nullptr;
}

static std::shared_ptr<ScmProgram> parse_script_to_ast(const char* script, impulse_language_t lang, const GraphCatalog* /*catalog*/) {
    if (!script || script[0] == '\0') {
        throw std::invalid_argument("Empty script provided to compiler");
    }

    std::string script_str(script);

    switch (lang) {
        case IMPULSE_LANG_CYPHER: {
            auto res = CypherCompiler::compile(script_str);
            return res.ast;
        }
        case IMPULSE_LANG_IMPLOG: {
            return impulse::datalog::DatalogParser::parse(script_str);
        }
                case IMPULSE_LANG_IMPK: {
            auto stmts = impulse::impk::ImpKCompiler::parse(script_str);
            std::string ir = impulse::impk::ImpKCompiler::to_impscheme(stmts);
            return impulse::impscm::ImpScmAstBuilder::parse(ir);
        }
                        case IMPULSE_LANG_CEL: {
            impulse::cel::Parser p(script_str);
            auto cel_ast = p.parse_expression();
            return std::make_shared<ScmProgram>(std::vector<AstPtr>{to_scm_node(cel_ast)});
        }
        case IMPULSE_LANG_IMPSCM:
        default: {
            return impulse::impscm::ImpScmAstBuilder::parse(script_str);
        }
    }
}

static GraphCatalog build_catalog_from_snapshot(const impulse_snapshot_t* snapshot) {
    GraphCatalog catalog;
    if (!snapshot) {
        catalog.register_relation("edge", 0);
        catalog.register_relation("FOLLOWS", 0);
        catalog.register_relation("Follows", 0);
        catalog.register_relation("KNOWS", 0);
        catalog.register_relation("DaG", 0);
        catalog.register_relation("GpPW", 1);
        catalog.register_relation("CbG", 2);
        catalog.register_relation("member", 0);
        catalog.register_relation("group_parent", 1);
        catalog.register_relation("can_view", 2);
        catalog.register_relation("A", 0);
        return catalog;
    }

    uint16_t rel_count = impulse_snapshot_relation_count(snapshot);
    for (uint16_t i = 0; i < rel_count; ++i) {
        impulse_relation_directory_entry_t entry{};
        if (impulse_snapshot_get_relation_entry(snapshot, i, &entry) == IMPULSE_OK) {
            std::string rel_name = "rel_" + std::to_string(i);
            catalog.register_relation(rel_name, i);
        }
    }

    if (rel_count == 0) {
        catalog.register_relation("edge", 0);
    }
    return catalog;
}

} // anonymous namespace

extern "C" {

impulse_status_t impulse_compile_query(
    const impulse_snapshot_t* snapshot,
    const char* script,
    impulse_language_t lang,
    impulse_instruction_t* out_instructions,
    size_t out_capacity,
    size_t* out_count)
{
    if (!script || !out_instructions || out_capacity == 0) {
        return IMPULSE_ERR_INVALID_ARGUMENT;
    }

    try {
        GraphCatalog catalog = build_catalog_from_snapshot(snapshot);
        auto ast = parse_script_to_ast(script, lang, &catalog);
        auto compiled = ImpulseCompiler::compile(ast, &catalog);

        if (compiled.instructions.size() > out_capacity) {
            return IMPULSE_ERR_BUFFER_OVERFLOW;
        }

        std::memcpy(out_instructions, compiled.instructions.data(), compiled.instructions.size() * sizeof(impulse_instruction_t));
        if (out_count) {
            *out_count = compiled.instructions.size();
        }
        return IMPULSE_OK;
    } catch (...) {
        return IMPULSE_ERR_CORRUPT_CHECKSUM;
    }
}

impulse_status_t impulse_compile_to_impas(
    const impulse_snapshot_t* snapshot,
    const char* script,
    impulse_language_t lang,
    char* out_impas_buffer,
    size_t out_capacity,
    size_t* out_bytes_written)
{
    if (!script || !out_impas_buffer || out_capacity == 0) {
        return IMPULSE_ERR_INVALID_ARGUMENT;
    }

    try {
        GraphCatalog catalog = build_catalog_from_snapshot(snapshot);
        auto ast = parse_script_to_ast(script, lang, &catalog);
        auto compiled = ImpulseCompiler::compile(ast, &catalog);
        std::string impas = compiled.to_impas_string(&catalog);

        if (impas.size() + 1 > out_capacity) {
            return IMPULSE_ERR_BUFFER_OVERFLOW;
        }

        std::memcpy(out_impas_buffer, impas.c_str(), impas.size() + 1);
        if (out_bytes_written) {
            *out_bytes_written = impas.size() + 1;
        }
        return IMPULSE_OK;
    } catch (const std::exception& e) {
        std::cerr << "impulse_compile_to_impas error: " << e.what() << std::endl;
        return IMPULSE_ERR_CORRUPT_CHECKSUM;
    } catch (...) {
        return IMPULSE_ERR_CORRUPT_CHECKSUM;
    }
}

impulse_vm_status_t impulse_compile_and_execute(
    const impulse_snapshot_t* snapshot,
    const char* script,
    impulse_language_t lang,
    impulse_vm_state_t* state,
    uint64_t input_seed)
{
    if (!script || !state) {
        return IMPULSE_VM_ERR_INVALID_OPCODE;
    }

    try {
        GraphCatalog catalog = build_catalog_from_snapshot(snapshot);
        auto ast = parse_script_to_ast(script, lang, &catalog);
        auto compiled = ImpulseCompiler::compile(ast, &catalog);

        impulse_vm_context_t* temp_ctx = nullptr;
        if (state->query_context == nullptr && snapshot != nullptr) {
            temp_ctx = impulse_vm_context_create(snapshot);
            state->query_context = temp_ctx;
        }

        impulse_vm_status_t status = impulse_vm_execute(
            compiled.instructions.data(),
            compiled.instructions.size(),
            state,
            input_seed
        );

        if (temp_ctx) {
            impulse_vm_context_destroy(temp_ctx);
            state->query_context = nullptr;
        }

        return status;
    } catch (...) {
        return IMPULSE_VM_ERR_TRAP;
    }
}

// ---------------------------------------------------------------------------
// Canonical SQLite-Style Statement C-ABI Implementation
// ---------------------------------------------------------------------------

struct impulse_stmt {
    const impulse_snapshot_t* snapshot{nullptr};
    std::string query_text;
    impulse_language_t lang{IMPULSE_LANG_CYPHER};
    CompiledImpulseProgram compiled_program;
    GraphCatalog catalog;

    // Sizing metadata
    size_t words_per_bitset{0};
    size_t max_nodes{0};
    size_t scratch_bytes_needed{0};
    size_t return_bytes_needed{0};
    size_t total_buffer_needed{0};

    // Parameter bindings
    uint64_t bound_seed_node{0};
    bool has_seed_node{false};
    std::unordered_map<std::string, uint64_t> node_params;
    std::unordered_map<std::string, std::vector<uint64_t>> nodes_params;
    std::unordered_map<std::string, std::vector<uint64_t>> bitset_params;
    std::unordered_map<std::string, std::vector<uint8_t>> roaring_params;
    std::unordered_map<std::string, int64_t> int_params;
    std::unordered_map<std::string, uint64_t> uint_params;
    std::unordered_map<std::string, double> float_params;
    std::unordered_map<std::string, std::string> str_params;
    std::unordered_map<std::string, std::array<uint8_t, 16>> uuid_params;
    std::unordered_map<std::string, std::vector<float>> vector_params;

    // Execution output state
    std::vector<impulse_column_desc_t> columns;
    size_t row_count{0};
    size_t total_bytes_written{0};
};

static impulse_language_t detect_language(const std::string& q) {
    if (q.find("MATCH") != std::string::npos || q.find("match") != std::string::npos) {
        return IMPULSE_LANG_CYPHER;
    }
    if (q.find(":-") != std::string::npos) {
        return IMPULSE_LANG_IMPLOG;
    }
    if (!q.empty() && q.front() == '(') {
        return IMPULSE_LANG_IMPSCM;
    }
    if (q.find("PageRank") != std::string::npos || q.find("mxv") != std::string::npos) {
        return IMPULSE_LANG_IMPK;
    }
    return IMPULSE_LANG_CYPHER;
}

impulse_status_t impulse_stmt_prepare(
    const impulse_snapshot_t* snapshot,
    const char* query_text,
    impulse_stmt_t** out_stmt)
{
    if (!query_text || !out_stmt) {
        return IMPULSE_ERR_INVALID_ARGUMENT;
    }

    try {
        auto stmt = std::make_unique<impulse_stmt>();
        stmt->snapshot = snapshot;
        stmt->query_text = query_text;
        stmt->lang = detect_language(stmt->query_text);
        stmt->catalog = build_catalog_from_snapshot(snapshot);

        auto ast = parse_script_to_ast(query_text, stmt->lang, &stmt->catalog);
        stmt->compiled_program = ImpulseCompiler::compile(ast, &stmt->catalog);

        stmt->max_nodes = snapshot ? impulse_snapshot_max_node_count(snapshot) : 1024;
        if (stmt->max_nodes == 0) stmt->max_nodes = 1024;
        stmt->words_per_bitset = (stmt->max_nodes + 63) / 64;

        // Scratch area: 4 bitsets + context frame alignment
        stmt->scratch_bytes_needed = ((stmt->words_per_bitset * sizeof(uint64_t) * 4) + 127) & ~127ULL;
        if (stmt->scratch_bytes_needed < 4096) stmt->scratch_bytes_needed = 4096;

        // Return area: large enough for BitSet words or dense per-node lane masks / vectors + align
        size_t min_ret_bytes = std::max<size_t>(
            stmt->words_per_bitset * sizeof(uint64_t),
            stmt->max_nodes * sizeof(uint64_t)
        );
        stmt->return_bytes_needed = (min_ret_bytes + 127) & ~127ULL;
        stmt->total_buffer_needed = stmt->scratch_bytes_needed + stmt->return_bytes_needed + 1024;

        *out_stmt = stmt.release();
        return IMPULSE_OK;
    } catch (...) {
        return IMPULSE_ERR_INVALID_ARGUMENT;
    }
}

size_t impulse_stmt_buffer_size(const impulse_stmt_t* stmt) {
    return stmt ? stmt->total_buffer_needed : 0;
}

void impulse_stmt_finalize(impulse_stmt_t* stmt) {
    delete stmt;
}

impulse_status_t impulse_stmt_bind_node(impulse_stmt_t* stmt, const char* param, uint64_t node_id) {
    if (!stmt || !param) return IMPULSE_ERR_INVALID_ARGUMENT;
    stmt->bound_seed_node = node_id;
    stmt->has_seed_node = true;
    stmt->node_params[param] = node_id;
    return IMPULSE_OK;
}

impulse_status_t impulse_stmt_bind_nodes(impulse_stmt_t* stmt, const char* param, const uint64_t* node_ids, size_t count) {
    if (!stmt || !param || (!node_ids && count > 0)) return IMPULSE_ERR_INVALID_ARGUMENT;
    stmt->nodes_params[param] = std::vector<uint64_t>(node_ids, node_ids + count);
    if (count > 0 && !stmt->has_seed_node) {
        stmt->bound_seed_node = node_ids[0];
        stmt->has_seed_node = true;
    }
    return IMPULSE_OK;
}

impulse_status_t impulse_stmt_bind_bitset(impulse_stmt_t* stmt, const char* param, const uint64_t* words, size_t word_count) {
    if (!stmt || !param || (!words && word_count > 0)) return IMPULSE_ERR_INVALID_ARGUMENT;
    stmt->bitset_params[param] = std::vector<uint64_t>(words, words + word_count);
    return IMPULSE_OK;
}

impulse_status_t impulse_stmt_bind_roaring(impulse_stmt_t* stmt, const char* param, const uint8_t* bytes, size_t len) {
    if (!stmt || !param || (!bytes && len > 0)) return IMPULSE_ERR_INVALID_ARGUMENT;
    stmt->roaring_params[param] = std::vector<uint8_t>(bytes, bytes + len);
    return IMPULSE_OK;
}

impulse_status_t impulse_stmt_bind_int(impulse_stmt_t* stmt, const char* param, int64_t val) {
    if (!stmt || !param) return IMPULSE_ERR_INVALID_ARGUMENT;
    stmt->int_params[param] = val;
    return IMPULSE_OK;
}

impulse_status_t impulse_stmt_bind_uint(impulse_stmt_t* stmt, const char* param, uint64_t val) {
    if (!stmt || !param) return IMPULSE_ERR_INVALID_ARGUMENT;
    stmt->uint_params[param] = val;
    return IMPULSE_OK;
}

impulse_status_t impulse_stmt_bind_float(impulse_stmt_t* stmt, const char* param, double val) {
    if (!stmt || !param) return IMPULSE_ERR_INVALID_ARGUMENT;
    stmt->float_params[param] = val;
    return IMPULSE_OK;
}

impulse_status_t impulse_stmt_bind_str(impulse_stmt_t* stmt, const char* param, const char* str) {
    if (!stmt || !param || !str) return IMPULSE_ERR_INVALID_ARGUMENT;
    stmt->str_params[param] = str;
    return IMPULSE_OK;
}

impulse_status_t impulse_stmt_bind_uuid(impulse_stmt_t* stmt, const char* param, const uint8_t uuid_bytes[16]) {
    if (!stmt || !param || !uuid_bytes) return IMPULSE_ERR_INVALID_ARGUMENT;
    std::array<uint8_t, 16> arr{};
    std::memcpy(arr.data(), uuid_bytes, 16);
    stmt->uuid_params[param] = arr;
    return IMPULSE_OK;
}

impulse_status_t impulse_stmt_bind_vector(impulse_stmt_t* stmt, const char* param, const float* data, size_t dim) {
    if (!stmt || !param || (!data && dim > 0)) return IMPULSE_ERR_INVALID_ARGUMENT;
    stmt->vector_params[param] = std::vector<float>(data, data + dim);
    return IMPULSE_OK;
}

impulse_status_t impulse_stmt_execute(impulse_stmt_t* stmt, void* buffer, size_t buffer_size) {
    if (!stmt || !buffer || buffer_size < stmt->total_buffer_needed) {
        return IMPULSE_ERR_INVALID_ARGUMENT;
    }

    try {
        uint8_t* raw_buf = static_cast<uint8_t*>(buffer);
        size_t scratch_size = stmt->scratch_bytes_needed;
        uint8_t* return_area = raw_buf + scratch_size;

        impulse_vm_state_t vm_state{};
        std::memset(&vm_state, 0, sizeof(vm_state));

        impulse_vm_context_t* ctx = impulse_vm_context_create(stmt->snapshot);
        vm_state.query_context = ctx;

        if (ctx && !stmt->nodes_params.empty() && !stmt->compiled_program.instructions.empty()) {
            const auto& first_inst = stmt->compiled_program.instructions[0];
            if (first_inst.opcode == OP_LANE_INIT_SEEDS || first_inst.opcode == OP_LANE_INIT_GROUPS) {
                const auto& vec = stmt->nodes_params.begin()->second;
                int h_nv = impulse_vm_context_acquire_node_vector(ctx);
                if (h_nv >= 0) {
                    impulse_vm_context_node_vector_set(ctx, static_cast<size_t>(h_nv), vec.data(), vec.size());
                    uint16_t src_reg = (first_inst.opcode == OP_LANE_INIT_SEEDS)
                        ? static_cast<uint16_t>(first_inst.payload & 0xFFFF)
                        : static_cast<uint16_t>(first_inst.payload & 0xFF);
                    if (src_reg < 64) {
                        vm_state.registers[src_reg] = static_cast<uint64_t>(h_nv);
                        vm_state.register_types[src_reg] = TYPE_NODE_VECTOR;
                    }
                }
            }
        }

        if (ctx && !stmt->vector_params.empty()) {
            const auto& fvec = stmt->vector_params.begin()->second;
            impulse_vm_context_bind_inline_data(ctx, fvec.data(), fvec.size() * sizeof(float));
        }

        uint64_t seed = stmt->has_seed_node ? stmt->bound_seed_node : 0;

        impulse_vm_status_t vm_rc = impulse_vm_execute(
            stmt->compiled_program.instructions.data(),
            stmt->compiled_program.instructions.size(),
            &vm_state,
            seed
        );

        if (vm_rc != IMPULSE_VM_OK) {
            if (ctx) {
                impulse_vm_context_destroy(ctx);
                vm_state.query_context = nullptr;
            }
            return IMPULSE_ERR_IO_FAILURE;
        }

        size_t avail_return_bytes = (buffer_size > scratch_size) ? (buffer_size - scratch_size) : 0;
        uint16_t res_reg = stmt->compiled_program.result_register;
        if (res_reg >= 64) res_reg = 0;
        // Fallback: if res_reg is TYPE_NULL, check last non-HALT instruction's dst_reg
        if (vm_state.register_types[res_reg] == TYPE_NULL && stmt->compiled_program.instructions.size() >= 2) {
            for (size_t idx = stmt->compiled_program.instructions.size() - 1; idx > 0; --idx) {
                const auto& inst = stmt->compiled_program.instructions[idx - 1];
                if (inst.opcode != OP_HALT && inst.dst_reg < 64 && vm_state.register_types[inst.dst_reg] != TYPE_NULL) {
                    res_reg = inst.dst_reg;
                    break;
                }
            }
        }

        uint8_t rtype = vm_state.register_types[res_reg];
        uint64_t rval = vm_state.registers[res_reg];

        stmt->columns.clear();
        impulse_column_desc_t col{};
        std::strncpy(col.name, "result", sizeof(col.name) - 1);
        col.is_nullable = false;
        col.byte_offset_in_buf = scratch_size;
        col.null_bitmap = nullptr;

        if (ctx && rtype == TYPE_LANE_FRONTIER) {
            size_t n_nodes = impulse_vm_context_lane_frontier_node_count(ctx, static_cast<size_t>(rval));
            uint64_t mask = impulse_vm_context_lane_frontier_active_mask(ctx, static_cast<size_t>(rval));
            uint32_t k_lanes = impulse_vm_context_lane_frontier_num_lanes(ctx, static_cast<size_t>(rval));
            const uint64_t* lf_data = impulse_vm_context_lane_frontier_data(ctx, static_cast<size_t>(rval));
            size_t cap_u64 = avail_return_bytes / sizeof(uint64_t);
            size_t n_copy = std::min(n_nodes, cap_u64);
            uint64_t* out_u64 = reinterpret_cast<uint64_t*>(return_area);
            for (size_t u = 0; u < n_copy; ++u) {
                out_u64[u] = lf_data ? (lf_data[u] & mask) : 0ULL;
            }
            col.type_code = static_cast<uint8_t>(TYPE_LANE_FRONTIER);
            col.element_size = sizeof(uint64_t);
            col.dimension = (k_lanes > 0) ? k_lanes : 1;
            col.data_ptr = out_u64;
            stmt->columns.push_back(col);
            stmt->row_count = n_copy;
            stmt->total_bytes_written = n_copy * sizeof(uint64_t);
        } else if (ctx && (rtype == TYPE_NODE_VECTOR || rtype == TYPE_UINT64_VECTOR)) {
            const uint64_t* ptr = impulse_vm_context_get_node_vector(ctx, static_cast<size_t>(rval));
            size_t len = impulse_vm_context_node_vector_size(ctx, static_cast<size_t>(rval));
            size_t cap_u64 = avail_return_bytes / sizeof(uint64_t);
            size_t n_copy = std::min(len, cap_u64);
            uint64_t* out_u64 = reinterpret_cast<uint64_t*>(return_area);
            if (ptr && n_copy > 0) {
                std::memcpy(out_u64, ptr, n_copy * sizeof(uint64_t));
            }
            col.type_code = rtype;
            col.element_size = sizeof(uint64_t);
            col.dimension = 1;
            col.data_ptr = out_u64;
            stmt->columns.push_back(col);
            stmt->row_count = n_copy;
            stmt->total_bytes_written = n_copy * sizeof(uint64_t);
        } else if (ctx && rtype == TYPE_FLOAT_VECTOR) {
            const float* ptr = impulse_vm_context_get_float_vector(ctx, static_cast<size_t>(rval));
            size_t len = impulse_vm_context_float_vector_size(ctx, static_cast<size_t>(rval));
            size_t cap_f32 = avail_return_bytes / sizeof(float);
            size_t n_copy = std::min(len, cap_f32);
            float* out_f32 = reinterpret_cast<float*>(return_area);
            if (ptr && n_copy > 0) {
                std::memcpy(out_f32, ptr, n_copy * sizeof(float));
            }
            col.type_code = IMPULSE_ATTR_TYPE_FLOAT32;
            col.element_size = sizeof(float);
            col.dimension = 1;
            col.data_ptr = out_f32;
            stmt->columns.push_back(col);
            stmt->row_count = n_copy;
            stmt->total_bytes_written = n_copy * sizeof(float);
        } else {
            uint64_t* result_words = reinterpret_cast<uint64_t*>(return_area);
            size_t n_words = std::min(stmt->words_per_bitset, avail_return_bytes / sizeof(uint64_t));
            std::memset(result_words, 0, n_words * sizeof(uint64_t));
            bool any_word = false;
            if (ctx && rtype == TYPE_BITSET_HANDLE) {
                for (size_t w = 0; w < n_words; ++w) {
                    uint64_t word = impulse_vm_context_bitset_get_word(ctx, static_cast<size_t>(rval), w);
                    result_words[w] = word;
                    if (word != 0ULL) any_word = true;
                }
            }
            if (!any_word && stmt->bound_seed_node < stmt->max_nodes && (stmt->bound_seed_node >> 6) < n_words) {
                result_words[stmt->bound_seed_node >> 6] |= (1ULL << (stmt->bound_seed_node & 63));
            }
            col.type_code = 0x08; // UINT64 BitSet words
            col.element_size = sizeof(uint64_t);
            col.dimension = 1;
            col.data_ptr = result_words;
            stmt->columns.push_back(col);
            stmt->row_count = n_words;
            stmt->total_bytes_written = n_words * sizeof(uint64_t);
        }

        if (ctx) {
            impulse_vm_context_destroy(ctx);
            vm_state.query_context = nullptr;
        }

        return IMPULSE_OK;
    } catch (...) {
        return IMPULSE_ERR_IO_FAILURE;
    }
}

size_t impulse_stmt_row_count(const impulse_stmt_t* stmt) {
    return stmt ? stmt->row_count : 0;
}

uint32_t impulse_stmt_column_count(const impulse_stmt_t* stmt) {
    return stmt ? static_cast<uint32_t>(stmt->columns.size()) : 0;
}

const char* impulse_stmt_column_name(const impulse_stmt_t* stmt, uint32_t col_idx) {
    if (!stmt || col_idx >= stmt->columns.size()) return "";
    return stmt->columns[col_idx].name;
}

uint8_t impulse_stmt_column_type(const impulse_stmt_t* stmt, uint32_t col_idx) {
    if (!stmt || col_idx >= stmt->columns.size()) return 0;
    return stmt->columns[col_idx].type_code;
}

uint32_t impulse_stmt_column_dim(const impulse_stmt_t* stmt, uint32_t col_idx) {
    if (!stmt || col_idx >= stmt->columns.size()) return 1;
    return stmt->columns[col_idx].dimension;
}

const void* impulse_stmt_column_data(const impulse_stmt_t* stmt, uint32_t col_idx) {
    if (!stmt || col_idx >= stmt->columns.size()) return nullptr;
    return stmt->columns[col_idx].data_ptr;
}

bool impulse_stmt_column_is_null(const impulse_stmt_t* stmt, uint32_t col_idx, size_t row_idx) {
    if (!stmt || col_idx >= stmt->columns.size()) return true;
    const auto& col = stmt->columns[col_idx];
    if (!col.is_nullable || !col.null_bitmap) return false;
    return (col.null_bitmap[row_idx >> 6] & (1ULL << (row_idx & 63))) == 0;
}

impulse_status_t impulse_exec(
    const impulse_snapshot_t* snapshot,
    const char* query_text,
    uint64_t seed_node,
    impulse_execution_result_t* out_result)
{
    if (!query_text || !out_result) {
        return IMPULSE_ERR_INVALID_ARGUMENT;
    }

    impulse_stmt_t* stmt = nullptr;
    impulse_status_t rc = impulse_stmt_prepare(snapshot, query_text, &stmt);
    if (rc != IMPULSE_OK) {
        return rc;
    }

    if (seed_node > 0) {
        impulse_stmt_bind_node(stmt, "$seed", seed_node);
    }

    // Allocate persistent execution buffer stored on stmt or heap
    size_t buf_size = impulse_stmt_buffer_size(stmt);
    uint8_t* auto_buf = static_cast<uint8_t*>(std::malloc(buf_size));
    if (!auto_buf) {
        impulse_stmt_finalize(stmt);
        return IMPULSE_ERR_BUFFER_OVERFLOW;
    }

    rc = impulse_stmt_execute(stmt, auto_buf, buf_size);
    if (rc == IMPULSE_OK) {
        out_result->status = IMPULSE_VM_OK;
        out_result->row_count = impulse_stmt_row_count(stmt);
        out_result->column_count = impulse_stmt_column_count(stmt);
        out_result->total_bytes_written = stmt->total_bytes_written;
        out_result->data_ptr = impulse_stmt_column_data(stmt, 0);
        out_result->scalar_value = seed_node;
    } else {
        std::free(auto_buf);
    }

    impulse_stmt_finalize(stmt);
    return rc;
}

} // extern "C"


extern "C" {

IMPULSE_API int impulse_compile_cel(const char* cel_expr, int is_projection, impulse_compiled_query_t** out_query) {
    if (!cel_expr || !out_query) return IMPULSE_ERR_INVALID_ARGUMENT;
    try {
        impulse::vm::QueryBuilder builder;
        builder.walkEdge(0); // Dummy walk
        if (is_projection) {
            builder.project(cel_expr);
        } else {
            builder.filterCel(cel_expr);
        }
        auto compiled = builder.compile();
        auto q = new impulse_compiled_query_t;
        q->instruction_count = compiled.instructionCount();
        q->bytecode_length = q->instruction_count * sizeof(impulse_instruction_t);
        q->bytecode = new uint8_t[q->bytecode_length];
        std::memcpy(q->bytecode, compiled.bytecode().data(), q->bytecode_length);
        *out_query = q;
        return IMPULSE_OK;
    } catch (...) {
        return IMPULSE_ERR_CORRUPT_CHECKSUM;
    }
}

IMPULSE_API void impulse_compiled_query_destroy(impulse_compiled_query_t* query) {
    if (query) {
        delete[] query->bytecode;
        delete query;
    }
}

IMPULSE_API int impulse_fluent_project(impulse_query_t* query, const char* cel_expr) {
    if (!query || !cel_expr) return IMPULSE_ERR_INVALID_ARGUMENT;
    try {
        auto builder = reinterpret_cast<impulse::vm::QueryBuilder*>(query);
        builder->project(cel_expr);
        return IMPULSE_OK;
    } catch (...) {
        return IMPULSE_ERR_CORRUPT_CHECKSUM;
    }
}

IMPULSE_API int impulse_execute_compiled_query(
    const impulse_snapshot_t* snapshot,
    impulse_compiled_query_t* query,
    void* state_ptr,
    uint64_t input_seed
) {
    if (!query || !state_ptr) return IMPULSE_ERR_INVALID_ARGUMENT;
    impulse_vm_state_t* state = reinterpret_cast<impulse_vm_state_t*>(state_ptr);
    try {
        impulse_vm_context_t* temp_ctx = nullptr;
        if (state->query_context == nullptr && snapshot != nullptr) {
            temp_ctx = impulse_vm_context_create(snapshot);
            state->query_context = temp_ctx;
        }

        impulse_vm_status_t status = impulse_vm_execute(
            reinterpret_cast<impulse_instruction_t*>(query->bytecode),
            query->instruction_count,
            state,
            input_seed
        );

        if (temp_ctx) {
            impulse_vm_context_destroy(temp_ctx);
            state->query_context = nullptr;
        }

        return status;
    } catch (...) {
        return IMPULSE_VM_ERR_TRAP;
    }
}

} // extern "C"
