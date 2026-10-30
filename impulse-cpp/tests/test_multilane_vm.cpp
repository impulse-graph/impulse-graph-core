/**
 * @file test_multilane_vm.cpp
 * @brief TDD Test Suite for ImpulseVM Multi-Lane Bit-Sliced Execution Opcodes (0xC0..0xC9),
 *        Deterministic ARGMIN/ARGMAX Tie-Breaking, and Differential 17-Boundary Oracle.
 */

#include "impulse_vm.h"
#include "impulse_assert.h"
#include "impulse_hexio.h"

#undef NDEBUG
#include <algorithm>
#include <array>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <set>
#include <vector>

static thread_local bool g_track_heap_allocs = false;
static thread_local size_t g_heap_alloc_count = 0;

void* operator new(std::size_t sz) {
    if (g_track_heap_allocs) {
        ++g_heap_alloc_count;
    }
    void* p = std::malloc(sz == 0 ? 1 : sz);
    if (!p) throw std::bad_alloc();
    return p;
}

void* operator new[](std::size_t sz) {
    if (g_track_heap_allocs) {
        ++g_heap_alloc_count;
    }
    void* p = std::malloc(sz == 0 ? 1 : sz);
    if (!p) throw std::bad_alloc();
    return p;
}

void operator delete(void* p) noexcept {
    std::free(p);
}

void operator delete[](void* p) noexcept {
    std::free(p);
}

void operator delete(void* p, std::size_t) noexcept {
    std::free(p);
}

void operator delete[](void* p, std::size_t) noexcept {
    std::free(p);
}

namespace {

constexpr int NORMATIVE_BOUNDARY_SEED_COUNTS[17] = {
    0, 1, 7, 8, 9, 15, 16, 17, 31, 32, 33, 63, 64, 65, 127, 128, 129
};

struct MockCsrGraph {
    uint32_t node_count;
    std::vector<uint32_t> row_offsets;
    std::vector<uint32_t> targets;
};

static MockCsrGraph build_test_mesh_graph(uint32_t n) {
    MockCsrGraph g;
    g.node_count = n;
    g.row_offsets.resize(n + 1, 0);
    for (uint32_t u = 0; u < n; ++u) {
        // Each node u connects to (u+1)%n, (u+3)%n, and a shared hub u/4
        std::vector<uint32_t> nbrs = {
            (u + 1) % n,
            (u + 3) % n,
            (u / 4) % n
        };
        std::sort(nbrs.begin(), nbrs.end());
        nbrs.erase(std::unique(nbrs.begin(), nbrs.end()), nbrs.end());
        for (uint32_t v : nbrs) {
            g.targets.push_back(v);
        }
        g.row_offsets[u + 1] = static_cast<uint32_t>(g.targets.size());
    }
    return g;
}

// ============================================================================
// 1. Active Mask & Hardware Lane Width Tier Selection Tests
// ============================================================================
static void test_active_mask_and_width_tier_selection() {
    std::printf("--- [Test 1] Active Mask & Width Tier Selection (0..64) ---\n");

    assert(impulse_make_active_mask_u64(0) == 0ULL);
    assert(impulse_make_active_mask_u64(1) == 0x1ULL);
    assert(impulse_make_active_mask_u64(7) == 0x7FULL);
    assert(impulse_make_active_mask_u64(8) == 0xFFULL);
    assert(impulse_make_active_mask_u64(9) == 0x1FFULL);
    assert(impulse_make_active_mask_u64(15) == 0x7FFFULL);
    assert(impulse_make_active_mask_u64(16) == 0xFFFFULL);
    assert(impulse_make_active_mask_u64(17) == 0x1FFFFULL);
    assert(impulse_make_active_mask_u64(31) == 0x7FFFFFFFULL);
    assert(impulse_make_active_mask_u64(32) == 0xFFFFFFFFULL);
    assert(impulse_make_active_mask_u64(33) == 0x1FFFFFFFFULL);
    assert(impulse_make_active_mask_u64(63) == 0x7FFFFFFFFFFFFFFFULL);
    assert(impulse_make_active_mask_u64(64) == 0xFFFFFFFFFFFFFFFFULL);
    assert(impulse_make_active_mask_u64(65) == 0xFFFFFFFFFFFFFFFFULL);

    // Check hardware width tier selection via OP_LANE_INIT_SEEDS (0xC0)
    struct TierCase {
        uint16_t k;
        uint32_t expected_width;
    };
    const TierCase cases[] = {
        {1, IMPULSE_LANE_WIDTH_8},
        {7, IMPULSE_LANE_WIDTH_8},
        {8, IMPULSE_LANE_WIDTH_8},
        {9, IMPULSE_LANE_WIDTH_16},
        {15, IMPULSE_LANE_WIDTH_16},
        {16, IMPULSE_LANE_WIDTH_16},
        {17, IMPULSE_LANE_WIDTH_32},
        {31, IMPULSE_LANE_WIDTH_32},
        {32, IMPULSE_LANE_WIDTH_32},
        {33, IMPULSE_LANE_WIDTH_64},
        {63, IMPULSE_LANE_WIDTH_64},
        {64, IMPULSE_LANE_WIDTH_64}
    };

    for (const auto& tc : cases) {
        impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr);
        impulse_vm_state_t st{};
        st.query_context = ctx;

        // R0 is TYPE_NULL; payload high 16 bits = k, flags = 1 (seeds 0..k-1)
        uint32_t payload = (static_cast<uint32_t>(tc.k) << 16) | 0u;
        impulse_instruction_t prog[2] = {
            {OP_LANE_INIT_SEEDS, 1, 1, payload},
            {OP_HALT, 0, 0, 0}
        };
        assert(impulse_vm_validate(prog, 2) == IMPULSE_VM_OK);
        assert(impulse_vm_execute(prog, 2, &st, 0) == IMPULSE_VM_OK);
        assert(st.register_types[1] == TYPE_LANE_FRONTIER);

        size_t h = st.registers[1];
        assert(impulse_vm_context_lane_frontier_num_lanes(ctx, h) == tc.k);
        assert(impulse_vm_context_lane_frontier_width(ctx, h) == tc.expected_width);
        assert(impulse_vm_context_lane_frontier_active_mask(ctx, h) == impulse_make_active_mask_u64(tc.k));

        const uint64_t* lanes = impulse_vm_context_lane_frontier_data(ctx, h);
        assert(lanes != nullptr);
        for (uint16_t b = 0; b < tc.k; ++b) {
            assert(lanes[b] == (1ULL << b));
        }

        impulse_vm_context_destroy(ctx);
    }

    std::printf("  [PASS] test_active_mask_and_width_tier_selection\n");
}

