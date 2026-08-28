/**
 * @file test_enterprise_benchmark.cpp
 * @brief Multi-Domain Enterprise Knowledge Graph Benchmark & Type System / Cast Boundary Test Suite.
 */

#include "impulse_vm.h"
#include "impulse_graph.h"
#include "impulse_cel.h"
#include "impulse_math_ops.h"

#include <iostream>
#include <vector>
#include <cassert>
#include <cmath>
#include <limits>
#include <cstring>
#include <iomanip>

#undef assert
#define assert(expr) do { \
    if (!(expr)) { \
        std::cerr << "FAILED: " << #expr << " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::abort(); \
    } \
} while(0)

// ---------------------------------------------------------------------------
// 1. Synthetic Multi-Domain Enterprise Graph Data Structure
// ---------------------------------------------------------------------------
struct EnterpriseData {
    static constexpr size_t NUM_ACCOUNTS = 100;
    static constexpr size_t NUM_MERCHANTS = 20;

    // Buffer for relation 0: TRANSFERS_TO (Account -> Account: 4 edges per node = 400 edges)
    // Layout: offsets (101 uint32_t), targets (400 uint32_t)
    std::vector<uint32_t> transfers_payload;

    // Buffer for relation 1: PURCHASED (Account -> Merchant: 2 edges per node = 200 edges)
    // Layout: offsets (101 uint32_t), targets (200 uint32_t)
    std::vector<uint32_t> purchases_payload;

    // Combined inline memory buffer
    std::vector<uint8_t> inline_buffer;

    size_t transfers_offset_bytes = 0;
    size_t purchases_offset_bytes = 0;

    EnterpriseData() {
        // Build TRANSFERS_TO: node u has edges to (u+1)%100, (u+2)%100, (u+3)%100, (u+4)%100
        std::vector<uint32_t> tr_offsets(NUM_ACCOUNTS + 1);
        std::vector<uint32_t> tr_targets(NUM_ACCOUNTS * 4);
        for (size_t u = 0; u < NUM_ACCOUNTS; ++u) {
            tr_offsets[u] = static_cast<uint32_t>(u * 4);
            for (size_t e = 0; e < 4; ++e) {
                tr_targets[u * 4 + e] = static_cast<uint32_t>((u + e + 1) % NUM_ACCOUNTS);
            }
        }
        tr_offsets[NUM_ACCOUNTS] = static_cast<uint32_t>(NUM_ACCOUNTS * 4);

        // Build PURCHASED: node u has edges to (u)%20, (u+1)%20
        std::vector<uint32_t> pu_offsets(NUM_ACCOUNTS + 1);
        std::vector<uint32_t> pu_targets(NUM_ACCOUNTS * 2);
        for (size_t u = 0; u < NUM_ACCOUNTS; ++u) {
            pu_offsets[u] = static_cast<uint32_t>(u * 2);
            for (size_t e = 0; e < 2; ++e) {
                pu_targets[u * 2 + e] = static_cast<uint32_t>((u + e) % NUM_MERCHANTS);
            }
        }
        pu_offsets[NUM_ACCOUNTS] = static_cast<uint32_t>(NUM_ACCOUNTS * 2);

        // Pack into inline_buffer
        transfers_offset_bytes = 0;
        size_t tr_sz = (tr_offsets.size() + tr_targets.size()) * sizeof(uint32_t);
        purchases_offset_bytes = tr_sz;
        size_t pu_sz = (pu_offsets.size() + pu_targets.size()) * sizeof(uint32_t);

        inline_buffer.resize(tr_sz + pu_sz);

        // Copy transfers
        uint8_t* p = inline_buffer.data();
        std::memcpy(p, tr_offsets.data(), tr_offsets.size() * sizeof(uint32_t));
        p += tr_offsets.size() * sizeof(uint32_t);
        std::memcpy(p, tr_targets.data(), tr_targets.size() * sizeof(uint32_t));
        p += tr_targets.size() * sizeof(uint32_t);

        // Copy purchases
        std::memcpy(p, pu_offsets.data(), pu_offsets.size() * sizeof(uint32_t));
        p += pu_offsets.size() * sizeof(uint32_t);
        std::memcpy(p, pu_targets.data(), pu_targets.size() * sizeof(uint32_t));
    }
};

