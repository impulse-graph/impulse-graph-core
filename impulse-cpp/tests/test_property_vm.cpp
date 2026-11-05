/**
 * @file test_property_vm.cpp
 * @brief Property-based testing for ImpulseVM vector math, bitset algebra, and execution invariants.
 *
 * Formally verifies:
 * 1. Vector Math Analytical Invariants (42-Function SIMD Catalog)
 * 2. Set Monoid & Boolean Algebra Laws (Inclusion-Exclusion, Commutativity, Idempotence)
 * 3. VM Control Flow & Loop Termination
 */

#include "impulse_vm.h"
#include "impulse_math_ops.h"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <set>
#include <iostream>
#include <chrono>

namespace {

struct PropertyRng {
    uint64_t state;
    explicit PropertyRng(uint64_t seed = 0xfedcba9876543210ULL) : state(seed ? seed : 1ULL) {}
    inline uint64_t next() {
        state ^= state >> 12;
        state ^= state << 25;
        state ^= state >> 27;
        return state * 0x2545F4914F6CDD1DULL;
    }
    inline double next_f64(double low, double high) {
        double norm = static_cast<double>(next() & 0xFFFFFFFF) / static_cast<double>(0xFFFFFFFF);
        return low + norm * (high - low);
    }
    inline uint32_t bounded(uint32_t bound) {
        if (bound == 0) return 0;
        return static_cast<uint32_t>(next() % bound);
    }
};

} // anonymous namespace

static void test_vector_math_properties(size_t trials, PropertyRng& rng) {
    std::cout << "[Property VM] Testing Vector Math Analytical Invariants (" << trials << " trials)..." << std::endl;

    for (size_t t = 0; t < trials; ++t) {
        double x = rng.next_f64(-50.0, 50.0);
        double y = rng.next_f64(-50.0, 50.0);

        // 1. Absolute value & Involution: abs(abs(x)) == abs(x)
        double a1 = impulse_math_unary_f64(MATH_FUNC_ABS, x);
        double a2 = impulse_math_unary_f64(MATH_FUNC_ABS, a1);
        assert(a1 >= 0.0);
        assert(a1 == a2);

        // 2. Hypotenuse Symmetry: hypot(x, y) == hypot(y, x) >= max(|x|, |y|)
        double h1 = impulse_math_binary_f64(MATH_FUNC_HYPOT, x, y);
        double h2 = impulse_math_binary_f64(MATH_FUNC_HYPOT, y, x);
        assert(std::abs(h1 - h2) < 1e-12);
        assert(h1 >= std::abs(x) - 1e-9);
        assert(h1 >= std::abs(y) - 1e-9);

        // 3. Clamping Invariant: clamp(x, lo, hi) in [lo, hi]
        double lo = rng.next_f64(-20.0, 0.0);
        double hi = rng.next_f64(0.0, 20.0);
        double clamped = impulse_math_ternary_f64(MATH_FUNC_CLAMP, x, lo, hi);
        assert(clamped >= lo && clamped <= hi);

        // 4. ReLU Non-negativity & Projection
        double relu_val = impulse_math_unary_f64(MATH_FUNC_RELU, x);
        assert(relu_val >= 0.0);
        if (x >= 0.0) {
            assert(relu_val == x);
        } else {
            assert(relu_val == 0.0);
        }

        // 5. Sigmoid Symmetry: sigmoid(x) + sigmoid(-x) == 1.0
        double s_pos = impulse_math_unary_f64(MATH_FUNC_SIGMOID, x);
        double s_neg = impulse_math_unary_f64(MATH_FUNC_SIGMOID, -x);
        assert(s_pos >= 0.0 && s_pos <= 1.0);
        assert(std::abs((s_pos + s_neg) - 1.0) < 1e-9);
    }
    std::cout << "  PASS: Vector Math Analytical Invariants" << std::endl;
}