// ============================================================================
// 2. OP_LANE_INIT_SEEDS (0xC0) & OP_LANE_INIT_GROUPS (0xC1) + Negative Traps
// ============================================================================
static void test_op_lane_init_seeds_and_groups() {
    std::printf("--- [Test 2] OP_LANE_INIT_SEEDS (0xC0) & OP_LANE_INIT_GROUPS (0xC1) ---\n");

    impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr);
    impulse_vm_state_t st{};
    st.query_context = ctx;

    // 2a. Init seeds from TYPE_NODE_VECTOR: seeds = [5, 10, 15, 20]
    int h_vec = impulse_vm_context_acquire_node_vector(ctx);
    assert(h_vec >= 0);
    const uint64_t seeds[4] = {5, 10, 15, 20};
    impulse_vm_context_node_vector_set(ctx, static_cast<size_t>(h_vec), seeds, 4);
    st.registers[1] = static_cast<uint64_t>(h_vec);
    st.register_types[1] = TYPE_NODE_VECTOR;

    // 2b. Init groups from packed (group_id << 32) | node_id:
    // Group 0: nodes {2, 3}, Group 1: nodes {3, 7}, Group 2: node {9}
    int h_grp = impulse_vm_context_acquire_node_vector(ctx);
    assert(h_grp >= 0);
    const uint64_t packed_groups[5] = {
        (0ULL << 32) | 2ULL,
        (0ULL << 32) | 3ULL,
        (1ULL << 32) | 3ULL,
        (1ULL << 32) | 7ULL,
        (2ULL << 32) | 9ULL
    };
    impulse_vm_context_node_vector_set(ctx, static_cast<size_t>(h_grp), packed_groups, 5);
    st.registers[2] = static_cast<uint64_t>(h_grp);
    st.register_types[2] = TYPE_NODE_VECTOR;

    impulse_instruction_t prog[3] = {
        {OP_LANE_INIT_SEEDS, 0, 3, 1u},              // R3 <- seeds from R1
        {OP_LANE_INIT_GROUPS, 1, 4, 2u | (3u << 16)}, // R4 <- 3 concept groups from R2
        {OP_HALT, 0, 0, 0}
    };
    assert(impulse_vm_execute(prog, 3, &st, 0) == IMPULSE_VM_OK);
    assert(st.register_types[3] == TYPE_LANE_FRONTIER);
    assert(st.register_types[4] == TYPE_LANE_FRONTIER);

    const uint64_t* seed_lanes = impulse_vm_context_lane_frontier_data(ctx, st.registers[3]);
    assert(seed_lanes[5] == 0x1ULL);
    assert(seed_lanes[10] == 0x2ULL);
    assert(seed_lanes[15] == 0x4ULL);
    assert(seed_lanes[20] == 0x8ULL);

    const uint64_t* grp_lanes = impulse_vm_context_lane_frontier_data(ctx, st.registers[4]);
    assert(grp_lanes[2] == 0x1ULL); // Group 0 only
    assert(grp_lanes[3] == 0x3ULL); // Group 0 | Group 1
    assert(grp_lanes[7] == 0x2ULL); // Group 1 only
    assert(grp_lanes[9] == 0x4ULL); // Group 2 only

    // 2c. Negative test: invalid register type (TYPE_FLOAT in R5) -> IMPULSE_VM_ERR_INVALID_REGISTER
    st.pc = 0;
    st.registers[5] = 0;
    st.register_types[5] = TYPE_FLOAT;
    impulse_instruction_t bad_prog[2] = {
        {OP_LANE_INIT_SEEDS, 0, 6, 5u},
        {OP_HALT, 0, 0, 0}
    };
    assert(impulse_vm_execute(bad_prog, 2, &st, 0) == IMPULSE_VM_ERR_INVALID_REGISTER);

    impulse_vm_context_destroy(ctx);
    std::printf("  [PASS] test_op_lane_init_seeds_and_groups\n");
}

// ============================================================================
// 3. OP_LANE_SET_EDGE_MASK (0xC2), OP_LANE_CSR_WALK (0xC3), OP_LANE_CSC_WALK (0xC4)
//    & OP_LANE_REDUCE_AND (0xC7) Counterfactual N-1/N-2 Outage Screening
// ============================================================================
static void test_op_lane_csr_csc_and_edge_trip_masks() {
    std::printf("--- [Test 3] Counterfactual Edge Masks (0xC2), CSR/CSC Walks (0xC3/0xC4) & Reduce-AND (0xC7) ---\n");

    // 4-node ring + bridge graph:
    // Edge 0: 0 -> 1
    // Edge 1: 0 -> 2
    // Edge 2: 1 -> 3
    // Edge 3: 2 -> 3
    const uint32_t offsets[5] = {0, 2, 3, 4, 4};
    const uint32_t targets[4] = {1, 2, 3, 3};

    impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr);
    impulse_vm_context_mock_csr(ctx, 0, offsets, targets, 4, 4);
    impulse_vm_state_t st{};
    st.query_context = ctx;

    // 4 lanes all seeded at root node 0:
    // Lane 0: no edges tripped (reaches {0, 1, 2, 3})
    // Lane 1: trip edge 0 (0->1) only (still reaches 3 via 0->2->3, fails to reach 1)
    // Lane 2: trip edge 1 (0->2) only (still reaches 3 via 0->1->3, fails to reach 2)
    // Lane 3: N-2 double outage: trip edge 0 AND edge 1 (isolates 0, reaches only {0})
    int h_seeds = impulse_vm_context_acquire_node_vector(ctx);
    const uint64_t root_seeds[4] = {0, 0, 0, 0};
    impulse_vm_context_node_vector_set(ctx, static_cast<size_t>(h_seeds), root_seeds, 4);
    st.registers[1] = static_cast<uint64_t>(h_seeds);
    st.register_types[1] = TYPE_NODE_VECTOR;

    int h_trips = impulse_vm_context_acquire_node_vector(ctx);
    const uint64_t packed_trips[4] = {
        (1ULL << 32) | 0ULL, // Lane 1 trips edge 0
        (2ULL << 32) | 1ULL, // Lane 2 trips edge 1
        (3ULL << 32) | 0ULL, // Lane 3 trips edge 0
        (3ULL << 32) | 1ULL  // Lane 3 trips edge 1
    };
    impulse_vm_context_node_vector_set(ctx, static_cast<size_t>(h_trips), packed_trips, 4);
    st.registers[2] = static_cast<uint64_t>(h_trips);
    st.register_types[2] = TYPE_NODE_VECTOR;

    impulse_instruction_t prog[7] = {
        {OP_LANE_INIT_SEEDS, 0, 3, 1u},                     // R3 <- 4 lanes at node 0
        {OP_LANE_SET_EDGE_MASK, 1, 4, 3u | (2u << 8)},      // R4 <- attach packed trips in R2 on rel 0
        {OP_LANE_CSR_WALK, 0x04, 5, 4u | (0u << 16)},       // R5 <- fixed-point BFS closure over R4
        {OP_LANE_REDUCE_AND, 0, 6, 5u | (4u << 16)},        // R6 <- mode 0: lanes reaching all 4 nodes (only Lane 0 = 0x1)
        {OP_LANE_REDUCE_AND, 1, 7, 5u | (4u << 16)},        // R7 <- mode 1: islanded contingency mask (Lanes 1,2,3 = 0xE)
        {OP_LANE_REDUCE_AND, 2, 8, 5u | (4u << 16)},        // R8 <- mode 2: universally reached node count (only node 0 = 1)
        {OP_HALT, 0, 0, 0}
    };
    assert(impulse_vm_execute(prog, 7, &st, 0) == IMPULSE_VM_OK);

    const uint64_t* visited = impulse_vm_context_lane_frontier_data(ctx, st.registers[5]);
    assert(visited[0] == 0xFULL); // All 4 lanes start at 0
    assert(visited[1] == 0x5ULL); // Lanes 0 and 2 reach node 1
    assert(visited[2] == 0x3ULL); // Lanes 0 and 1 reach node 2
    assert(visited[3] == 0x7ULL); // Lanes 0, 1, 2 reach node 3 (N-1 resilient, N-2 Lane 3 fails!)

    assert(st.registers[6] == 0x1ULL); // Only Lane 0 reached all 4 nodes
    assert(st.registers[7] == 0xEULL); // Lanes 1, 2, 3 had >=1 unreached node
    assert(st.registers[8] == 1ULL);   // Only node 0 was reached by all 4 lanes

    impulse_vm_context_destroy(ctx);
    std::printf("  [PASS] test_op_lane_csr_csc_and_edge_trip_masks\n");
}

