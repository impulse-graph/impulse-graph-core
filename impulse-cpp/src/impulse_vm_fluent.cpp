#include "impulse_vm_fluent.hpp"

#include <stdexcept>
#include <cstring>

namespace impulse::vm {

QueryResult CompiledQuery::executeWithContext(
    impulse_vm_context_t* ctx,
    impulse_vm_state_t* state,
    uint64_t input_param
) const {
    if (!state) {
        return QueryResult{IMPULSE_VM_ERR_NULL_SNAPSHOT, 0, TYPE_NULL, 0};
    }
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
    if (reg >= 64) {
        throw std::length_error("QueryBuilder exceeded maximum available VM registers (64)");
    }
    return reg;
}

void QueryBuilder::emit(uint8_t opcode, uint8_t flags, uint16_t dst_reg, uint32_t payload) {
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
    uint32_t payload = (static_cast<uint32_t>(relation_id) << 16) | (src_reg & 0xFFFF);
    emit(OP_CSR_WALK_FILTERED, 0, filter_id, payload);
    return *this;
}

QueryBuilder& QueryBuilder::walkEdgePredicate(uint16_t relation_id, uint32_t filter_id) {
    uint16_t src_reg = current_reg_;
    uint32_t payload = (static_cast<uint32_t>(relation_id) << 16) | (src_reg & 0xFFFF);
    emit(OP_CSR_WALK_PREDICATE, 0, filter_id, payload);
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
    emit(OP_NODE_FILTER, 0, current_reg_, (filter_id << 8) | (src_reg & 0xFF));
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

// --- Set & Vector Operations ---

QueryBuilder& QueryBuilder::unionWith(uint16_t src_reg) {
    emit(OP_SET_UNION, 0, current_reg_, src_reg);
    return *this;
}

QueryBuilder& QueryBuilder::intersectWith(uint16_t src_reg) {
    emit(OP_SET_INTERSECT, 0, current_reg_, src_reg);
    return *this;
}

QueryBuilder& QueryBuilder::differenceWith(uint16_t src_reg) {
    emit(OP_SET_DIFFERENCE, 0, current_reg_, src_reg);
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
    uint16_t counter_reg = allocateRegister();
    loadConstInt(count, counter_reg);

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

CompiledQuery QueryBuilder::compile() {
    std::vector<impulse_instruction_t> compiled = instructions_;
    compiled.push_back(impulse_instruction_t{OP_HALT, 0, 0, 0});
    return CompiledQuery(std::move(compiled), current_reg_);
}

} // namespace impulse::vm
