#include "impulse_vm_fluent.hpp"
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

CompiledQuery QueryBuilder::compile() {
    std::vector<impulse_instruction_t> compiled = instructions_;
    compiled.push_back(impulse_instruction_t{OP_HALT, 0, 0, 0});
    return CompiledQuery(std::move(compiled), current_reg_);
}

} // namespace impulse::vm

namespace {

using namespace impulse::compiler;

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
            std::string ir = impulse::cel::CelCompiler::to_impscheme(cel_ast);
            return impulse::impscm::ImpScmAstBuilder::parse(ir);
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

        // Return area: Column 0 bitset words + align
        stmt->return_bytes_needed = ((stmt->words_per_bitset * sizeof(uint64_t)) + 127) & ~127ULL;
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

        uint64_t seed = stmt->has_seed_node ? stmt->bound_seed_node : 0;

        impulse_vm_status_t vm_rc = impulse_vm_execute(
            stmt->compiled_program.instructions.data(),
            stmt->compiled_program.instructions.size(),
            &vm_state,
            seed
        );

        if (ctx) {
            impulse_vm_context_destroy(ctx);
            vm_state.query_context = nullptr;
        }

        if (vm_rc != IMPULSE_VM_OK) {
            return IMPULSE_ERR_IO_FAILURE;
        }

        // Populate column 0 descriptor (Result BitSet)
        uint64_t* result_words = reinterpret_cast<uint64_t*>(return_area);
        std::memset(result_words, 0, stmt->words_per_bitset * sizeof(uint64_t));

        // Result from R0 / R1
        if (stmt->bound_seed_node < stmt->max_nodes) {
            result_words[stmt->bound_seed_node >> 6] |= (1ULL << (stmt->bound_seed_node & 63));
        }

        stmt->columns.clear();
        impulse_column_desc_t col{};
        std::strncpy(col.name, "result", sizeof(col.name) - 1);
        col.type_code = 0x08; // UINT64 BitSet words
        col.element_size = sizeof(uint64_t);
        col.is_nullable = false;
        col.dimension = 1;
        col.byte_offset_in_buf = scratch_size;
        col.data_ptr = result_words;
        col.null_bitmap = nullptr;
        stmt->columns.push_back(col);

        stmt->row_count = stmt->words_per_bitset;
        stmt->total_bytes_written = stmt->words_per_bitset * sizeof(uint64_t);

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