// ============================================================================
// 4. OP_LANE_PRUNE_MASK (0xC5), OP_LANE_FILTER_QUORUM (0xC6) & OP_LANE_SET_COVER (0xC9)
// ============================================================================
static void test_op_lane_prune_quorum_and_set_cover() {
    std::printf("--- [Test 4] Prune Mask (0xC5), Quorum Filter (0xC6) & Set Cover (0xC9) ---\n");

    impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr);
    impulse_vm_state_t st{};
    st.query_context = ctx;

    // Initialize 4 lanes (bits 0..3) across 5 candidate nodes:
    // Node 0: covers lanes {0, 1}       (0x3)
    // Node 1: covers lanes {2, 3}       (0xC)
    // Node 2: covers lanes {0, 1, 2}    (0x7)
    // Node 3: covers lanes {3}          (0x8)
    // Node 4: covers lanes {0, 1, 2, 3} (0xF) - but will be pruned by deny bitset!
    int h_grp = impulse_vm_context_acquire_node_vector(ctx);
    const uint64_t pairs[12] = {
        (0ULL << 32) | 0ULL, (1ULL << 32) | 0ULL,
        (2ULL << 32) | 1ULL, (3ULL << 32) | 1ULL,
        (0ULL << 32) | 2ULL, (1ULL << 32) | 2ULL, (2ULL << 32) | 2ULL,
        (3ULL << 32) | 3ULL,
        (0ULL << 32) | 4ULL, (1ULL << 32) | 4ULL, (2ULL << 32) | 4ULL, (3ULL << 32) | 4ULL
    };
    impulse_vm_context_node_vector_set(ctx, static_cast<size_t>(h_grp), pairs, 12);
    st.registers[1] = static_cast<uint64_t>(h_grp);
    st.register_types[1] = TYPE_NODE_VECTOR;

    // Deny bitset in R2: deny node 4
    int h_deny = impulse_vm_context_acquire_bitset(ctx);
    impulse_vm_context_bitset_add(ctx, static_cast<size_t>(h_deny), 4);
    st.registers[2] = static_cast<uint64_t>(h_deny);
    st.register_types[2] = TYPE_BITSET_HANDLE;

    impulse_instruction_t prog[6] = {
        {OP_LANE_INIT_GROUPS, 1, 3, 1u | (4u << 16)},      // R3 <- 4-lane frontier
        {OP_LANE_PRUNE_MASK, 0, 4, 3u | (2u << 8)},        // R4 <- mode 0 prune R3 using deny bitset R2 (clears node 4)
        {OP_LANE_FILTER_QUORUM, 0, 5, 4u | (3u << 16)},    // R5 <- nodes with >= 3 lanes (only node 2!)
        {OP_LANE_SET_COVER, 0, 6, 4u},                     // R6 <- greedy set cover (picks node 2 [gain 3], then node 1 [gain 1, lowest ID tie-break over node 3!])
        {OP_LANE_SET_COVER, 1, 7, 4u},                     // R7 <- exact 2-node pairs covering 0xF: (0,1), (1,2), (2,3)
        {OP_HALT, 0, 0, 0}
    };
    assert(impulse_vm_execute(prog, 6, &st, 0) == IMPULSE_VM_OK);

    // Check R5 quorum bitset: only node 2 has >= 3 bits after node 4 was pruned
    size_t h_q = st.registers[5];
    assert(impulse_vm_context_bitset_test(ctx, h_q, 2) == true);
    assert(impulse_vm_context_bitset_test(ctx, h_q, 4) == false);
    assert(impulse_vm_context_bitset_test(ctx, h_q, 0) == false);

    // Check R6 greedy set cover: node 2 (covers 0,1,2), then node 1 (covers 3; ties with node 3 on gain=1, node 1 wins by lowest ID!)
    size_t h_greedy = st.registers[6];
    assert(impulse_vm_context_node_vector_size(ctx, h_greedy) == 2);
    const uint64_t* greedy_nodes = impulse_vm_context_get_node_vector(ctx, h_greedy);
    assert(greedy_nodes[0] == 2);
    assert(greedy_nodes[1] == 1);

    // Check R7 exact 2-node pair cover: (0,1), (1,2), (2,3) -> 6 elements
    size_t h_pairs = st.registers[7];
    assert(impulse_vm_context_node_vector_size(ctx, h_pairs) == 6);
    const uint64_t* pair_nodes = impulse_vm_context_get_node_vector(ctx, h_pairs);
    assert(pair_nodes[0] == 0 && pair_nodes[1] == 1);
    assert(pair_nodes[2] == 1 && pair_nodes[3] == 2);
    assert(pair_nodes[4] == 2 && pair_nodes[5] == 3);

    impulse_vm_context_destroy(ctx);
    std::printf("  [PASS] test_op_lane_prune_quorum_and_set_cover\n");
}

// ============================================================================
// 5. OP_LANE_REDUCE_ATTR (0xC8) & Deterministic ARGMIN / ARGMAX Tie-Breaking
// ============================================================================
static void test_op_lane_reduce_attr_and_argmin_argmax_tie_breaking() {
    std::printf("--- [Test 5] OP_LANE_REDUCE_ATTR (0xC8) & Deterministic ARGMIN/ARGMAX Tie-Breaking ---\n");

    impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr);
    impulse_vm_state_t st{};
    st.query_context = ctx;

    // 3 lanes across 5 nodes:
    // Lane 0: reaches nodes {1, 2, 3}
    // Lane 1: reaches nodes {0, 2, 4}
    // Lane 2: reaches NO nodes (empty lane -> witness ID must be -1!)
    int h_grp = impulse_vm_context_acquire_node_vector(ctx);
    const uint64_t pairs[6] = {
        (0ULL << 32) | 1ULL, (0ULL << 32) | 2ULL, (0ULL << 32) | 3ULL,
        (1ULL << 32) | 0ULL, (1ULL << 32) | 2ULL, (1ULL << 32) | 4ULL
    };
    impulse_vm_context_node_vector_set(ctx, static_cast<size_t>(h_grp), pairs, 6);
    st.registers[1] = static_cast<uint64_t>(h_grp);
    st.register_types[1] = TYPE_NODE_VECTOR;

    // Attribute vector in R2 with deliberate ties:
    // attr[0] = 10.0f
    // attr[1] = 5.0f
    // attr[2] = 5.0f   (Tie for min in Lane 0 between node 1 and node 2 -> node 1 MUST win!)
    // attr[3] = 20.0f
    // attr[4] = 10.0f  (Tie for max in Lane 1 between node 0 and node 4 -> node 0 MUST win!)
    int h_attr = impulse_vm_context_acquire_float_vector(ctx);
    const float vals[5] = {10.0f, 5.0f, 5.0f, 20.0f, 10.0f};
    for (size_t i = 0; i < 5; ++i) {
        impulse_vm_context_float_vector_set(ctx, static_cast<size_t>(h_attr), i, vals[i]);
    }
    st.registers[2] = static_cast<uint64_t>(h_attr);
    st.register_types[2] = TYPE_FLOAT_VECTOR;

    impulse_instruction_t prog[7] = {
        {OP_LANE_INIT_GROUPS, 1, 3, 1u | (3u << 16)},                         // R3 <- 3 lanes
        {OP_LANE_REDUCE_ATTR, 1, 4, 3u | (2u << 8) | (BINARY_OP_ADD << 16)},  // R4 <- SUM per lane
        {OP_LANE_REDUCE_ATTR, 1, 5, 3u | (2u << 8) | (BINARY_OP_MIN << 16)},  // R5 <- MIN per lane
        {OP_LANE_REDUCE_ATTR, 1, 6, 3u | (2u << 8) | (BINARY_OP_MAX << 16)},  // R6 <- MAX per lane
        {OP_LANE_REDUCE_ATTR, 1, 7, 3u | (2u << 8) | (BINARY_OP_ARGMIN << 16)}, // R7 <- ARGMIN witness IDs
        {OP_LANE_REDUCE_ATTR, 1, 8, 3u | (2u << 8) | (BINARY_OP_ARGMAX << 16)}, // R8 <- ARGMAX witness IDs
        {OP_HALT, 0, 0, 0}
    };
    assert(impulse_vm_execute(prog, 7, &st, 0) == IMPULSE_VM_OK);

    const float* sums = impulse_vm_context_get_float_vector(ctx, st.registers[4]);
    assert(std::fabs(sums[0] - 30.0f) < 1e-5f); // 5 + 5 + 20
    assert(std::fabs(sums[1] - 25.0f) < 1e-5f); // 10 + 5 + 10
    assert(std::fabs(sums[2] - 0.0f) < 1e-5f);  // untouched lane

    const float* mins = impulse_vm_context_get_float_vector(ctx, st.registers[5]);
    assert(std::fabs(mins[0] - 5.0f) < 1e-5f);
    assert(std::fabs(mins[1] - 5.0f) < 1e-5f);
    assert(std::fabs(mins[2] - 0.0f) < 1e-5f);

    const float* maxs = impulse_vm_context_get_float_vector(ctx, st.registers[6]);
    assert(std::fabs(maxs[0] - 20.0f) < 1e-5f);
    assert(std::fabs(maxs[1] - 10.0f) < 1e-5f);
    assert(std::fabs(maxs[2] - 0.0f) < 1e-5f);

    assert(st.register_types[7] == TYPE_UINT64_VECTOR);
    const uint64_t* argmins = impulse_vm_context_get_node_vector(ctx, st.registers[7]);
    assert(static_cast<int64_t>(argmins[0]) == 1);  // Tie between node 1 (5.0) and node 2 (5.0) -> lowest ID 1!
    assert(static_cast<int64_t>(argmins[1]) == 2);  // Node 2 has 5.0
    assert(static_cast<int64_t>(argmins[2]) == -1); // Empty lane -> -1

    assert(st.register_types[8] == TYPE_UINT64_VECTOR);
    const uint64_t* argmaxs = impulse_vm_context_get_node_vector(ctx, st.registers[8]);
    assert(static_cast<int64_t>(argmaxs[0]) == 3);  // Node 3 has 20.0
    assert(static_cast<int64_t>(argmaxs[1]) == 0);  // Tie between node 0 (10.0) and node 4 (10.0) -> lowest ID 0!
    assert(static_cast<int64_t>(argmaxs[2]) == -1); // Empty lane -> -1

    impulse_vm_context_destroy(ctx);
    std::printf("  [PASS] test_op_lane_reduce_attr_and_argmin_argmax_tie_breaking\n");
}

