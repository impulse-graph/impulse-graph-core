#ifndef IMPULSE_VM_FLUENT_HPP
#define IMPULSE_VM_FLUENT_HPP

#include "impulse_vm.h"
#include "impulse_graph.h"

#ifndef IMPULSE_ATTR_TYPE_FLOAT32
#define IMPULSE_ATTR_TYPE_FLOAT32 0x05
#endif

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

    [[nodiscard]] bool isBitsetHandle() const { return result_type == TYPE_BITSET_HANDLE; }
    [[nodiscard]] bool isLaneFrontier() const { return result_type == TYPE_LANE_FRONTIER; }
    [[nodiscard]] bool isNodeVector() const { return result_type == TYPE_NODE_VECTOR || result_type == TYPE_UINT64_VECTOR; }
    [[nodiscard]] bool isFloatVector() const { return result_type == TYPE_FLOAT_VECTOR; }

    /**
     * @brief Extract dense lane-mask array (size N, masked by active_mask) when result is TYPE_LANE_FRONTIER.
     */
    [[nodiscard]] std::vector<uint64_t> extractLaneMasks(const impulse_vm_context_t* ctx) const {
        if (!ctx || result_type != TYPE_LANE_FRONTIER) {
            return {};
        }
        size_t n = impulse_vm_context_lane_frontier_node_count(ctx, raw_value);
        uint64_t mask = impulse_vm_context_lane_frontier_active_mask(ctx, raw_value);
        const uint64_t* data = impulse_vm_context_lane_frontier_data(ctx, raw_value);
        if (!data || n == 0) {
            return {};
        }
        std::vector<uint64_t> out(n);
        for (size_t u = 0; u < n; ++u) {
            out[u] = data[u] & mask;
        }
        return out;
    }

    /**
     * @brief Extract sorted active node IDs for a specific lane index (0..63) when result is TYPE_LANE_FRONTIER.
     */
    [[nodiscard]] std::vector<uint64_t> extractLaneNodes(const impulse_vm_context_t* ctx, uint32_t lane_idx) const {
        if (!ctx || result_type != TYPE_LANE_FRONTIER || lane_idx >= 64) {
            return {};
        }
        size_t n = impulse_vm_context_lane_frontier_node_count(ctx, raw_value);
        uint64_t mask = impulse_vm_context_lane_frontier_active_mask(ctx, raw_value);
        uint64_t bit = (1ULL << lane_idx) & mask;
        const uint64_t* data = impulse_vm_context_lane_frontier_data(ctx, raw_value);
        if (!data || n == 0 || bit == 0ULL) {
            return {};
        }
        std::vector<uint64_t> out;
        for (size_t u = 0; u < n; ++u) {
            if ((data[u] & bit) != 0ULL) {
                out.push_back(static_cast<uint64_t>(u));
            }
        }
        return out;
    }

    /**
     * @brief Extract all active node IDs (nodes with at least one active lane bit) from TYPE_LANE_FRONTIER.
     */
    [[nodiscard]] std::vector<uint64_t> extractActiveLaneNodes(const impulse_vm_context_t* ctx) const {
        if (!ctx || result_type != TYPE_LANE_FRONTIER) {
            return {};
        }
        size_t n = impulse_vm_context_lane_frontier_node_count(ctx, raw_value);
        uint64_t mask = impulse_vm_context_lane_frontier_active_mask(ctx, raw_value);
        const uint64_t* data = impulse_vm_context_lane_frontier_data(ctx, raw_value);
        if (!data || n == 0 || mask == 0ULL) {
            return {};
        }
        std::vector<uint64_t> out;
        for (size_t u = 0; u < n; ++u) {
            if ((data[u] & mask) != 0ULL) {
                out.push_back(static_cast<uint64_t>(u));
            }
        }
        return out;
    }

    /**
     * @brief Extract uint64_t elements when result is TYPE_NODE_VECTOR or TYPE_UINT64_VECTOR.
     */
    [[nodiscard]] std::vector<uint64_t> extractNodeVector(const impulse_vm_context_t* ctx) const {
        if (!ctx || (result_type != TYPE_NODE_VECTOR && result_type != TYPE_UINT64_VECTOR)) {
            return {};
        }
        const uint64_t* ptr = impulse_vm_context_get_node_vector(ctx, raw_value);
        size_t len = impulse_vm_context_node_vector_size(ctx, raw_value);
        if (!ptr || len == 0) {
            return {};
        }
        return std::vector<uint64_t>(ptr, ptr + len);
    }

    /**
     * @brief Extract float32 elements when result is TYPE_FLOAT_VECTOR.
     */
    [[nodiscard]] std::vector<float> extractFloatVector(const impulse_vm_context_t* ctx) const {
        if (!ctx || result_type != TYPE_FLOAT_VECTOR) {
            return {};
        }
        const float* ptr = impulse_vm_context_get_float_vector(ctx, raw_value);
        size_t len = impulse_vm_context_float_vector_size(ctx, raw_value);
        if (!ptr || len == 0) {
            return {};
        }
        return std::vector<float>(ptr, ptr + len);
    }

    /**
     * @brief Polymorphic active-node extraction across TYPE_BITSET_HANDLE, TYPE_LANE_FRONTIER,
     * TYPE_NODE_VECTOR / TYPE_UINT64_VECTOR, and scalar TYPE_NODE_ID / TYPE_INT64.
     */
    [[nodiscard]] std::vector<uint64_t> extractActiveNodes(const impulse_vm_context_t* ctx) const {
        if (!ctx) return {};
        if (result_type == TYPE_BITSET_HANDLE) {
            std::vector<uint64_t> out;
            size_t max_n = impulse_vm_context_get_vector_size(ctx);
            size_t num_words = (max_n + 63) / 64;
            for (size_t w = 0; w < num_words; ++w) {
                uint64_t word = impulse_vm_context_bitset_get_word(ctx, raw_value, w);
                if (word == 0ULL) continue;
                for (uint32_t b = 0; b < 64; ++b) {
                    if ((word & (1ULL << b)) != 0ULL) {
                        uint64_t u = w * 64 + b;
                        if (u < max_n) out.push_back(u);
                    }
                }
            }
            return out;
        }
        if (result_type == TYPE_LANE_FRONTIER) {
            return extractActiveLaneNodes(ctx);
        }
        if (result_type == TYPE_NODE_VECTOR || result_type == TYPE_UINT64_VECTOR) {
            return extractNodeVector(ctx);
        }
        if (result_type == TYPE_NODE_ID || result_type == TYPE_INT64) {
            return {raw_value};
        }
        return {};
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
    QueryBuilder& walkEdge2Hop(uint16_t rel1_id, uint16_t rel2_id, uint8_t flags = 0);
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
    QueryBuilder& floatVectorScale(float scale_factor);
    QueryBuilder& vectorReduceSum();
    QueryBuilder& vectorDiv(uint16_t denom_reg);
    QueryBuilder& l1NormDiff(uint16_t other_reg);
    QueryBuilder& vectorTimeValidAt(uint16_t ts_reg, uint16_t start_attr_reg = 1, uint16_t end_attr_reg = 2);

    // --- GraphBLAS & Linear Algebra ---
    QueryBuilder& matrixVectorMul(uint16_t matrix_reg, uint8_t semiring_id = SEMIRING_PLUS_TIMES);
    QueryBuilder& vectorMatrixMul(uint16_t matrix_reg, uint8_t semiring_id = SEMIRING_PLUS_TIMES);
    QueryBuilder& ewiseAdd(uint16_t other_reg, uint8_t binary_op = BINARY_OP_ADD);
    QueryBuilder& ewiseMult(uint16_t other_reg, uint8_t binary_op = BINARY_OP_MUL);
    QueryBuilder& reduce(uint8_t binary_op = BINARY_OP_ADD);
    QueryBuilder& reduceMin();
    QueryBuilder& reduceMax();
    QueryBuilder& argMin();
    QueryBuilder& argMax();

    // --- Multi-Lane Bit-Sliced Execution (0xC0..0xC9) & Hybrid Vector Search (0xCA..0xCB) ---
    QueryBuilder& laneInitSeeds(uint16_t seed_reg, uint8_t width_flag = 0);
    QueryBuilder& laneInitGroups(uint16_t pairs_reg, uint16_t num_groups);
    QueryBuilder& laneSetEdgeMask(uint16_t pairs_reg, uint16_t relation_id);
    QueryBuilder& laneWalkCsr(uint16_t relation_id, bool fixed_point = false);
    QueryBuilder& laneWalkCsc(uint16_t relation_id, bool fixed_point = false);
    QueryBuilder& lanePruneMask(uint8_t mode, uint16_t arg0, uint16_t arg1 = 0);
    QueryBuilder& laneFilterQuorum(uint16_t min_quorum, uint16_t mask_reg = 0xFF);
    QueryBuilder& laneReduceAnd();
    QueryBuilder& laneReduceAttr(uint16_t attr_id, uint8_t reduce_op = 0);
    QueryBuilder& laneSetCover(uint16_t mask_reg = 0xFF, uint8_t mode = 0);
    QueryBuilder& vectorAnnSearch(uint16_t query_reg, uint16_t top_k, uint16_t index_or_attr_id = 0, bool fused_lane_frontier = false);
    QueryBuilder& vectorRerankDot(uint16_t query_reg, uint16_t attr_id = 0, uint8_t metric = 0);

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

// ---------------------------------------------------------------------------
// High-Level Domain-Bound C++20 Fluent API (Single-Lane & Multi-Lane)
// ---------------------------------------------------------------------------

enum class ReducerOp : uint8_t {
    SUM = 0,
    MAX = 1,
    MIN = 2,
    COUNT = 3,
    ARGMIN = 4,
    ARGMAX = 5,
    AVG = 6
};

struct SteinerEdge {
    uint32_t src_node{0};
    uint32_t tgt_node{0};
    uint16_t relation_id{0};
    uint64_t lane_mask{0};
};

struct SteinerSubgraph {
    std::vector<uint32_t> bridge_nodes;
    std::vector<uint32_t> all_nodes;
    std::vector<SteinerEdge> edges;
};

class MultiLaneResult {
public:
    MultiLaneResult() = default;
    MultiLaneResult(const impulse_snapshot_t* snapshot,
                    uint16_t domain_id,
                    uint32_t domain_node_count,
                    uint32_t lane_count,
                    std::vector<std::string> lane_keys,
                    std::vector<uint64_t> words_per_node);

    [[nodiscard]] uint32_t laneCount() const { return lane_count_; }
    [[nodiscard]] uint32_t domainNodeCount() const { return domain_node_count_; }
    [[nodiscard]] uint16_t domainId() const { return domain_id_; }

    [[nodiscard]] uint64_t laneMaskFor(uint32_t node_id) const;
    [[nodiscard]] uint32_t lanePopcountFor(uint32_t node_id) const;
    [[nodiscard]] std::vector<uint32_t> attributionFor(uint32_t node_id) const;
    [[nodiscard]] std::vector<std::string> keyAttributionFor(const std::string& node_key) const;

    [[nodiscard]] std::vector<uint32_t> nodesWithQuorum(uint32_t min_lanes) const;
    [[nodiscard]] std::vector<std::pair<std::string, uint32_t>> keysWithQuorum(uint32_t min_lanes) const;

    [[nodiscard]] std::vector<std::pair<uint32_t, uint32_t>> findSetCoverPairs(uint64_t target_mask = ~0ULL) const;
    [[nodiscard]] std::vector<uint32_t> findGreedySetCover(uint64_t target_mask = ~0ULL, uint32_t max_nodes = 8) const;

private:
    const impulse_snapshot_t* snapshot_{nullptr};
    uint16_t domain_id_{0};
    uint32_t domain_node_count_{0};
    uint32_t lane_count_{0};
    uint32_t words_per_lane_{0};
    std::vector<std::string> lane_keys_;
    std::vector<uint64_t> words_per_node_;
};

class Traversal {
public:
    Traversal(const impulse_snapshot_t* snapshot, uint16_t domain_id, std::vector<uint64_t> seeds, bool all_nodes = false);

    // --- Hop & Fixed-Point Transitions ---
    Traversal& out(const std::string& relation);
    Traversal& in(const std::string& relation);
    Traversal& outFiltered(const std::string& relation, uint32_t filter_id);
    Traversal& outPredicate(const std::string& relation, const std::string& edge_cel_predicate);
    Traversal& inPredicate(const std::string& relation, const std::string& edge_cel_predicate);
    Traversal& out2Hop(const std::string& rel1, const std::string& rel2);
    Traversal& repeat(int iterations, const std::string& relation);
    Traversal& repeatUntilStable(const std::string& relation);

    // --- In-Domain Filters, Projections & Temporal ---
    Traversal& filter(const std::string& node_cel_predicate);
    Traversal& project(const std::string& cel_expression);
    Traversal& validAt(int64_t epoch_millis);

    // --- Frontier Set Algebra ---
    Traversal& unionWith(const Traversal& other);
    Traversal& intersect(const Traversal& other);
    Traversal& difference(const Traversal& other);

    // --- Fused Vector Math & GraphBLAS ---
    Traversal& mulNodeAttr(const std::string& attr_name);
    Traversal& scaleState(float factor);
    Traversal& divNodeAttr(const std::string& attr_name);
    double l1NormDiff(const Traversal& other) const;
    Traversal& mxv(const std::string& relation, uint8_t semiring = SEMIRING_PLUS_TIMES);
    Traversal& vxm(const std::string& relation, uint8_t semiring = SEMIRING_PLUS_TIMES);
    Traversal& ewiseAdd(const Traversal& other, uint8_t binary_op = BINARY_OP_ADD);
    Traversal& ewiseMult(const Traversal& other, uint8_t binary_op = BINARY_OP_MUL);

    // --- Degree, Subgraph Sampling & Vector Rerank ---
    Traversal& degree(const std::string& relation);
    Traversal& sampleNeighbors(const std::string& relation, int32_t k, uint32_t seed = 0);
    Traversal& randomWalk(const std::string& relation, int32_t steps, uint32_t seed = 0);
    Traversal& kCore(int32_t k = 2);
    Traversal& rerank(const std::vector<float>& query_vector, const std::string& attr_name = "", uint8_t metric = 0);

    // --- Terminal Collectors ---
    [[nodiscard]] std::vector<uint64_t> collect() const;
    [[nodiscard]] std::vector<std::string> collectKeys() const;
    [[nodiscard]] size_t count() const;
    [[nodiscard]] double reduceSum() const;
    [[nodiscard]] double walkReduceSum(const std::string& relation, const std::string& attr_name = "") const;
    [[nodiscard]] double reduceMin() const;
    [[nodiscard]] double reduceMax() const;
    [[nodiscard]] int64_t argMin() const;
    [[nodiscard]] int64_t argMax() const;
    [[nodiscard]] std::string argMinKey() const;
    [[nodiscard]] std::string argMaxKey() const;
    [[nodiscard]] CompiledQuery compile() const;

    [[nodiscard]] uint16_t currentDomainId() const { return current_domain_id_; }

private:
    struct Step {
        enum class Kind {
            OUT, IN, OUT_FILTERED, OUT_PREDICATE, IN_PREDICATE, OUT_2HOP,
            REPEAT, REPEAT_UNTIL_STABLE,
            FILTER_CEL, PROJECT_CEL, VALID_AT,
            SET_UNION, SET_INTERSECT, SET_DIFFERENCE,
            MUL_ATTR, SCALE_STATE, DIV_ATTR,
            MXV, VXM, EWISE_ADD, EWISE_MULT,
            DEGREE, SAMPLE_NEIGHBORS, RANDOM_WALK, KCORE, RERANK
        } kind;
        std::string str1;
        std::string str2;
        uint32_t u32_arg1{0};
        uint32_t u32_arg2{0};
        int64_t i64_arg{0};
        float f32_arg{0.0f};
        std::vector<uint64_t> other_nodes;
        std::vector<float> float_vec;
    };

    const impulse_snapshot_t* snapshot_{nullptr};
    uint16_t initial_domain_id_{0};
    uint16_t current_domain_id_{0};
    std::vector<uint64_t> seeds_;
    bool all_nodes_{false};
    std::vector<Step> steps_;

    [[nodiscard]] std::vector<float> evaluateStateVector(std::vector<uint64_t>* out_active_nodes = nullptr) const;
};

class MultiLaneTraversal {
public:
    enum class InitMode { SEEDS, GROUPS, OUTAGES };

    MultiLaneTraversal(const impulse_snapshot_t* snapshot,
                       uint16_t domain_id,
                       InitMode mode,
                       std::vector<uint64_t> seeds,
                       std::vector<std::string> lane_keys = {},
                       std::vector<std::pair<uint32_t, uint32_t>> group_pairs = {},
                       uint16_t outage_rel_id = 0,
                       std::vector<std::vector<uint32_t>> tripped_edges_per_lane = {});

    // --- Hop & Fixed-Point Transitions ---
    MultiLaneTraversal& out(const std::string& relation);
    MultiLaneTraversal& out(const std::string& relation, const std::string& edge_cel_predicate);
    MultiLaneTraversal& in(const std::string& relation);
    MultiLaneTraversal& in(const std::string& relation, const std::string& edge_cel_predicate);
    MultiLaneTraversal& repeat(int iterations, const std::string& relation);
    MultiLaneTraversal& repeatUntilStable(const std::string& relation);

    // --- In-Domain Filters, CEL Mask Mutation & Bit-Lane Pruning ---
    MultiLaneTraversal& filter(const std::string& node_cel_predicate);
    MultiLaneTraversal& project(const std::string& cel_expression);
    MultiLaneTraversal& pruneWhere(const std::string& deny_cel_predicate);
    MultiLaneTraversal& pruneDeniedBy(const std::string& deny_relation);
    MultiLaneTraversal& pruneHubs(uint16_t max_degree, uint8_t min_quorum);
    MultiLaneTraversal& whereQuorum(uint8_t min_lanes);
    MultiLaneTraversal& whereMask(uint64_t required_any_mask, uint64_t forbidden_mask = 0ULL);

    // --- Named-Set High-Level Helpers (No Raw Hex Bitmasks Needed) ---
    MultiLaneTraversal& mustHitAtLeast(uint8_t min_lanes, const std::vector<std::string>& subset_seed_keys);
    MultiLaneTraversal& mustAvoidAll(const std::vector<std::string>& forbidden_seed_keys);

    // --- Terminal Collectors ---
    [[nodiscard]] MultiLaneResult collect() const;
    [[nodiscard]] Traversal toQuorumTraversal(uint8_t min_lanes) const;
    [[nodiscard]] std::vector<double> reducePerLane(const std::string& node_attribute, ReducerOp op) const;
    [[nodiscard]] std::vector<int64_t> argMinNodesPerLane(const std::string& node_attribute) const;
    [[nodiscard]] std::vector<int64_t> argMaxNodesPerLane(const std::string& node_attribute) const;
    [[nodiscard]] std::vector<std::string> argMinKeysPerLane(const std::string& node_attribute) const;
    [[nodiscard]] std::vector<std::string> argMaxKeysPerLane(const std::string& node_attribute) const;
    [[nodiscard]] std::vector<bool> checkAllNodesReachedPerLane() const;
    [[nodiscard]] SteinerSubgraph toSteinerSubgraph(uint8_t min_concept_quorum) const;

private:
    struct LaneStep {
        enum class Kind {
            OUT, IN, REPEAT, REPEAT_UNTIL_STABLE,
            FILTER_CEL, PROJECT_CEL, PRUNE_WHERE_CEL, PRUNE_DENIED_BY, PRUNE_HUBS,
            WHERE_QUORUM, WHERE_MASK, MUST_HIT_AT_LEAST, MUST_AVOID_ALL
        } kind;
        std::string str_arg;
        uint16_t u16_arg{0};
        uint8_t u8_arg{0};
        uint64_t mask_req{0};
        uint64_t mask_forbid{0};
        std::vector<uint32_t> lane_indices;
    };

    const impulse_snapshot_t* snapshot_{nullptr};
    uint16_t current_domain_id_{0};
    InitMode mode_{InitMode::SEEDS};
    std::vector<uint64_t> seeds_;
    std::vector<std::string> lane_keys_;
    std::vector<std::pair<uint32_t, uint32_t>> group_pairs_;
    uint16_t outage_rel_id_{0};
    std::vector<std::vector<uint32_t>> tripped_edges_per_lane_;
    std::vector<LaneStep> steps_;

    [[nodiscard]] std::vector<uint32_t> resolveKeyIndices(const std::vector<std::string>& keys) const;
};

class MultiLaneBuilder {
public:
    MultiLaneBuilder(const impulse_snapshot_t* snapshot, uint16_t domain_id)
        : snapshot_(snapshot), domain_id_(domain_id) {}

    [[nodiscard]] MultiLaneTraversal fromSeeds(std::vector<uint64_t> node_ids) const;
    [[nodiscard]] MultiLaneTraversal fromSeeds(std::initializer_list<uint64_t> node_ids) const {
        return fromSeeds(std::vector<uint64_t>(node_ids));
    }

    [[nodiscard]] MultiLaneTraversal fromKeys(const std::vector<std::string>& keys) const;
    [[nodiscard]] MultiLaneTraversal fromKeys(std::initializer_list<std::string> keys) const {
        return fromKeys(std::vector<std::string>(keys));
    }

    [[nodiscard]] MultiLaneTraversal fromGroups(const std::vector<std::pair<std::string, std::vector<uint64_t>>>& concept_groups) const;
    [[nodiscard]] MultiLaneTraversal fromConceptVectors(const std::string& index_or_attr,
                                                        const std::vector<std::pair<std::string, std::vector<float>>>& concept_vectors,
                                                        uint16_t hits_per_concept) const;
    [[nodiscard]] MultiLaneTraversal fromOutages(const std::string& relation,
                                                 const std::vector<std::vector<uint32_t>>& tripped_edge_ids_per_lane) const;

private:
    const impulse_snapshot_t* snapshot_{nullptr};
    uint16_t domain_id_{0};
};

class DomainView {
public:
    DomainView(const impulse_snapshot_t* snapshot, uint16_t domain_id)
        : snapshot_(snapshot), domain_id_(domain_id) {}

    [[nodiscard]] Traversal all() const;
    [[nodiscard]] Traversal from(std::vector<uint64_t> node_ids) const;
    [[nodiscard]] Traversal from(std::initializer_list<uint64_t> node_ids) const {
        return from(std::vector<uint64_t>(node_ids));
    }
    [[nodiscard]] Traversal fromKey(const std::string& key) const;
    [[nodiscard]] Traversal fromKeys(const std::vector<std::string>& keys) const;
    [[nodiscard]] Traversal vectorSearch(const std::string& index_or_attr, const std::vector<float>& query_vector, uint16_t top_k) const;

    [[nodiscard]] MultiLaneBuilder multiLane() const {
        return MultiLaneBuilder(snapshot_, domain_id_);
    }

    [[nodiscard]] uint16_t domainId() const { return domain_id_; }

private:
    const impulse_snapshot_t* snapshot_{nullptr};
    uint16_t domain_id_{0};
};

class SnapshotView {
public:
    explicit SnapshotView(const impulse_snapshot_t* snapshot) : snapshot_(snapshot) {}

    [[nodiscard]] DomainView domain(const std::string& domain_name) const;
    [[nodiscard]] DomainView domain(uint16_t domain_id) const {
        return DomainView(snapshot_, domain_id);
    }
    [[nodiscard]] const impulse_snapshot_t* raw() const { return snapshot_; }

private:
    const impulse_snapshot_t* snapshot_{nullptr};
};

} // namespace impulse::vm

#endif // IMPULSE_VM_FLUENT_HPP
