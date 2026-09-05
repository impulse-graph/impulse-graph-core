#ifndef IMPULSE_VM_FLUENT_HPP
#define IMPULSE_VM_FLUENT_HPP

#include "impulse_vm.h"
#include "impulse_graph.h"

#include <vector>
#include <array>
#include <string>
#include <functional>
#include <memory>
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <stdexcept>

namespace impulse::vm {

/**
 * @brief Type-safe result wrapper for VM execution output.
 */
struct QueryResult {
    impulse_vm_status_t status{IMPULSE_VM_OK};
    uint16_t result_register{0};
    impulse_register_type_t result_type{TYPE_NULL};
    uint64_t raw_value{0};

    [[nodiscard]] bool isOk() const { return status == IMPULSE_VM_OK; }

    [[nodiscard]] uint64_t asInt() const { return raw_value; }

    [[nodiscard]] float asFloat() const {
        float f{0.0f};
        static_assert(sizeof(float) == sizeof(uint32_t));
        auto val32 = static_cast<uint32_t>(raw_value);
        std::memcpy(&f, &val32, sizeof(float));
        return f;
    }

    [[nodiscard]] double asDouble() const {
        double d{0.0};
        std::memcpy(&d, &raw_value, sizeof(double));
        return d;
    }

    /**
     * @brief Test membership of node_id in output bitset if result is a bitset handle.
     */
    [[nodiscard]] bool testBitset(const impulse_vm_context_t* ctx, uint64_t node_id) const {
        if (!ctx || result_type != TYPE_BITSET_HANDLE) {
            return false;
        }
        return impulse_vm_context_bitset_test(ctx, raw_value, node_id);
    }
};

/**
 * @brief Represents an immutable compiled VM query ready for execution.
 */
class CompiledQuery {
public:
    CompiledQuery(std::vector<impulse_instruction_t> instructions, uint16_t result_reg)
        : instructions_(std::move(instructions)), result_register_(result_reg) {}

    [[nodiscard]] const std::vector<impulse_instruction_t>& bytecode() const { return instructions_; }
    [[nodiscard]] uint16_t resultRegister() const { return result_register_; }
    [[nodiscard]] size_t instructionCount() const { return instructions_.size(); }

    /**
     * @brief Execute query on snapshot with new context lifecycle.
     */
    QueryResult execute(const impulse_snapshot_t* snapshot, uint64_t input_param = 0, impulse_vm_context_t** out_context = nullptr) const;

    /**
     * @brief Execute query using existing context and state.
     */
    QueryResult executeWithContext(impulse_vm_context_t* ctx, impulse_vm_state_t* state, uint64_t input_param = 0) const;

private:
    std::vector<impulse_instruction_t> instructions_;
    uint16_t result_register_;
};

/**
 * @brief Fluent style builder for building Impulse VM query bytecodes.
 */
class QueryBuilder {
public:
    QueryBuilder();
    explicit QueryBuilder(uint16_t start_register);

    // --- Inputs & Constants ---
    QueryBuilder& inputNode(uint16_t dst_reg = 0);
    QueryBuilder& inputSet(uint16_t dst_reg = 0);
    QueryBuilder& loadConstInt(int64_t value, uint16_t dst_reg = 0);
    QueryBuilder& loadConstFloat(float value, uint16_t dst_reg = 0);
    QueryBuilder& loadConstStrPrefix(const char* prefix, uint16_t dst_reg = 0);
    QueryBuilder& loadKeys(const char** keys, size_t count, uint16_t dst_reg = 0);

    // --- Graph Walks & Filters ---
    QueryBuilder& walkEdge(uint16_t relation_id, uint8_t flags = 0);
    QueryBuilder& walkEdgeFiltered(uint16_t relation_id, uint32_t filter_id);
    QueryBuilder& walkEdgePredicate(uint16_t relation_id, uint32_t filter_id);
    QueryBuilder& walkDegree(uint16_t relation_id);
    QueryBuilder& walkReduceSum(uint16_t relation_id, uint16_t val_reg);
    QueryBuilder& walkCsc(uint16_t relation_id);
    QueryBuilder& filterNode(uint32_t filter_id);
    QueryBuilder& filterNodeStrPrefix(const char* prefix);
    QueryBuilder& filterCel(const std::string& expression);
    QueryBuilder& project(const std::string& expression);