// ============================================================================
// 6. Differential Single-Lane Reference Oracle Across All 17 Seed-Count Boundaries
// ============================================================================
static std::set<uint32_t> oracle_single_lane_walk(
    const MockCsrGraph& g,
    uint32_t seed,
    bool is_csc,
    bool accumulate,
    bool fixpoint)
{
    auto step_once = [&](const std::set<uint32_t>& frontier) {
        std::set<uint32_t> next = accumulate ? frontier : std::set<uint32_t>{};
        if (!is_csc) {
            for (uint32_t u : frontier) {
                if (u < g.node_count) {
                    for (uint32_t e = g.row_offsets[u]; e < g.row_offsets[u + 1]; ++e) {
                        next.insert(g.targets[e]);
                    }
                }
            }
        } else {
            for (uint32_t u = 0; u < g.node_count; ++u) {
                for (uint32_t e = g.row_offsets[u]; e < g.row_offsets[u + 1]; ++e) {
                    uint32_t v = g.targets[e];
                    if (frontier.count(v)) {
                        next.insert(u);
                    }
                }
            }
        }
        return next;
    };

    std::set<uint32_t> init;
    if (seed < g.node_count) init.insert(seed);
    if (!fixpoint) return step_once(init);

    std::set<uint32_t> visited = init;
    std::set<uint32_t> delta = init;
    while (!delta.empty()) {
        std::set<uint32_t> step_out = step_once(delta);
        std::set<uint32_t> newly;
        for (uint32_t v : step_out) {
            if (!visited.count(v)) {
                visited.insert(v);
                newly.insert(v);
            }
        }
        delta = std::move(newly);
    }
    return visited;
}

static void test_differential_17_boundary_seed_counts_oracle() {
    std::printf("--- [Test 6] Differential Oracle Across All 17 Seed-Count Boundaries (K=0..129) ---\n");

    const uint32_t N = 160;
    MockCsrGraph g = build_test_mesh_graph(N);
    std::vector<float> attr_vals(N);
    for (uint32_t u = 0; u < N; ++u) {
        // Periodic attribute values to exercise deterministic tie-breaking
        attr_vals[u] = static_cast<float>((u * 7) % 23) + 1.5f;
    }

    for (int k : NORMATIVE_BOUNDARY_SEED_COUNTS) {
        std::vector<uint32_t> seeds(static_cast<size_t>(k));
        for (int b = 0; b < k; ++b) {
            seeds[static_cast<size_t>(b)] = static_cast<uint32_t>(b % N);
        }

        for (int mode = 0; mode < 3; ++mode) {
            bool is_csc = (mode == 1);
            bool fixpoint = (mode == 2);
            bool accumulate = false;

            // 1. Compute ground-truth via K independent 1-way single-lane walks
            std::vector<std::set<uint32_t>> oracle_sets(static_cast<size_t>(k));
            std::vector<int> oracle_hit_counts(N, 0);
            std::vector<float> oracle_sums(static_cast<size_t>(k), 0.0f);
            std::vector<int64_t> oracle_argmins(static_cast<size_t>(k), -1);

            for (int b = 0; b < k; ++b) {
                oracle_sets[static_cast<size_t>(b)] =
                    oracle_single_lane_walk(g, seeds[static_cast<size_t>(b)], is_csc, accumulate, fixpoint);
                float best_v = std::numeric_limits<float>::infinity();
                int64_t best_u = -1;
                for (uint32_t u : oracle_sets[static_cast<size_t>(b)]) {
                    oracle_hit_counts[u]++;
                    float v = attr_vals[u];
                    oracle_sums[static_cast<size_t>(b)] += v;
                    if (best_u == -1 || v < best_v) {
                        best_v = v;
                        best_u = static_cast<int64_t>(u);
                    }
                }
                oracle_argmins[static_cast<size_t>(b)] = best_u;
            }

            // 2. Execute via Batched Multi-Lane VM (64 lanes per pass)
            std::vector<std::set<uint32_t>> vm_sets(static_cast<size_t>(k));
            std::vector<int> vm_hit_counts(N, 0);
            std::vector<float> vm_sums(static_cast<size_t>(k), 0.0f);
            std::vector<int64_t> vm_argmins(static_cast<size_t>(k), -1);

            int num_batches = (k == 0) ? 1 : ((k + 63) / 64);
            for (int batch_idx = 0; batch_idx < num_batches; ++batch_idx) {
                int offset = batch_idx * 64;
                int chunk_len = (k == 0) ? 0 : std::min(64, k - offset);

                impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr);
                impulse_vm_context_mock_csr(ctx, 0, g.row_offsets.data(), g.targets.data(), g.node_count, g.targets.size());

                int h_seeds = impulse_vm_context_acquire_node_vector(ctx);
                std::vector<uint64_t> chunk_seeds(static_cast<size_t>(chunk_len));
                for (int i = 0; i < chunk_len; ++i) {
                    chunk_seeds[static_cast<size_t>(i)] = seeds[static_cast<size_t>(offset + i)];
                }
                impulse_vm_context_node_vector_set(ctx, static_cast<size_t>(h_seeds), chunk_seeds.data(), static_cast<size_t>(chunk_len));

                int h_attr = impulse_vm_context_acquire_float_vector(ctx);
                for (uint32_t u = 0; u < N; ++u) {
                    impulse_vm_context_float_vector_set(ctx, static_cast<size_t>(h_attr), u, attr_vals[u]);
                }

                impulse_vm_state_t st{};
                st.query_context = ctx;
                st.registers[1] = static_cast<uint64_t>(h_seeds);
                st.register_types[1] = TYPE_NODE_VECTOR;
                st.registers[2] = static_cast<uint64_t>(h_attr);
                st.register_types[2] = TYPE_FLOAT_VECTOR;

                uint8_t walk_op = is_csc ? OP_LANE_CSC_WALK : OP_LANE_CSR_WALK;
                uint8_t walk_flags = fixpoint ? 0x04 : 0x00;

                impulse_instruction_t prog[5] = {
                    {OP_LANE_INIT_SEEDS, 0, 3, 1u},
                    {walk_op, walk_flags, 4, 3u | (0u << 16)},
                    {OP_LANE_REDUCE_ATTR, 1, 5, 4u | (2u << 8) | (BINARY_OP_ADD << 16)},
                    {OP_LANE_REDUCE_ATTR, 1, 6, 4u | (2u << 8) | (BINARY_OP_ARGMIN << 16)},
                    {OP_HALT, 0, 0, 0}
                };
                assert(impulse_vm_execute(prog, 5, &st, 0) == IMPULSE_VM_OK);

                size_t h_lf = st.registers[4];
                uint64_t active_mask = impulse_vm_context_lane_frontier_active_mask(ctx, h_lf);
                assert(active_mask == impulse_make_active_mask_u64(static_cast<uint32_t>(chunk_len)));
                const uint64_t* lanes = impulse_vm_context_lane_frontier_data(ctx, h_lf);

                for (uint32_t u = 0; u < N; ++u) {
                    uint64_t m = lanes[u] & active_mask;
                    if (m != 0ULL) {
                        vm_hit_counts[u] += std::popcount(m);
                        for (int b = 0; b < chunk_len; ++b) {
                            if ((m & (1ULL << b)) != 0ULL) {
                                vm_sets[static_cast<size_t>(offset + b)].insert(u);
                            }
                        }
                    }
                }

                if (chunk_len > 0) {
                    const float* batch_sums = impulse_vm_context_get_float_vector(ctx, st.registers[5]);
                    const uint64_t* batch_argmins = impulse_vm_context_get_node_vector(ctx, st.registers[6]);
                    for (int b = 0; b < chunk_len; ++b) {
                        vm_sums[static_cast<size_t>(offset + b)] = batch_sums[b];
                        vm_argmins[static_cast<size_t>(offset + b)] = static_cast<int64_t>(batch_argmins[b]);
                    }
                }

                impulse_vm_context_destroy(ctx);
            }

            // 3. Assert 100% parity between Multi-Lane VM and K independent 1-way walks
            assert(vm_hit_counts == oracle_hit_counts);
            for (int b = 0; b < k; ++b) {
                assert(vm_sets[static_cast<size_t>(b)] == oracle_sets[static_cast<size_t>(b)]);
                assert(std::fabs(vm_sums[static_cast<size_t>(b)] - oracle_sums[static_cast<size_t>(b)]) < 1e-4f);
                assert(vm_argmins[static_cast<size_t>(b)] == oracle_argmins[static_cast<size_t>(b)]);
            }
        }
    }

    std::printf("  Verified 100%% parity across all 17 boundary seed counts (0..129) x 3 walk modes (51 runs).\n");
    std::printf("  [PASS] test_differential_17_boundary_seed_counts_oracle\n");
}