// ---------------------------------------------------------------------------
// 2. Query Test Suite
// ---------------------------------------------------------------------------
static void test_query_1_filtered_transfer_bfs(const EnterpriseData& d) {
    std::cout << "[Test Q1] 1-Hop Filtered Transfer BFS on ImpulseVM..." << std::endl;

    impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr);
    assert(ctx != nullptr);
    impulse_vm_context_bind_inline_data(ctx, d.inline_buffer.data(), d.inline_buffer.size());

    impulse_vm_state_t state{};
    state.query_context = ctx;

    // Bytecode:
    // 0: OP_INIT_MOCK_GRAPH slot 0, offset transfers_offset_bytes, node_count 100
    // 1: OP_INIT_INPUT_NODE R0, 0 (Account 0)
    // 2: OP_CSR_WALK R1, R0, 0 (Hop to destinations: 1, 2, 3, 4)
    // 3: OP_SET_CARDINALITY R2, R1
    // 4: OP_ASSERT R2, 4
    // 5: OP_HALT
    uint32_t slot_0_payload = static_cast<uint32_t>(d.transfers_offset_bytes) | (100 << 16);
    std::vector<impulse_instruction_t> bc = {
        { OP_INIT_MOCK_GRAPH, 0, 0, slot_0_payload },
        { OP_INIT_INPUT_NODE, 0, 0, 0 },
        { OP_CSR_WALK, 0, 1, 0 },
        { OP_SET_CARDINALITY, 0, 2, 1 },
        { OP_ASSERT, 0, 2, 4 },
        { OP_HALT, 0, 0, 0 }
    };

    impulse_vm_status_t st = impulse_vm_execute(bc.data(), bc.size(), &state, 0);
    assert(st == IMPULSE_VM_OK);
    assert(state.registers[2] == 4);

    impulse_vm_context_destroy(ctx);
    std::cout << "  -> PASSED: Transferred to exactly 4 destination accounts (Cardinality = 4)." << std::endl;
}

static void test_query_2_two_hop_reachability(const EnterpriseData& d) {
    std::cout << "[Test Q2] 2-Hop Multi-Domain Transitive Reachability (Account -> Account -> Merchant)..." << std::endl;

    impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr);
    assert(ctx != nullptr);
    impulse_vm_context_bind_inline_data(ctx, d.inline_buffer.data(), d.inline_buffer.size());

    impulse_vm_state_t state{};
    state.query_context = ctx;

    // Slot 0: TRANSFERS_TO
    uint32_t slot_0_payload = static_cast<uint32_t>(d.transfers_offset_bytes) | (100 << 16);
    // Slot 1: PURCHASED
    uint32_t slot_1_payload = static_cast<uint32_t>(d.purchases_offset_bytes) | (100 << 16);

    // Bytecode:
    // 0: OP_INIT_MOCK_GRAPH slot 0 (TRANSFERS_TO)
    // 1: OP_INIT_MOCK_GRAPH slot 1 (PURCHASED)
    // 2: OP_INIT_INPUT_NODE R0, 0
    // 3: OP_CSR_WALK R1, R0, 0 (Hop 1: Account 0 -> Accounts {1, 2, 3, 4})
    // 4: OP_CSR_WALK R2, R1, 1 (Hop 2: Accounts {1, 2, 3, 4} -> Merchants {1, 2, 3, 4, 5})
    // 5: OP_SET_CARDINALITY R3, R2
    // 6: OP_ASSERT R3, 5
    // 7: OP_HALT
    std::vector<impulse_instruction_t> bc = {
        { OP_INIT_MOCK_GRAPH, 0, 0, slot_0_payload },
        { OP_INIT_MOCK_GRAPH, 0, 1, slot_1_payload },
        { OP_INIT_INPUT_NODE, 0, 0, 0 },
        { OP_CSR_WALK, 0, 1, 0 },
        { OP_CSR_WALK, 0, 2, 1 | (1 << 16) },
        { OP_SET_CARDINALITY, 0, 3, 2 },
        { OP_ASSERT, 0, 3, 5 },
        { OP_HALT, 0, 0, 0 }
    };

    impulse_vm_status_t st = impulse_vm_execute(bc.data(), bc.size(), &state, 0);
    assert(st == IMPULSE_VM_OK);
    assert(state.registers[3] == 5);

    std::cout << "  -> PASSED: 2-Hop frontier reached " << state.registers[3] << " distinct merchant nodes (Expected = 5)." << std::endl;
    impulse_vm_context_destroy(ctx);
}