    // --- Set & Vector Operations ---
    QueryBuilder& unionWith(uint16_t src_reg);
    QueryBuilder& intersectWith(uint16_t src_reg);
    QueryBuilder& differenceWith(uint16_t src_reg);
    QueryBuilder& cardinality();
    QueryBuilder& vectorMulAttr(uint16_t attr_reg);
    QueryBuilder& vectorReduceSum();
    QueryBuilder& vectorDiv(uint16_t denom_reg);
    QueryBuilder& l1NormDiff(uint16_t other_reg);

    // --- GraphBLAS & Linear Algebra ---
    QueryBuilder& matrixVectorMul(uint16_t matrix_reg, uint8_t semiring_id = SEMIRING_PLUS_TIMES);
    QueryBuilder& vectorMatrixMul(uint16_t matrix_reg, uint8_t semiring_id = SEMIRING_PLUS_TIMES);
    QueryBuilder& ewiseAdd(uint16_t other_reg, uint8_t binary_op = BINARY_OP_ADD);
    QueryBuilder& ewiseMult(uint16_t other_reg, uint8_t binary_op = BINARY_OP_MUL);
    QueryBuilder& reduce(uint8_t binary_op = BINARY_OP_ADD);

    // --- Graph Analytics & Extended Domain ---
    QueryBuilder& afforest();
    QueryBuilder& tcSweepBatch();
    QueryBuilder& brandesForward();
    QueryBuilder& brandesBackward();
    QueryBuilder& deltaStepRelax(uint16_t weight_reg);
    QueryBuilder& sampleNeighbors(uint16_t relation_id, int32_t k_samples, uint32_t seed = 0);
    QueryBuilder& randomWalk(uint16_t relation_id, int32_t steps, uint32_t seed = 0);
    QueryBuilder& scatterGather();
    QueryBuilder& rebacCheck(uint32_t permission_id);
    QueryBuilder& roaringBitmapAnd(uint16_t other_reg);
    QueryBuilder& islandDetect(uint16_t secondary_reg);
    QueryBuilder& sparseMatVec();
    QueryBuilder& louvainModularity();
    QueryBuilder& kcoreDecomposition();
    QueryBuilder& motifMatch3();
    QueryBuilder& graphIsomorphism();

    // --- Register Control & Movement ---
    QueryBuilder& mov(uint16_t dst_reg, uint16_t src_reg);
    QueryBuilder& clearReg(uint16_t reg);
    QueryBuilder& loadIndirect(uint16_t dst_reg, uint16_t src_param, uint16_t index_reg = 0, uint8_t flags = 0);

    // --- Inline Data & Error Handling ---
    QueryBuilder& loadInlineArray(uint16_t dst_reg, uint16_t offset_bytes, uint16_t count);
    QueryBuilder& initMockGraph(uint16_t slot_id, uint16_t offset_bytes, uint16_t node_count);
    QueryBuilder& throwErr(uint32_t user_error_code);
    QueryBuilder& assertVal(uint16_t src_reg, uint32_t expected_val, uint8_t flags = 0);
    QueryBuilder& trap(uint32_t signal_id = 0);
    QueryBuilder& nop();

    // --- Control Flow ---
    QueryBuilder& repeat(int count, const std::function<void(QueryBuilder&)>& body);
    QueryBuilder& repeatUntilStable(const std::function<void(QueryBuilder&)>& body);
    QueryBuilder& jmp(int32_t instruction_offset);
    QueryBuilder& jz(int32_t instruction_offset);
    QueryBuilder& jnz(int32_t instruction_offset);

    // --- Terminal Collect Operators ---
    QueryBuilder& collectBitset();
    QueryBuilder& collectArray();
    QueryBuilder& mapDenseToKeys();
    QueryBuilder& collectValueMap();

    // --- Inspection & Compilation ---
    [[nodiscard]] uint16_t currentRegister() const { return current_reg_; }
    void setCurrentRegister(uint16_t reg) { current_reg_ = reg; }
    uint16_t allocateRegister();

    [[nodiscard]] const std::vector<impulse_instruction_t>& rawInstructions() const { return instructions_; }

    CompiledQuery compile();

private:
    void emit(uint8_t opcode, uint8_t flags, uint16_t dst_reg, uint32_t payload);

    std::vector<impulse_instruction_t> instructions_;
    uint16_t current_reg_{0};
    uint16_t next_alloc_reg_{1};
};

} // namespace impulse::vm

#endif // IMPULSE_VM_FLUENT_HPP