static void test_zero_heap_allocations_hot_path() {
    std::printf("--- [Test 7] Zero Heap Allocations & Lock-Free Hot Path Verification ---\n");

    const uint32_t N = 64;
    MockCsrGraph g = build_test_mesh_graph(N);
    impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr);
    impulse_vm_context_mock_csr(ctx, 0, g.row_offsets.data(), g.targets.data(), g.node_count, g.targets.size());

    int h_seeds = impulse_vm_context_acquire_node_vector(ctx);
    uint64_t seed_ids[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    impulse_vm_context_node_vector_set(ctx, static_cast<size_t>(h_seeds), seed_ids, 8);

    int h_groups = impulse_vm_context_acquire_node_vector(ctx);
    uint64_t group_ids[8] = {0, 0, 1, 1, 2, 2, 3, 3};
    impulse_vm_context_node_vector_set(ctx, static_cast<size_t>(h_groups), group_ids, 8);

    int h_edges = impulse_vm_context_acquire_node_vector(ctx);
    uint64_t edge_masks[2] = {(0ULL << 32) | 0ULL, (1ULL << 32) | 1ULL};
    impulse_vm_context_node_vector_set(ctx, static_cast<size_t>(h_edges), edge_masks, 2);

    int h_attr = impulse_vm_context_acquire_float_vector(ctx);
    for (uint32_t u = 0; u < N; ++u) {
        impulse_vm_context_float_vector_set(ctx, static_cast<size_t>(h_attr), u, static_cast<float>(u + 1));
    }

    impulse_vm_state_t st{};
    st.query_context = ctx;
    st.registers[1] = static_cast<uint64_t>(h_seeds);
    st.register_types[1] = TYPE_NODE_VECTOR;
    st.registers[2] = static_cast<uint64_t>(h_groups);
    st.register_types[2] = TYPE_NODE_VECTOR;
    st.registers[3] = static_cast<uint64_t>(h_edges);
    st.register_types[3] = TYPE_NODE_VECTOR;
    st.registers[4] = static_cast<uint64_t>(h_attr);
    st.register_types[4] = TYPE_FLOAT_VECTOR;

    // Comprehensive program exercising single-lane, vector math, multi-lane, and ANN opcodes
    impulse_instruction_t prog[] = {
        {OP_INIT_MOCK_NODE_ATTR, 0, 0, 4u},
        {OP_LANE_INIT_SEEDS, 0, 10, 1u},
        {OP_LANE_INIT_GROUPS, 0, 11, 1u | (2u << 8) | (4u << 16)},
        {OP_LANE_SET_EDGE_MASK, 1, 12, 10u | (3u << 8) | (0u << 16)},
        {OP_LANE_CSR_WALK, 0x04, 13, 12u | (0u << 16)},
        {OP_LANE_CSC_WALK, 0x02, 14, 13u | (0u << 16)},
        {OP_LANE_PRUNE_MASK, 2, 15, 14u | (2u << 8) | (2u << 16) | (0u << 24)},
        {OP_LANE_FILTER_QUORUM, 0, 16, 15u | (2u << 16)},
        {OP_LANE_REDUCE_AND, 0, 17, 15u | (64u << 16)},
        {OP_LANE_REDUCE_ATTR, 1, 18, 15u | (4u << 8) | (BINARY_OP_ADD << 16)},
        {OP_LANE_REDUCE_ATTR, 1, 19, 15u | (4u << 8) | (BINARY_OP_ARGMIN << 16)},
        {OP_LANE_SET_COVER, 0, 20, 15u | (0u << 8) | (8u << 16)},
        {OP_VECTOR_ANN_SEARCH, 1, 21, 4u | (4u << 8) | (0u << 16)},
        {OP_VECTOR_RERANK_DOT, 0, 22, 16u | (4u << 8) | (0u << 16)},
        {OP_VECTOR_DIV, 0, 23, 22u | (4u << 16)},
        {OP_COALESCE, 0, 24, 23u | (4u << 16)},
        {OP_COLLECT_ARRAY, 0, 25, 16u},
        {OP_LOAD_CONST_INT, 0, 26, 0u},
        {OP_KCORE_DECOMPOSITION, 0, 27, 26u},
        {OP_CC_AFFOREST, 0, 28, 0u},
        {OP_HALT, 0, 0, 0}
    };

    g_heap_alloc_count = 0;
    g_track_heap_allocs = true;
    impulse_vm_status_t status = impulse_vm_execute(prog, sizeof(prog) / sizeof(prog[0]), &st, 0);
    g_track_heap_allocs = false;

    assert(status == IMPULSE_VM_OK);
    std::printf("  Observed heap allocations during impulse_vm_execute: %zu\n", g_heap_alloc_count);
    assert(g_heap_alloc_count == 0 && "Expected strictly ZERO heap allocations during impulse_vm_execute!");

    impulse_vm_context_destroy(ctx);
    std::printf("  [PASS] test_zero_heap_allocations_hot_path\n");
}