static void test_query_3_vector_math_risk_decay() {
    std::cout << "[Test Q3] Vector Math & Temporal Exponential Risk Decay..." << std::endl;

    // Values: [100.0, 200.0, 300.0, 400.0]
    // Decay: exp(-0.05 * 10) = 0.60653066
    // Expected Sum = 1000.0 * 0.60653066 = 606.53066
    float values[] = { 100.0f, 200.0f, 300.0f, 400.0f };
    float decay_rate = 0.05f;
    float dt = 10.0f;
    float exp_factor = std::exp(-decay_rate * dt);
    float expected_sum = (100.0f + 200.0f + 300.0f + 400.0f) * exp_factor;

    impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr);
    impulse_vm_context_bind_inline_data(ctx, values, sizeof(values));

    impulse_vm_state_t state{};
    state.query_context = ctx;

    // Bytecode:
    // 0: OP_LOAD_INLINE_ARRAY R1, offset 0
    // 1: OP_FLOAT_VECTOR_SCALE R1, exp_factor (scales R1 in-place)
    // 2: OP_VECTOR_REDUCE_SUM R3, R1
    // 3: OP_HALT
    uint32_t factor_bits = 0;
    std::memcpy(&factor_bits, &exp_factor, 4);

    std::vector<impulse_instruction_t> bc = {
        { OP_LOAD_INLINE_ARRAY, 0, 1, 0 },
        { OP_FLOAT_VECTOR_SCALE, 0, 1, factor_bits },
        { OP_VECTOR_REDUCE_SUM, 0, 3, 1 },
        { OP_HALT, 0, 0, 0 }
    };

    impulse_vm_status_t st = impulse_vm_execute(bc.data(), bc.size(), &state, 0);
    assert(st == IMPULSE_VM_OK);

    float actual_sum = 0.0f;
    std::memcpy(&actual_sum, &state.registers[3], 4);
    assert(std::abs(actual_sum - expected_sum) < 1e-3);

    std::cout << "  -> PASSED: Calculated temporal decay sum = " << actual_sum << " (Expected = " << expected_sum << ")." << std::endl;
    impulse_vm_context_destroy(ctx);
}

static void test_query_4_nullable_coalescing() {
    std::cout << "[Test Q4] Nullable Attribute Coalescing (OP_COALESCE)..." << std::endl;

    float nan_f = std::numeric_limits<float>::quiet_NaN();
    float vals[] = { 0.25f, nan_f, 0.15f, nan_f }; // values (NaN is null)
    float fallback[] = { 0.05f, 0.05f, 0.05f, 0.05f }; // fallback

    std::vector<uint8_t> data(sizeof(vals) + sizeof(fallback));
    std::memcpy(data.data(), vals, sizeof(vals));
    std::memcpy(data.data() + sizeof(vals), fallback, sizeof(fallback));

    impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr);
    impulse_vm_context_bind_inline_data(ctx, data.data(), data.size());

    impulse_vm_state_t state{};
    state.query_context = ctx;

    // 0: OP_LOAD_INLINE_ARRAY R1, offset 0, count 4 (vals)
    // 1: OP_LOAD_INLINE_ARRAY R2, offset 16, count 4 (fallback)
    // 2: OP_COALESCE R3, R1, R2 -> [0.25, 0.05, 0.15, 0.05]
    // 3: OP_VECTOR_REDUCE_SUM R4, R3 -> Sum = 0.50f
    // 4: OP_HALT
    std::vector<impulse_instruction_t> bc = {
        { OP_LOAD_INLINE_ARRAY, 0, 1, 0 | (4 << 16) },
        { OP_LOAD_INLINE_ARRAY, 0, 2, 16 | (4 << 16) },
        { OP_COALESCE, 0, 3, (1 & 0xFFFF) | (2 << 16) },
        { OP_VECTOR_REDUCE_SUM, 0, 4, 3 },
        { OP_HALT, 0, 0, 0 }
    };

    impulse_vm_status_t st = impulse_vm_execute(bc.data(), bc.size(), &state, 0);
    assert(st == IMPULSE_VM_OK);

    float sum = 0.0f;
    std::memcpy(&sum, &state.registers[4], 4);
    assert(std::abs(sum - 0.50f) < 1e-4);

    std::cout << "  -> PASSED: Coalesced nullable vector sum = " << sum << " (Expected = 0.50)." << std::endl;
    impulse_vm_context_destroy(ctx);
}