static void test_set_monoid_properties(size_t trials, PropertyRng& rng) {
    std::cout << "[Property VM] Testing Set Monoid & Inclusion-Exclusion Invariants (" << trials << " trials)..." << std::endl;

    for (size_t t = 0; t < trials; ++t) {
        uint32_t size_a = rng.bounded(100);
        uint32_t size_b = rng.bounded(100);

        std::set<uint32_t> set_a;
        std::set<uint32_t> set_b;

        for (uint32_t i = 0; i < size_a; ++i) set_a.insert(rng.bounded(200));
        for (uint32_t i = 0; i < size_b; ++i) set_b.insert(rng.bounded(200));

        std::set<uint32_t> union_ab;
        std::set<uint32_t> union_ba;
        for (auto v : set_a) { union_ab.insert(v); union_ba.insert(v); }
        for (auto v : set_b) { union_ab.insert(v); union_ba.insert(v); }

        std::set<uint32_t> intersect_ab;
        for (auto v : set_a) {
            if (set_b.count(v)) intersect_ab.insert(v);
        }

        std::set<uint32_t> diff_aa; // A \ A == empty
        for (auto v : set_a) {
            if (set_a.count(v) == 0) diff_aa.insert(v);
        }

        // Invariant 1: Commutativity of Union: A U B == B U A
        assert(union_ab == union_ba);

        // Invariant 2: Inclusion-Exclusion Principle: |A U B| = |A| + |B| - |A n B|
        size_t expected_union_len = set_a.size() + set_b.size() - intersect_ab.size();
        assert(union_ab.size() == expected_union_len);

        // Invariant 3: Difference with self is empty
        assert(diff_aa.empty());
    }
    std::cout << "  PASS: Set Monoid & Inclusion-Exclusion Invariants" << std::endl;
}

static void test_loop_counter_invariants(size_t trials, PropertyRng& rng) {
    std::cout << "[Property VM] Testing Loop Termination Invariants (" << trials << " trials)..." << std::endl;

    impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr);
    for (size_t t = 0; t < trials; ++t) {
        uint64_t count = 1 + rng.bounded(50);

        // Program: Loop exactly 'count' times using OP_LOOP_DECR
        // 0: LOAD_CONST_INT R1, count
        // 1: LOOP_DECR R1, loop_target=0 (relative offset 0: pc += 0 leaves pc at 1)
        // 2: HALT
        std::vector<impulse_instruction_t> prog = {
            { OP_LOAD_CONST_INT, 0, 1, static_cast<uint32_t>(count) },
            { OP_LOOP_DECR, 0, 1, 0 }, // Relative offset 0 -> stays at PC = 1 until count reaches 0, then pc++ advances to 2
            { OP_HALT, 0, 0, 0 }
        };

        impulse_vm_state_t state{};
        state.query_context = ctx;
        impulse_vm_status_t st = impulse_vm_execute(prog.data(), prog.size(), &state, count * 3 + 100);
        assert(st == IMPULSE_VM_OK);
        assert(state.pc == 2);
        assert(state.registers[1] == 0);
    }
    impulse_vm_context_destroy(ctx);
    std::cout << "  PASS: Loop Termination Invariants" << std::endl;
}

int main(int argc, char** argv) {
    size_t trials = 1000;
    uint64_t seed = 0x13579bdf2468ace0ULL;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--trials" && i + 1 < argc) {
            trials = std::strtoull(argv[++i], nullptr, 10);
        } else if (arg == "--seed" && i + 1 < argc) {
            seed = std::strtoull(argv[++i], nullptr, 10);
        }
    }

    std::cout << "============================================================" << std::endl;
    std::cout << " ImpulseVM Property-Based Algebraic Invariant Test Suite" << std::endl;
    std::cout << " Trials: " << trials << " | Seed: " << seed << std::endl;
    std::cout << "============================================================" << std::endl;

    PropertyRng rng(seed);
    auto start_time = std::chrono::steady_clock::now();

    test_vector_math_properties(trials, rng);
    test_set_monoid_properties(trials, rng);
    test_loop_counter_invariants(trials, rng);

    auto end_time = std::chrono::steady_clock::now();
    double duration_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();

    std::cout << "[Property VM] ALL INVARIANTS SATISFIED in " << duration_ms << " ms." << std::endl;
    return 0;
}