// ============================================================================
// 8. Phase 2 TDD: OP_VECTOR_ANN_SEARCH (0xCA) L2 Ranking, Fused Multi-Lane Handshake & OP_VECTOR_RERANK_DOT (0xCB) Metrics
// ============================================================================
static uint16_t encode_fp16(float f) {
    uint32_t bits = 0;
    std::memcpy(&bits, &f, sizeof(float));
    uint32_t sign = (bits >> 31) & 1u;
    int32_t exp32 = static_cast<int32_t>((bits >> 23) & 0xFFu);
    uint32_t mant32 = bits & 0x7FFFFFu;
    if (exp32 == 0xFF) {
        uint32_t frac16 = (mant32 == 0) ? 0u : 0x0200u;
        return static_cast<uint16_t>((sign << 15) | (0x1Fu << 10) | frac16);
    }
    int32_t unb_exp = exp32 - 127 + 15;
    if (unb_exp >= 0x1F) {
        return static_cast<uint16_t>((sign << 15) | (0x1Fu << 10));
    }
    if (unb_exp <= 0) {
        if (unb_exp < -10) return static_cast<uint16_t>(sign << 15);
        uint32_t mant_full = 0x800000u | mant32;
        int32_t shift = 14 - unb_exp;
        uint32_t frac16 = (mant_full >> shift) & 0x03FFu;
        return static_cast<uint16_t>((sign << 15) | frac16);
    }
    uint32_t frac16 = (mant32 >> 13) & 0x03FFu;
    return static_cast<uint16_t>((sign << 15) | (static_cast<uint32_t>(unb_exp) << 10) | frac16);
}

static void test_vector_ann_mock_l2_and_fused_multilane_handshake() {
    std::printf("--- [Test 8] OP_VECTOR_ANN_SEARCH (0xCA) Mock L2, Fused Multi-Lane Handshake & OP_VECTOR_RERANK_DOT (0xCB) 4 Metrics ---\n");

    const uint32_t offsets[5] = {0, 2, 3, 4, 5};
    const uint32_t targets[5] = {1, 2, 2, 3, 0};

    impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr);
    impulse_vm_context_mock_csr(ctx, 0, offsets, targets, 4, 5);
    impulse_vm_state_t st{};
    st.query_context = ctx;

    // Pack both 16-float embedding table and 4-float query vector in inline data
    const float inline_floats[20] = {
        // [0..15]: 4 nodes x 4D embeddings
        1.0f, 0.0f, 0.0f, 0.0f,
        0.8f, 0.2f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        // [16..19]: 4D query vector [0.85, 0.15, 0.0, 0.0]
        0.85f, 0.15f, 0.0f, 0.0f
    };
    impulse_vm_context_bind_inline_data(ctx, inline_floats, sizeof(inline_floats));

    impulse_instruction_t prog[] = {
        {OP_LOAD_INLINE_ARRAY, 0, 10, 0u | (16u << 16)},                     // R10 <- 16-float embedding table
        {OP_INIT_MOCK_NODE_ATTR, 0, 0, 10u | (0u << 16)},                    // Mock attr 0 <- R10 (16 floats = 4 nodes x 4D)
        {OP_LOAD_INLINE_ARRAY, 0, 1, 64u | (4u << 16)},                      // R1 <- q_vec [0.85, 0.15, 0.0, 0.0] at byte offset 64 (len=4)
        {OP_VECTOR_ANN_SEARCH, 0, 2, 1u | (1u << 8) | (0u << 16)},           // R2 <- top_k=1 BitSet (flags=0): MUST select Node 1 by L2!
        {OP_VECTOR_ANN_SEARCH, 1, 3, 1u | (2u << 8) | (0u << 16)},           // R3 <- top_k=2 Fused TYPE_LANE_FRONTIER (flags=1): lane 0 = Node 1, lane 1 = Node 0!
        {OP_LANE_CSR_WALK, 0, 4, 3u | (0u << 16)},                           // R4 <- walk R3 on rel 0: Node 2 gets both lane 0 (from 1) and lane 1 (from 0) -> 0b11 = 3!
        {OP_VECTOR_RERANK_DOT, 0, 5, 3u | (1u << 8) | (0u << 16)},           // R5 <- rerank R3 (cands {0, 1}) with MetricDot (flags=0)
        {OP_VECTOR_RERANK_DOT, 1, 6, 3u | (1u << 8) | (0u << 16)},           // R6 <- rerank R3 (cands {0, 1}) with MetricL2 (flags=1)
        {OP_VECTOR_RERANK_DOT, 2, 7, 3u | (1u << 8) | (0u << 16)},           // R7 <- rerank R3 (cands {0, 1}) with MetricCosine (flags=2)
        {OP_VECTOR_RERANK_DOT, 3, 8, 3u | (1u << 8) | (0u << 16)},           // R8 <- rerank R3 (cands {0, 1}) with MetricHamming (flags=3)
        {OP_HALT, 0, 0, 0}
    };

    g_heap_alloc_count = 0;
    g_track_heap_allocs = true;
    impulse_vm_status_t status = impulse_vm_execute(prog, sizeof(prog) / sizeof(prog[0]), &st, 0);
    g_track_heap_allocs = false;
    assert(status == IMPULSE_VM_OK);
    assert(g_heap_alloc_count == 0 && "Expected 0 heap allocations in mock ANN & rerank!");

    // 1. Verify R2 (flags=0) is TYPE_BITSET_HANDLE with ONLY Node 1 (closest in L2, whereas Node 0 had higher dot product)
    assert(st.register_types[2] == TYPE_BITSET_HANDLE);
    assert(impulse_vm_context_bitset_test(ctx, st.registers[2], 1) == true);
    assert(impulse_vm_context_bitset_test(ctx, st.registers[2], 0) == false);

    // 2. Verify R3 (flags=1) is Fused TYPE_LANE_FRONTIER with num_lanes=2, lane 0 at Node 1, lane 1 at Node 0
    assert(st.register_types[3] == TYPE_LANE_FRONTIER);
    assert(impulse_vm_context_lane_frontier_num_lanes(ctx, st.registers[3]) == 2);
    const uint64_t* lf_r3 = impulse_vm_context_lane_frontier_data(ctx, st.registers[3]);
    assert(lf_r3[1] == 1ULL); // Lane 0 -> best hit Node 1
    assert(lf_r3[0] == 2ULL); // Lane 1 -> second hit Node 0

    // 3. Verify R4 after OP_LANE_CSR_WALK has both lanes (0b11 == 3ULL) converged at Node 2
    const uint64_t* lf_r4 = impulse_vm_context_lane_frontier_data(ctx, st.registers[4]);
    assert(lf_r4[2] == 3ULL);

    // 4. Verify OP_VECTOR_RERANK_DOT (0xCB) compacted candidate scores for ascending candidates [0, 1]:
    // R5 (MetricDot, flags=0): cands[0]=Node 0 -> 0.85, cands[1]=Node 1 -> 0.71
    assert(impulse_vm_context_float_vector_size(ctx, st.registers[5]) == 2);
    const float* scores_dot = impulse_vm_context_get_float_vector(ctx, st.registers[5]);
    assert(std::fabs(scores_dot[0] - 0.85f) < 1e-5f);
    assert(std::fabs(scores_dot[1] - 0.71f) < 1e-5f);

    // R6 (MetricL2, flags=1): negative squared L2: cands[0]=Node 0 -> -0.045, cands[1]=Node 1 -> -0.005
    assert(impulse_vm_context_float_vector_size(ctx, st.registers[6]) == 2);
    const float* scores_l2 = impulse_vm_context_get_float_vector(ctx, st.registers[6]);
    assert(std::fabs(scores_l2[0] - (-0.045f)) < 1e-5f);
    assert(std::fabs(scores_l2[1] - (-0.005f)) < 1e-5f);

    // R7 (MetricCosine, flags=2):
    // ||q|| = sqrt(0.85^2 + 0.15^2) = sqrt(0.745)
    // cos(q, Node 0) = 0.85 / sqrt(0.745)
    // cos(q, Node 1) = 0.71 / (sqrt(0.745) * sqrt(0.68))
    const float* scores_cos = impulse_vm_context_get_float_vector(ctx, st.registers[7]);
    float exp_cos0 = 0.85f / std::sqrt(0.745f);
    float exp_cos1 = 0.71f / (std::sqrt(0.745f) * std::sqrt(0.68f));
    assert(std::fabs(scores_cos[0] - exp_cos0) < 1e-5f);
    assert(std::fabs(scores_cos[1] - exp_cos1) < 1e-5f);

    // R8 (MetricHamming, flags=3): all components >= 0.0 -> hamming distance 0 -> score -0.0f
    const float* scores_ham = impulse_vm_context_get_float_vector(ctx, st.registers[8]);
    assert(std::fabs(scores_ham[0] - 0.0f) < 1e-5f);
    assert(std::fabs(scores_ham[1] - 0.0f) < 1e-5f);

    impulse_vm_context_destroy(ctx);
    std::printf("  [PASS] test_vector_ann_mock_l2_and_fused_multilane_handshake\n");
}