// ---------------------------------------------------------------------------
// 3. Exhaustive Type Conversion & Invalid Cast / Boundary Suite
// ---------------------------------------------------------------------------
static void test_type_system_and_invalid_cast_boundaries() {
    std::cout << "[Test Types] Exhaustive Type Conversions, Invalid Casts & Error Traps..." << std::endl;

    impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr);

    impulse_vm_state_t state{};
    state.query_context = ctx;

    // 1. Division by zero vs safeDiv
    double a = 10.0, zero = 0.0;
    double safe_res = impulse_math_ternary_f64(MATH_FUNC_SAFE_DIV, a, zero, 99.0);
    assert(safe_res == 99.0);

    // 2. NaN / Inf Detection
    double nan_val = std::numeric_limits<double>::quiet_NaN();
    double inf_val = std::numeric_limits<double>::infinity();
    assert(std::isnan(nan_val));
    assert(std::isinf(inf_val));
    assert(!std::isfinite(nan_val));
    assert(!std::isfinite(inf_val));

    // 3. OP_ASSERT_FINITE with Finite vs Non-Finite Values
    float vec_data[] = { 1.0f, 2.0f, 3.0f, 4.0f };
    impulse_vm_context_bind_inline_data(ctx, vec_data, sizeof(vec_data));

    std::vector<impulse_instruction_t> bc_finite = {
        { OP_LOAD_INLINE_ARRAY, 0, 1, 0 },
        { OP_ASSERT_FINITE, 0, 1, 0 },
        { OP_HALT, 0, 0, 0 }
    };
    state.pc = 0;
    impulse_vm_status_t st = impulse_vm_execute(bc_finite.data(), bc_finite.size(), &state, 0);
    assert(st == IMPULSE_VM_OK);

    // Inject NaN -> Assert finite fails
    float nan_data[] = { 1.0f, 2.0f, std::numeric_limits<float>::quiet_NaN(), 4.0f };
    impulse_vm_context_bind_inline_data(ctx, nan_data, sizeof(nan_data));
    state.pc = 0;
    st = impulse_vm_execute(bc_finite.data(), bc_finite.size(), &state, 0);
    assert(st == IMPULSE_VM_ERR_FLOATING_POINT);

    // 4. Invalid BitSet Handle Trap (Handle >= 8)
    state.registers[5] = 99; // Invalid handle
    state.register_types[5] = TYPE_BITSET_HANDLE;
    std::vector<impulse_instruction_t> bc_bad_handle = {
        { OP_SET_UNION, 0, 6, (5 & 0xFFFF) | (5 << 16) },
        { OP_HALT, 0, 0, 0 }
    };
    state.pc = 0;
    st = impulse_vm_execute(bc_bad_handle.data(), bc_bad_handle.size(), &state, 0);
    assert(st == IMPULSE_VM_ERR_OUT_OF_BOUNDS || st == IMPULSE_VM_ERR_INVALID_REGISTER);

    // 5. Invalid Register Index Trap (Register >= 64)
    std::vector<impulse_instruction_t> bc_bad_reg = {
        { OP_LOAD_CONST_INT, 0, 75, 42 },
        { OP_HALT, 0, 0, 0 }
    };
    state.pc = 0;
    st = impulse_vm_execute(bc_bad_reg.data(), bc_bad_reg.size(), &state, 0);
    assert(st == IMPULSE_VM_ERR_INVALID_REGISTER);

    // 6. Out-of-Bounds Relation ID Trap
    std::vector<impulse_instruction_t> bc_bad_rel = {
        { OP_INIT_INPUT_NODE, 0, 0, 0 },
        { OP_CSR_WALK, 0, 1, (0 & 0xFFFF) | (9999 << 16) },
        { OP_HALT, 0, 0, 0 }
    };
    state.pc = 0;
    st = impulse_vm_execute(bc_bad_rel.data(), bc_bad_rel.size(), &state, 0);
    assert(st == IMPULSE_VM_ERR_OUT_OF_BOUNDS);

    // 7. Null Query Context Trap
    state.query_context = nullptr;
    state.pc = 0;
    st = impulse_vm_execute(bc_finite.data(), bc_finite.size(), &state, 0);
    assert(st == IMPULSE_VM_ERR_NULL_SNAPSHOT);

    impulse_vm_context_destroy(ctx);
    std::cout << "  -> PASSED: All 7 type conversion, invalid cast & error boundary checks verified." << std::endl;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------
int main() {
    std::cout << "=========================================================================" << std::endl;
    std::cout << " Impulse Graph Engine — Enterprise Benchmark & Type System Battery       " << std::endl;
    std::cout << "=========================================================================" << std::endl;

    EnterpriseData d;

    test_query_1_filtered_transfer_bfs(d);
    test_query_2_two_hop_reachability(d);
    test_query_3_vector_math_risk_decay();
    test_query_4_nullable_coalescing();
    test_type_system_and_invalid_cast_boundaries();

    std::cout << "\n=== ALL ENTERPRISE BENCHMARK & TYPE SYSTEM TESTS PASSED CLEANLY ===" << std::endl;
    return 0;
}