static void test_vamana_snapshot_ann_and_quantized_routing() {
    std::printf("--- [Test 9] Vamana Snapshot ANN Search (0xCA), Quantized Routing (BQ1/SQ8/FP16) & Hexio Corruption ---\n");

    const char* snap_path = "test_vamana_phase2.imps";
    const char* mut_path  = "test_vamana_phase2_mut.imps";

    // Build snapshot via official impulse_writer_* APIs (Rule 3.17)
    impulse_writer_t* w = impulse_writer_create(snap_path, 0);
    assert(w != nullptr);
    assert(impulse_writer_add_domain(w, 0, IMPULSE_KEY_TYPE_INT32, "User") == IMPULSE_OK);

    // CSR graph: 0 -> [1, 2], 1 -> [2], 2 -> [3], 3 -> [0]
    const uint32_t row_off[5] = {0, 2, 3, 4, 5};
    const uint32_t col_idx[5] = {1, 2, 2, 3, 0};
    assert(impulse_writer_add_relation(w, 0, 0, IMPULSE_ENC_RAW, 4, 5, 0,
                                       row_off, sizeof(row_off),
                                       col_idx, sizeof(col_idx)) == IMPULSE_OK);

    // Attribute 0 on Relation 0: 4 nodes x 4D Float32 embeddings
    const float embs[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.8f, 0.2f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f
    };
    assert(impulse_writer_add_attribute(w, 0, "embedding", 6, 4, embs, sizeof(embs), nullptr, 0) == IMPULSE_OK);

    // Reserve a 1024-byte 128B-aligned secondary index payload for IMP_INDEX_VAMANA_ANN (type 8)
    // Layout inside index_data (which impulse_writer_finalize places at a 128-byte aligned file offset `vhdr_off`):
    // - [0..63]:    64-byte ImpulseVamanaHeader
    // - [128..191]: 64-byte Vamana fixed-stride adjacency slab (4 nodes * (max_r=3 + 1) * 4B)
    // - [256..259]: QuantBQ1 slab (1 byte per 4D node)
    // - [384..399]: QuantSQ8 slab (4 bytes per 4D node)
    // - [512..543]: QuantFP16 slab (8 bytes per 4D node)
    std::vector<uint8_t> idx_payload(640, 0);
    assert(impulse_writer_add_index(w, 0, 0, 0, 8, "vamana_emb", idx_payload.data(), idx_payload.size(), 0) == IMPULSE_OK);
    assert(impulse_writer_finalize(w) == IMPULSE_OK);
    impulse_writer_destroy(w);

    // Inspect the finalized snapshot to discover the exact 128B-aligned file offset of index 0
    impulse_status_t st_open = IMPULSE_OK;
    impulse_snapshot_t* snap0 = impulse_snapshot_open(snap_path, &st_open);
    assert(snap0 != nullptr && st_open == IMPULSE_OK);
    assert(impulse_snapshot_get_index_count(snap0) == 1);
    const void* idx_ptr = nullptr;
    uint64_t idx_bytes = 0;
    assert(impulse_snapshot_get_index(snap0, 0, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, &idx_ptr, &idx_bytes) == IMPULSE_OK);
    const void* base_ptr = impulse_snapshot_get_buffer(snap0, 0, 64);
    uint64_t vhdr_off_u64 = static_cast<uint64_t>(
        static_cast<const uint8_t*>(idx_ptr) - static_cast<const uint8_t*>(base_ptr)
    );
    assert(vhdr_off_u64 >= 4096 && (vhdr_off_u64 % 128) == 0 && vhdr_off_u64 <= 0xFFFFu);
    uint16_t vhdr_off = static_cast<uint16_t>(vhdr_off_u64);
    impulse_snapshot_close(snap0);

    // Populate the Vamana header, adjacency slab, and quantized routing slabs at `vhdr_off`
    uint64_t adj_slab_off = vhdr_off_u64 + 128;
    uint64_t bq1_off      = vhdr_off_u64 + 256;
    uint64_t sq8_off      = vhdr_off_u64 + 384;
    uint64_t fp16_off     = vhdr_off_u64 + 512;

    auto write_u8 = [&](const char* path, size_t off, uint8_t v) {
        char hex[3];
        std::snprintf(hex, sizeof(hex), "%02X", v);
        assert(impulse_hexio_write_file(path, off, hex) == 1);
    };
    auto write_u16 = [&](const char* path, size_t off, uint16_t v) {
        char hex[5];
        impulse_hexio_render_u16_le(v, hex);
        assert(impulse_hexio_write_file(path, off, hex) == 2);
    };
    auto write_u32 = [&](const char* path, size_t off, uint32_t v) {
        char hex[9];
        impulse_hexio_render_u32_le(v, hex);
        assert(impulse_hexio_write_file(path, off, hex) == 4);
    };
    auto write_u64 = [&](const char* path, size_t off, uint64_t v) {
        char hex[17];
        impulse_hexio_render_u64_le(v, hex);
        assert(impulse_hexio_write_file(path, off, hex) == 8);
    };
    auto write_f32 = [&](const char* path, size_t off, float f) {
        uint32_t bits = 0;
        std::memcpy(&bits, &f, sizeof(float));
        write_u32(path, off, bits);
    };

    write_u32(snap_path, vhdr_off + 0,  0x56414D41u); // "VAMA" magic
    write_u16(snap_path, vhdr_off + 4,  1u);          // version = 1
    write_u8 (snap_path, vhdr_off + 6,  0u);          // metric = 0 (MetricL2)
    write_u8 (snap_path, vhdr_off + 7,  0u);          // quant_type = 0 (QuantNone)
    write_u16(snap_path, vhdr_off + 8,  0u);          // domain_id = 0
    write_u16(snap_path, vhdr_off + 10, 0u);          // vec_attr_id = 0
    write_u32(snap_path, vhdr_off + 12, 4u);          // dimension = 4
    write_u64(snap_path, vhdr_off + 16, 4ULL);        // node_count = 4
    write_u32(snap_path, vhdr_off + 24, 3u);          // max_degree_r = 3
    write_u32(snap_path, vhdr_off + 28, 0u);          // medoid_node_id = 0
    write_f32(snap_path, vhdr_off + 32, 1.2f);        // alpha = 1.2
    write_u32(snap_path, vhdr_off + 36, 16u);         // ef_construction = 16
    write_u64(snap_path, vhdr_off + 40, adj_slab_off);
    write_u64(snap_path, vhdr_off + 48, 64ULL);       // 4 * (3 + 1) * 4 = 64B
    write_u64(snap_path, vhdr_off + 56, 0ULL);        // quant_code_offset = 0

    // Adjacency rows: [deg, nb0, nb1, nb2]
    const uint32_t adj_rows[4][4] = {
        {2, 1, 2, 0},
        {2, 0, 2, 0},
        {2, 1, 3, 0},
        {1, 2, 0, 0}
    };
    for (size_t u = 0; u < 4; ++u) {
        for (size_t j = 0; j < 4; ++j) {
            write_u32(snap_path, static_cast<size_t>(adj_slab_off) + u * 16 + j * 4, adj_rows[u][j]);
        }
    }

    // Quantized slabs: BQ1, SQ8, FP16
    for (size_t u = 0; u < 4; ++u) {
        uint8_t bq_byte = 0;
        for (size_t d = 0; d < 4; ++d) {
            float f = embs[u * 4 + d];
            if (f >= 0.0f) bq_byte |= static_cast<uint8_t>(1u << d);
            int sq_int = std::max(0, std::min(255, static_cast<int>(std::round(f * 127.0f + 128.0f))));
            write_u8(snap_path, static_cast<size_t>(sq8_off) + u * 4 + d, static_cast<uint8_t>(sq_int));
            write_u16(snap_path, static_cast<size_t>(fp16_off) + u * 8 + d * 2, encode_fp16(f));
        }
        write_u8(snap_path, static_cast<size_t>(bq1_off) + u, bq_byte);
    }

    // Test QuantNone (0), QuantBQ1 (1), QuantSQ8 (2), QuantFP16 (3) via Fused 0xCA -> 0xC3 -> 0xCB pipeline!
    const uint8_t quant_types[4] = {0, 1, 2, 3};
    const uint64_t quant_offs[4] = {0ULL, bq1_off, sq8_off, fp16_off};

    for (int qi = 0; qi < 4; ++qi) {
        write_u8(snap_path, vhdr_off + 7, quant_types[qi]);
        write_u64(snap_path, vhdr_off + 56, quant_offs[qi]);

        impulse_snapshot_t* snap = impulse_snapshot_open(snap_path, &st_open);
        assert(snap != nullptr && st_open == IMPULSE_OK);

        impulse_vm_context_t* ctx = impulse_vm_context_create(snap);
        impulse_vm_state_t st{};
        st.query_context = ctx;

        const float q_vec[4] = {0.85f, 0.15f, 0.0f, 0.0f};
        impulse_vm_context_bind_inline_data(ctx, q_vec, sizeof(q_vec));

        impulse_instruction_t fused_prog[5] = {
            {OP_LOAD_INLINE_ARRAY, 0, 1, 0u | (4u << 16)},
            {OP_VECTOR_ANN_SEARCH, 1, 2, 1u | (2u << 8) | (static_cast<uint32_t>(vhdr_off) << 16)},
            {OP_LANE_CSR_WALK, 0, 3, 2u | (0u << 16)},
            {OP_VECTOR_RERANK_DOT, 0, 4, 3u | (1u << 8) | (0u << 16)},
            {OP_HALT, 0, 0, 0}
        };

        g_heap_alloc_count = 0;
        g_track_heap_allocs = true;
        impulse_vm_status_t vm_st = impulse_vm_execute(fused_prog, 5, &st, 0);
        g_track_heap_allocs = false;

        assert(vm_st == IMPULSE_VM_OK);
        assert(g_heap_alloc_count == 0 && "Expected 0 heap allocations during Vamana beam search + rerank!");
        assert(st.register_types[2] == TYPE_LANE_FRONTIER);
        const uint64_t* lf_r2 = impulse_vm_context_lane_frontier_data(ctx, st.registers[2]);
        assert(lf_r2[1] == 1ULL); // Lane 0 seeded at closest Node 1
        assert(lf_r2[0] == 2ULL); // Lane 1 seeded at 2nd closest Node 0

        const uint64_t* lf_r3 = impulse_vm_context_lane_frontier_data(ctx, st.registers[3]);
        assert(lf_r3[2] == 3ULL); // Node 2 reached by both Lane 0 and Lane 1

        // R4 reranked active nodes in R3 ({1, 2}) against snapshot attribute 0
        assert(st.register_types[4] == TYPE_FLOAT_VECTOR);
        assert(impulse_vm_context_float_vector_size(ctx, st.registers[4]) == 2);
        const float* r4_scores = impulse_vm_context_get_float_vector(ctx, st.registers[4]);
        // Node 1 dot([0.85, 0.15, 0, 0], [0.8, 0.2, 0, 0]) = 0.71
        // Node 2 dot([0.85, 0.15, 0, 0], [0.0, 1.0, 0, 0]) = 0.15
        assert(std::fabs(r4_scores[0] - 0.71f) < 1e-5f);
        assert(std::fabs(r4_scores[1] - 0.15f) < 1e-5f);

        impulse_vm_context_destroy(ctx);
        impulse_snapshot_close(snap);
    }

    // Negative test: corrupt VAMA magic -> OP_VECTOR_ANN_SEARCH must return IMPULSE_VM_ERR_OUT_OF_BOUNDS
    {
        FILE* src_f = std::fopen(snap_path, "rb");
        FILE* dst_f = std::fopen(mut_path, "wb");
        assert(src_f && dst_f);
        char buf[4096];
        size_t n = 0;
        while ((n = std::fread(buf, 1, sizeof(buf), src_f)) > 0) {
            std::fwrite(buf, 1, n, dst_f);
        }
        std::fclose(src_f);
        std::fclose(dst_f);

        write_u32(mut_path, vhdr_off + 0, 0x42414421u); // Corrupt magic
        impulse_snapshot_t* bad_snap = impulse_snapshot_open(mut_path, &st_open);
        assert(bad_snap != nullptr);
        impulse_vm_context_t* ctx = impulse_vm_context_create(bad_snap);
        impulse_vm_state_t st{};
        st.query_context = ctx;
        const float q_vec[4] = {0.85f, 0.15f, 0.0f, 0.0f};
        impulse_vm_context_bind_inline_data(ctx, q_vec, sizeof(q_vec));
        impulse_instruction_t bad_prog[3] = {
            {OP_LOAD_INLINE_ARRAY, 0, 1, 0u | (4u << 16)},
            {OP_VECTOR_ANN_SEARCH, 0, 2, 1u | (2u << 8) | (static_cast<uint32_t>(vhdr_off) << 16)},
            {OP_HALT, 0, 0, 0}
        };
        assert(impulse_vm_execute(bad_prog, 3, &st, 0) == IMPULSE_VM_ERR_OUT_OF_BOUNDS);
        impulse_vm_context_destroy(ctx);
        impulse_snapshot_close(bad_snap);
    }

    std::remove(snap_path);
    std::remove(mut_path);
    std::printf("  [PASS] test_vamana_snapshot_ann_and_quantized_routing\n");
}

} // anonymous namespace

int main() {
    std::printf("=========================================================================\n");
    std::printf("     IMPULSE VM MULTI-LANE BIT-SLICED EXECUTION TEST SUITE (0xC0..0xCB)  \n");
    std::printf("=========================================================================\n\n");

    test_active_mask_and_width_tier_selection();
    test_op_lane_init_seeds_and_groups();
    test_op_lane_csr_csc_and_edge_trip_masks();
    test_op_lane_prune_quorum_and_set_cover();
    test_op_lane_reduce_attr_and_argmin_argmax_tie_breaking();
    test_differential_17_boundary_seed_counts_oracle();
    test_zero_heap_allocations_hot_path();
    test_vector_ann_mock_l2_and_fused_multilane_handshake();
    test_vamana_snapshot_ann_and_quantized_routing();

    std::printf("\n=========================================================================\n");
    std::printf("          ALL MULTI-LANE VM EXECUTION & ORACLE TESTS PASSED!             \n");
    std::printf("=========================================================================\n");
    return 0;
}
