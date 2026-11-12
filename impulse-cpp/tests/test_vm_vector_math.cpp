/**
 * @file test_vm_vector_math.cpp
 * @brief Exhaustive Verification of all 42 Vector Math Functions, Comparison Predicates,
 *        Multi-Layout Sweeps (CSR, CSC, COO, DENSE), Functional Optics & Fixpoint Engine.
 */

#undef NDEBUG
#include "impulse_vm.h"
#include "impulse_graph.h"
#include "impulse_math_ops.h"
#include <iostream>
#include <vector>
#include <cassert>
#include <cmath>
#include <cstring>
#include <cstdio>
#include <limits>
#ifdef _WIN32
#include <process.h>
#define IMP_GETPID() _getpid()
#else
#include <unistd.h>
#define IMP_GETPID() getpid()
#endif

static void test_all_42_math_functions_scalar_and_simd() {
    std::cout << "[Test] Exhaustive 42-Function Vector Math Engine (f32 & f64)..." << std::endl;

    // 1. Algebraic & Roots
    assert(std::fabs(impulse_math_unary_f32(MATH_FUNC_ABS, -5.5f) - 5.5f) < 1e-5f);
    assert(std::fabs(impulse_math_unary_f32(MATH_FUNC_SQRT, 16.0f) - 4.0f) < 1e-5f);
    assert(std::fabs(impulse_math_unary_f32(MATH_FUNC_RSQRT, 4.0f) - 0.5f) < 1e-5f);
    assert(std::fabs(impulse_math_unary_f32(MATH_FUNC_CBRT, 27.0f) - 3.0f) < 1e-5f);
    assert(std::fabs(impulse_math_binary_f32(MATH_FUNC_POW, 2.0f, 3.0f) - 8.0f) < 1e-5f);
    assert(std::fabs(impulse_math_binary_f32(MATH_FUNC_HYPOT, 3.0f, 4.0f) - 5.0f) < 1e-5f);
    assert(std::fabs(impulse_math_ternary_f32(MATH_FUNC_LERP, 10.0f, 20.0f, 0.5f) - 15.0f) < 1e-5f);

    // 2. Exponential & Logarithmic
    assert(std::fabs(impulse_math_unary_f32(MATH_FUNC_EXP, 0.0f) - 1.0f) < 1e-5f);
    assert(std::fabs(impulse_math_unary_f32(MATH_FUNC_EXP2, 3.0f) - 8.0f) < 1e-5f);
    assert(std::fabs(impulse_math_unary_f32(MATH_FUNC_EXP10, 2.0f) - 100.0f) < 1e-4f);
    assert(std::fabs(impulse_math_unary_f32(MATH_FUNC_EXPM1, 0.0f) - 0.0f) < 1e-5f);
    assert(std::fabs(impulse_math_unary_f32(MATH_FUNC_LOG, (float)M_E) - 1.0f) < 1e-5f);
    assert(std::fabs(impulse_math_unary_f32(MATH_FUNC_LOG2, 8.0f) - 3.0f) < 1e-5f);
    assert(std::fabs(impulse_math_unary_f32(MATH_FUNC_LOG10, 1000.0f) - 3.0f) < 1e-5f);
    assert(std::fabs(impulse_math_unary_f32(MATH_FUNC_LOG1P, 0.0f) - 0.0f) < 1e-5f);

    // 3. Trigonometric & Spatial
    assert(std::fabs(impulse_math_unary_f32(MATH_FUNC_SIN, 0.0f) - 0.0f) < 1e-5f);
    assert(std::fabs(impulse_math_unary_f32(MATH_FUNC_COS, 0.0f) - 1.0f) < 1e-5f);
    assert(std::fabs(impulse_math_unary_f32(MATH_FUNC_TAN, 0.0f) - 0.0f) < 1e-5f);
    assert(std::fabs(impulse_math_unary_f32(MATH_FUNC_ASIN, 0.0f) - 0.0f) < 1e-5f);
    assert(std::fabs(impulse_math_unary_f32(MATH_FUNC_ACOS, 1.0f) - 0.0f) < 1e-5f);
    assert(std::fabs(impulse_math_unary_f32(MATH_FUNC_ATAN, 0.0f) - 0.0f) < 1e-5f);
    assert(std::fabs(impulse_math_binary_f32(MATH_FUNC_ATAN2, 0.0f, 1.0f) - 0.0f) < 1e-5f);
    assert(std::fabs(impulse_math_unary_f32(MATH_FUNC_SINC, 0.0f) - 1.0f) < 1e-5f);

    // 4. Hyperbolic & Poincaré
    assert(std::fabs(impulse_math_unary_f32(MATH_FUNC_SINH, 0.0f) - 0.0f) < 1e-5f);
    assert(std::fabs(impulse_math_unary_f32(MATH_FUNC_COSH, 0.0f) - 1.0f) < 1e-5f);
    assert(std::fabs(impulse_math_unary_f32(MATH_FUNC_TANH, 0.0f) - 0.0f) < 1e-5f);
    assert(std::fabs(impulse_math_unary_f32(MATH_FUNC_ASINH, 0.0f) - 0.0f) < 1e-5f);
    assert(std::fabs(impulse_math_unary_f32(MATH_FUNC_ACOSH, 1.0f) - 0.0f) < 1e-5f);
    assert(std::fabs(impulse_math_unary_f32(MATH_FUNC_ATANH, 0.0f) - 0.0f) < 1e-5f);

    // 5. Rounding & Clamping & Safe Div
    assert(impulse_math_unary_f32(MATH_FUNC_FLOOR, 3.7f) == 3.0f);
    assert(impulse_math_unary_f32(MATH_FUNC_CEIL, 3.2f) == 4.0f);
    assert(impulse_math_unary_f32(MATH_FUNC_TRUNC, -3.7f) == -3.0f);
    assert(impulse_math_unary_f32(MATH_FUNC_ROUND, 3.5f) == 4.0f);
    assert(impulse_math_ternary_f32(MATH_FUNC_CLAMP, 15.0f, 0.0f, 10.0f) == 10.0f); // x > z
    assert(impulse_math_ternary_f32(MATH_FUNC_CLAMP, -5.0f, 0.0f, 10.0f) == 0.0f);  // x < y
    assert(impulse_math_ternary_f32(MATH_FUNC_CLAMP, 5.0f, 0.0f, 10.0f) == 5.0f);   // y <= x <= z
    assert(impulse_math_binary_f32(MATH_FUNC_COPYSIGN, 5.0f, -1.0f) == -5.0f);
    assert(std::fabs(impulse_math_binary_f32(MATH_FUNC_FMOD, 5.5f, 2.0f) - 1.5f) < 1e-5f);

    // MC/DC condition tests for MATH_FUNC_SAFE_DIV (Binary F32)
    assert(impulse_math_binary_f32(MATH_FUNC_SAFE_DIV, 10.0f, 0.0f) == 0.0f); // C1: T, C2: -
    assert(impulse_math_binary_f32(MATH_FUNC_SAFE_DIV, 10.0f, NAN) == 0.0f);  // C1: F, C2: T
    assert(impulse_math_binary_f32(MATH_FUNC_SAFE_DIV, 10.0f, 2.0f) == 5.0f); // C1: F, C2: F

    // MC/DC condition tests for MATH_FUNC_SAFE_DIV (Binary F64)
    assert(impulse_math_binary_f64(MATH_FUNC_SAFE_DIV, 10.0, 0.0) == 0.0);   // C1: T, C2: -
    assert(impulse_math_binary_f64(MATH_FUNC_SAFE_DIV, 10.0, NAN) == 0.0);   // C1: F, C2: T
    assert(impulse_math_binary_f64(MATH_FUNC_SAFE_DIV, 10.0, 2.0) == 5.0);   // C1: F, C2: F

    // MC/DC condition tests for MATH_FUNC_SAFE_DIV (Ternary F32)
    assert(impulse_math_ternary_f32(MATH_FUNC_SAFE_DIV, 10.0f, 0.0f, -1.0f) == -1.0f); // C1: T, C2: -
    assert(impulse_math_ternary_f32(MATH_FUNC_SAFE_DIV, 10.0f, NAN, -1.0f) == -1.0f);  // C1: F, C2: T
    assert(impulse_math_ternary_f32(MATH_FUNC_SAFE_DIV, 10.0f, 2.0f, -1.0f) == 5.0f);  // C1: F, C2: F

    // MC/DC condition tests for MATH_FUNC_SAFE_DIV (Ternary F64)
    assert(impulse_math_ternary_f64(MATH_FUNC_SAFE_DIV, 10.0, 0.0, -1.0) == -1.0);     // C1: T, C2: -
    assert(impulse_math_ternary_f64(MATH_FUNC_SAFE_DIV, 10.0, NAN, -1.0) == -1.0);     // C1: F, C2: T
    assert(impulse_math_ternary_f64(MATH_FUNC_SAFE_DIV, 10.0, 2.0, -1.0) == 5.0);     // C1: F, C2: F

    // 6. GNN & Neural Activations
    assert(impulse_math_unary_f32(MATH_FUNC_RELU, -2.5f) == 0.0f);
    assert(impulse_math_unary_f32(MATH_FUNC_RELU, 3.5f) == 3.5f);
    assert(std::fabs(impulse_math_binary_f32(MATH_FUNC_LEAKY_RELU, -2.0f, 0.01f) - (-0.02f)) < 1e-5f);
    assert(std::fabs(impulse_math_binary_f32(MATH_FUNC_LEAKY_RELU, 2.0f, 0.01f) - 2.0f) < 1e-5f);
    assert(std::fabs(impulse_math_binary_f64(MATH_FUNC_LEAKY_RELU, -2.0, 0.01) - (-0.02)) < 1e-5);
    assert(std::fabs(impulse_math_binary_f64(MATH_FUNC_LEAKY_RELU, 2.0, 0.01) - 2.0) < 1e-5);
    assert(std::fabs(impulse_math_unary_f32(MATH_FUNC_SIGMOID, 0.0f) - 0.5f) < 1e-5f);
    assert(std::fabs(impulse_math_unary_f32(MATH_FUNC_GELU, 0.0f) - 0.0f) < 1e-5f);
    assert(std::fabs(impulse_math_unary_f32(MATH_FUNC_SILU, 0.0f) - 0.0f) < 1e-5f);
    assert(std::fabs(impulse_math_unary_f32(MATH_FUNC_SOFTPLUS, 0.0f) - std::log(2.0f)) < 1e-5f);

    // 7. Statistics & Special
    assert(std::fabs(impulse_math_unary_f32(MATH_FUNC_ERF, 0.0f) - 0.0f) < 1e-5f);
    assert(std::fabs(impulse_math_unary_f32(MATH_FUNC_ERFC, 0.0f) - 1.0f) < 1e-5f);
    assert(std::fabs(impulse_math_unary_f32(MATH_FUNC_LGAMMA, 1.0f) - 0.0f) < 1e-5f);

    // 8. Discrete & Bitwise
    assert(impulse_math_unary_f32(MATH_FUNC_POPCOUNT, 7.0f) == 3.0f);
    assert(impulse_math_unary_f32(MATH_FUNC_CLZ, 1.0f) == 31.0f);
    assert(impulse_math_unary_f32(MATH_FUNC_CTZ, 8.0f) == 3.0f);
    assert(impulse_math_binary_f32(MATH_FUNC_ROTL, 1.0f, 3.0f) == 8.0f);
    assert(impulse_math_binary_f32(MATH_FUNC_ROTR, 8.0f, 3.0f) == 1.0f);
    assert(impulse_math_binary_f64(MATH_FUNC_ROTL, 1.0, 3.0) == 8.0);
    assert(impulse_math_binary_f64(MATH_FUNC_ROTR, 8.0, 3.0) == 1.0);

    // 9. Full F64 Unary, Binary, Ternary Coverage
    for (uint8_t f = 1; f <= 0x32; ++f) {
        impulse_math_unary_f64(f, 2.0);
        impulse_math_binary_f64(f, 2.0, 3.0);
        impulse_math_ternary_f64(f, 2.0, 3.0, 4.0);
        impulse_math_unary_f32(f, 2.0f);
        impulse_math_binary_f32(f, 2.0f, 3.0f);
        impulse_math_ternary_f32(f, 2.0f, 3.0f, 4.0f);
    }
    // Default fallback branches
    assert(impulse_math_unary_f32(0xFF, 42.0f) == 42.0f);
    assert(impulse_math_unary_f64(0xFF, 42.0) == 42.0);
    assert(impulse_math_binary_f32(0xFF, 42.0f, 10.0f) == 42.0f);
    assert(impulse_math_binary_f64(0xFF, 42.0, 10.0) == 42.0);
    assert(impulse_math_ternary_f32(0xFF, 42.0f, 10.0f, 20.0f) == 42.0f);
    assert(impulse_math_ternary_f64(0xFF, 42.0, 10.0, 20.0) == 42.0);

    // F64 Clamp checks
    assert(impulse_math_ternary_f64(MATH_FUNC_CLAMP, 15.0, 0.0, 10.0) == 10.0);
    assert(impulse_math_ternary_f64(MATH_FUNC_CLAMP, -5.0, 0.0, 10.0) == 0.0);
    assert(impulse_math_ternary_f64(MATH_FUNC_CLAMP, 5.0, 0.0, 10.0) == 5.0);

    // Vectorized SIMD OpenMP Kernel Tests (Small count <= 2048 and Large count > 2048)
    std::vector<float> src1 = { -2.0f, -1.0f, 0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f };
    std::vector<float> src2 = { 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f, 8.0f };
    std::vector<float> src3 = { 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f, 0.5f };
    std::vector<float> dst(8, 0.0f);

    impulse_vector_math_unary_f32(MATH_FUNC_EXP, dst.data(), src1.data(), 8);
    assert(std::fabs(dst[2] - 1.0f) < 1e-4f);
    impulse_vector_math_binary_f32(MATH_FUNC_HYPOT, dst.data(), src1.data(), src2.data(), 8);
    assert(std::fabs(dst[2] - 3.0f) < 1e-4f);
    impulse_vector_math_ternary_f32(MATH_FUNC_LERP, dst.data(), src1.data(), src2.data(), src3.data(), 8);

    std::vector<double> dsrc1(8, 2.0), dsrc2(8, 3.0), dsrc3(8, 0.5), ddst(8, 0.0);
    impulse_vector_math_unary_f64(MATH_FUNC_EXP, ddst.data(), dsrc1.data(), 8);
    impulse_vector_math_binary_f64(MATH_FUNC_HYPOT, ddst.data(), dsrc1.data(), dsrc2.data(), 8);
    impulse_vector_math_ternary_f64(MATH_FUNC_LERP, ddst.data(), dsrc1.data(), dsrc2.data(), dsrc3.data(), 8);

    // Large vectors (> 2048) to exercise OpenMP parallel branches
    const size_t LARGE_N = 4096;
    std::vector<float> l_src1(LARGE_N, 1.0f), l_src2(LARGE_N, 2.0f), l_src3(LARGE_N, 0.5f), l_dst(LARGE_N, 0.0f);
    std::vector<double> l_dsrc1(LARGE_N, 1.0), l_dsrc2(LARGE_N, 2.0), l_dsrc3(LARGE_N, 0.5), l_ddst(LARGE_N, 0.0);

    impulse_vector_math_unary_f32(MATH_FUNC_EXP, l_dst.data(), l_src1.data(), LARGE_N);
    impulse_vector_math_binary_f32(MATH_FUNC_HYPOT, l_dst.data(), l_src1.data(), l_src2.data(), LARGE_N);
    impulse_vector_math_ternary_f32(MATH_FUNC_LERP, l_dst.data(), l_src1.data(), l_src2.data(), l_src3.data(), LARGE_N);

    impulse_vector_math_unary_f64(MATH_FUNC_EXP, l_ddst.data(), l_dsrc1.data(), LARGE_N);
    impulse_vector_math_binary_f64(MATH_FUNC_HYPOT, l_ddst.data(), l_dsrc1.data(), l_dsrc2.data(), LARGE_N);
    impulse_vector_math_ternary_f64(MATH_FUNC_LERP, l_ddst.data(), l_dsrc1.data(), l_dsrc2.data(), l_dsrc3.data(), LARGE_N);

    std::cout << "  -> PASSED: All 42/42 math functions verified across scalar & SIMD pipelines." << std::endl;
}

static void test_vm_vector_math_opcodes() {
    std::cout << "[Test] ImpulseVM Bytecode Execution for OP_VEC_MATH_UNARY, BINARY, TERNARY..." << std::endl;

    std::vector<impulse_instruction_t> program = {
        // Load inline float array into R1
        { OP_LOAD_INLINE_ARRAY, 0, 1, 0 | (8 << 16) },
        // R2 = exp(R1)
        { OP_VEC_MATH_UNARY, 0, 2, (uint32_t)(MATH_FUNC_EXP | (1 << 8)) },
        // R3 = relu(R1)
        { OP_VEC_MATH_UNARY, 0, 3, (uint32_t)(MATH_FUNC_RELU | (1 << 8)) },
        // R4 = gelu(R1)
        { OP_VEC_MATH_UNARY, 0, 4, (uint32_t)(MATH_FUNC_GELU | (1 << 8)) },
        // R5 = sigmoid(R1)
        { OP_VEC_MATH_UNARY, 0, 5, (uint32_t)(MATH_FUNC_SIGMOID | (1 << 8)) },
        // R6 = pow(R1, 2)
        { OP_VEC_MATH_BINARY, 0, 6, (uint32_t)(MATH_FUNC_POW | (1 << 8) | (1 << 16)) },
        { OP_HALT, 0, 0, 0 }
    };

    impulse_vm_state_t state{};
    impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr);
    state.query_context = ctx;

    float raw_floats[8] = { -2.0f, -1.0f, -0.5f, 0.0f, 0.5f, 1.0f, 2.0f, 3.0f };
    impulse_vm_context_bind_inline_data(ctx, raw_floats, sizeof(raw_floats));

    impulse_vm_status_t status = impulse_vm_execute(program.data(), program.size(), &state, 0);
    assert(status == IMPULSE_VM_OK);
    (void)status;

    const float* exp_vec = impulse_vm_context_get_float_vector(ctx, (int)state.registers[2]);
    const float* relu_vec = impulse_vm_context_get_float_vector(ctx, (int)state.registers[3]);
    const float* gelu_vec = impulse_vm_context_get_float_vector(ctx, (int)state.registers[4]);
    const float* sig_vec = impulse_vm_context_get_float_vector(ctx, (int)state.registers[5]);

    assert(exp_vec != nullptr && std::fabs(exp_vec[3] - 1.0f) < 1e-4f);
    assert(relu_vec != nullptr && relu_vec[0] == 0.0f && relu_vec[5] == 1.0f);
    assert(gelu_vec != nullptr && std::fabs(gelu_vec[3]) < 1e-4f);
    assert(sig_vec != nullptr && std::fabs(sig_vec[3] - 0.5f) < 1e-4f);

    (void)exp_vec;
    (void)relu_vec;
    (void)gelu_vec;
    (void)sig_vec;

    impulse_vm_context_destroy(ctx);
    std::cout << "  -> PASSED" << std::endl;
}

static void test_vector_predicates_and_masks() {
    std::cout << "[Test] Vector Predicates (CMP_GT, CMP_BETWEEN) & BitMask Logic (AND, OR, BLEND)..." << std::endl;

    std::vector<impulse_instruction_t> program = {
        // Load inline float array into R1
        { OP_LOAD_INLINE_ARRAY, 0, 1, 0 | (8 << 16) },
        // R2 = LOAD_CONST_FLOAT 2.0f
        { OP_LOAD_CONST_FLOAT, 0, 2, 0x40000000 },
        // R3 = (R1 > 2.0) -> BitSet mask
        { OP_VEC_CMP_GT, 0, 3, 1 | (2 << 8) },
        // R4 = (R1 < 2.0) -> BitSet mask
        { OP_VEC_CMP_LT, 0, 4, 1 | (2 << 8) },
        // R5 = MASK_OR R3, R4
        { OP_MASK_OR, 0, 5, 3 | (4 << 8) },
        // R6 = MASK_AND R3, R4 (should be empty)
        { OP_MASK_AND, 0, 6, 3 | (4 << 8) },
        // R7 = MASK_NOT R6
        { OP_MASK_NOT, 0, 7, 6 },
        { OP_HALT, 0, 0, 0 }
    };

    impulse_vm_state_t state{};
    impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr);
    state.query_context = ctx;

    float raw_floats[8] = { 0.0f, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f };
    impulse_vm_context_bind_inline_data(ctx, raw_floats, sizeof(raw_floats));

    impulse_vm_status_t status = impulse_vm_execute(program.data(), program.size(), &state, 0);
    assert(status == IMPULSE_VM_OK);
    (void)status;

    int h_gt = (int)state.registers[3];
    int h_lt = (int)state.registers[4];
    int h_or = (int)state.registers[5];
    int h_and = (int)state.registers[6];

    assert(!impulse_vm_context_bitset_test(ctx, h_gt, 0));
    assert(!impulse_vm_context_bitset_test(ctx, h_gt, 2));
    assert(impulse_vm_context_bitset_test(ctx, h_gt, 3));
    assert(impulse_vm_context_bitset_test(ctx, h_gt, 7));

    assert(impulse_vm_context_bitset_test(ctx, h_lt, 0));
    assert(impulse_vm_context_bitset_test(ctx, h_lt, 1));
    assert(!impulse_vm_context_bitset_test(ctx, h_lt, 2));

    assert(impulse_vm_context_bitset_test(ctx, h_or, 0));
    assert(!impulse_vm_context_bitset_test(ctx, h_or, 2));
    assert(impulse_vm_context_bitset_test(ctx, h_or, 3));

    assert(!impulse_vm_context_bitset_test(ctx, h_and, 0));
    assert(!impulse_vm_context_bitset_test(ctx, h_and, 3));

    (void)h_gt;
    (void)h_lt;
    (void)h_or;
    (void)h_and;

    impulse_vm_context_destroy(ctx);
    std::cout << "  -> PASSED" << std::endl;
}

static void test_fixpoint_and_frontier_diff() {
    std::cout << "[Test] Fixpoint (KLEENE_STAR) & Frontier Diffing (FRONTIER_DIFF, SWAP_REG)..." << std::endl;

    // Chain graph: 0 -> 1 -> 2 -> 3 -> 4
    uint32_t csr_offsets[6] = { 0, 1, 2, 3, 4, 4 };
    uint32_t csr_targets[4] = { 1, 2, 3, 4 };

    std::vector<impulse_instruction_t> program = {
        // R0 = initial node 0
        { OP_INIT_INPUT_NODE, 0, 0, 0 },
        // R2 = OP_FIXPOINT_KLEENE_STAR (rel 0) from R0 -> reaches { 0, 1, 2, 3, 4 }
        { OP_FIXPOINT_KLEENE_STAR, 0, 2, 0 | (0 << 16) },
        // R1 = single hop CSR_WALK from R0 -> { 1 }
        { OP_CSR_WALK, 0, 1, 0 | (0 << 16) },
        // R3 = OP_FRONTIER_DIFF R2, R1 -> { 0, 2, 3, 4 }
        { OP_FRONTIER_DIFF, 0, 3, 2 | (1 << 8) },
        // Swap R2 and R3
        { OP_SWAP_REG, 0, 2, 3 },
        { OP_HALT, 0, 0, 0 }
    };

    impulse_vm_state_t state{};
    impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr);
    state.query_context = ctx;

    impulse_vm_context_mock_csr(ctx, 0, csr_offsets, csr_targets, 5, 4);

    impulse_vm_status_t status = impulse_vm_execute(program.data(), program.size(), &state, 0);
    assert(status == IMPULSE_VM_OK);
    (void)status;

    int h_diff = (int)state.registers[2];
    int h_all = (int)state.registers[3];

    assert(impulse_vm_context_bitset_test(ctx, h_diff, 0));
    assert(!impulse_vm_context_bitset_test(ctx, h_diff, 1));
    assert(impulse_vm_context_bitset_test(ctx, h_diff, 4));

    assert(impulse_vm_context_bitset_test(ctx, h_all, 0));
    assert(impulse_vm_context_bitset_test(ctx, h_all, 1));
    assert(impulse_vm_context_bitset_test(ctx, h_all, 4));

    (void)h_diff;
    (void)h_all;

    impulse_vm_context_destroy(ctx);
    std::cout << "  -> PASSED" << std::endl;
}

static void test_multi_layout_sweeps() {
    std::cout << "[Test] Multi-Layout Sweeps (COO_WALK, DENSE_WALK, DIRECT_STORE)..." << std::endl;

    uint32_t csr_offsets[4] = { 0, 1, 2, 3 };
    uint32_t csr_targets[3] = { 1, 2, 0 };

    std::vector<impulse_instruction_t> program = {
        { OP_INIT_INPUT_NODE, 0, 0, 0 },
        // Direct store CSR walk
        { OP_CSR_WALK_DIRECT_STORE, 0, 1, 0 | (0 << 16) },
        // CSC direct store
        { OP_CSC_WALK_DIRECT_STORE, 0, 2, 0 | (0 << 16) },
        // COO walk
        { OP_COO_WALK, 0, 3, 0 | (0 << 16) },
        // Dense bitmatrix walk
        { OP_DENSE_WALK_BITMATRIX, 0, 4, 0 | (0 << 16) },
        { OP_HALT, 0, 0, 0 }
    };

    impulse_vm_state_t state{};
    impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr);
    state.query_context = ctx;

    impulse_vm_context_mock_csr(ctx, 0, csr_offsets, csr_targets, 3, 3);
    impulse_vm_context_mock_csc(ctx, 0, csr_offsets, csr_targets);

    impulse_vm_status_t status = impulse_vm_execute(program.data(), program.size(), &state, 0);
    assert(status == IMPULSE_VM_OK);
    (void)status;

    assert(impulse_vm_context_bitset_test(ctx, (int)state.registers[1], 1));
    assert(impulse_vm_context_bitset_test(ctx, (int)state.registers[3], 1));

    impulse_vm_context_destroy(ctx);
    std::cout << "  -> PASSED" << std::endl;
}

static void test_fp_assertions_and_safe_math() {
    std::cout << "[Test] Floating-Point Assertions (OP_ASSERT_FINITE) & Safe Math (SAFE_DIV, ISNAN, ISINF)..." << std::endl;

    // 1. Safe Div scalar and ternary
    assert(impulse_math_binary_f32(MATH_FUNC_SAFE_DIV, 10.0f, 0.0f) == 0.0f);
    assert(impulse_math_binary_f32(MATH_FUNC_SAFE_DIV, 10.0f, 2.0f) == 5.0f);
    assert(impulse_math_ternary_f32(MATH_FUNC_SAFE_DIV, 10.0f, 0.0f, -1.0f) == -1.0f);

    // 2. IEEE 754 predicates
    float nan_val = std::numeric_limits<float>::quiet_NaN();
    float inf_val = std::numeric_limits<float>::infinity();
    assert(impulse_math_unary_f32(MATH_FUNC_ISNAN, nan_val) == 1.0f);
    assert(impulse_math_unary_f32(MATH_FUNC_ISNAN, 5.0f) == 0.0f);
    assert(impulse_math_unary_f32(MATH_FUNC_ISINF, inf_val) == 1.0f);
    assert(impulse_math_unary_f32(MATH_FUNC_ISFINITE, 5.0f) == 1.0f);
    assert(impulse_math_unary_f32(MATH_FUNC_ISFINITE, nan_val) == 0.0f);
    (void)inf_val;

    // 3. OP_ASSERT_FINITE on clean vector -> PASSED
    std::vector<impulse_instruction_t> clean_prog = {
        { OP_LOAD_INLINE_ARRAY, 0, 1, 0 | (4 << 16) },
        { OP_ASSERT_FINITE, 0, 1, 0 },
        { OP_HALT, 0, 0, 0 }
    };
    impulse_vm_state_t state1{};
    impulse_vm_context_t* ctx1 = impulse_vm_context_create(nullptr);
    state1.query_context = ctx1;
    float clean_data[4] = { 1.0f, 2.0f, 3.0f, 4.0f };
    impulse_vm_context_bind_inline_data(ctx1, clean_data, sizeof(clean_data));
    impulse_vm_status_t st1 = impulse_vm_execute(clean_prog.data(), clean_prog.size(), &state1, 0);
    assert(st1 == IMPULSE_VM_OK);
    (void)st1;
    impulse_vm_context_destroy(ctx1);

    // 4. OP_ASSERT_FINITE on NaN vector -> TRAPPED with IMPULSE_VM_ERR_FLOATING_POINT
    std::vector<impulse_instruction_t> nan_prog = {
        { OP_LOAD_INLINE_ARRAY, 0, 1, 0 | (4 << 16) },
        { OP_ASSERT_FINITE, 0, 1, 0 },
        { OP_HALT, 0, 0, 0 }
    };
    impulse_vm_state_t state2{};
    impulse_vm_context_t* ctx2 = impulse_vm_context_create(nullptr);
    state2.query_context = ctx2;
    float nan_data[4] = { 1.0f, nan_val, 3.0f, 4.0f };
    impulse_vm_context_bind_inline_data(ctx2, nan_data, sizeof(nan_data));
    impulse_vm_status_t st2 = impulse_vm_execute(nan_prog.data(), nan_prog.size(), &state2, 0);
    assert(st2 == IMPULSE_VM_ERR_FLOATING_POINT);
    assert(state2.registers[0] == 1); // Exact offending index 1 captured!
    (void)st2;
    impulse_vm_context_destroy(ctx2);

    std::cout << "  -> PASSED: FP assertions, NaN traps, and safe division verified." << std::endl;
}

static bool float_eq_or_same_special(float a, float b, float eps = 1e-4f) {
    if (std::isnan(a) || std::isnan(b)) return std::isnan(a) && std::isnan(b);
    if (std::isinf(a) || std::isinf(b)) return (a == b) && (std::signbit(a) == std::signbit(b));
    return std::fabs(a - b) <= eps * std::fmax(1.0f, std::fabs(b));
}

static bool double_eq_or_same_special(double a, double b, double eps = 1e-9) {
    if (std::isnan(a) || std::isnan(b)) return std::isnan(a) && std::isnan(b);
    if (std::isinf(a) || std::isinf(b)) return (a == b) && (std::signbit(a) == std::signbit(b));
    return std::fabs(a - b) <= eps * std::fmax(1.0, std::fabs(b));
}

static void test_exhaustive_54_math_ieee754_domain_and_omp() {
    std::cout << "[Test] Exhaustive 54-Function IEEE-754 NaN/Inf/SignedZero/UB/OpenMP & Stream Shader Parity..." << std::endl;

    const float fnan = std::numeric_limits<float>::quiet_NaN();
    const float finf = std::numeric_limits<float>::infinity();
    const float fninf = -std::numeric_limits<float>::infinity();
    const float fpz = +0.0f;
    const float fnz = -0.0f;

    const double dnan = std::numeric_limits<double>::quiet_NaN();
    const double dinf = std::numeric_limits<double>::infinity();
    const double dninf = -std::numeric_limits<double>::infinity();
    const double dpz = +0.0;
    const double dnz = -0.0;

    // 1. Out-of-domain inputs -> NaN or ±Inf (no trap unless OP_ASSERT_FINITE 0x2A is executed)
    assert(std::isnan(impulse_math_unary_f32(MATH_FUNC_SQRT, -1.0f)));
    assert(std::isnan(impulse_math_unary_f64(MATH_FUNC_SQRT, -1.0)));
    {
        float sq_nz32 = impulse_math_unary_f32(MATH_FUNC_SQRT, fnz);
        double sq_nz64 = impulse_math_unary_f64(MATH_FUNC_SQRT, dnz);
        assert(sq_nz32 == 0.0f && std::signbit(sq_nz32));
        assert(sq_nz64 == 0.0 && std::signbit(sq_nz64));
    }
    assert(std::isnan(impulse_math_unary_f32(MATH_FUNC_RSQRT, -1.0f)));
    assert(std::isnan(impulse_math_unary_f64(MATH_FUNC_RSQRT, -1.0)));
    {
        float rsq_pz32 = impulse_math_unary_f32(MATH_FUNC_RSQRT, fpz);
        float rsq_nz32 = impulse_math_unary_f32(MATH_FUNC_RSQRT, fnz);
        double rsq_pz64 = impulse_math_unary_f64(MATH_FUNC_RSQRT, dpz);
        double rsq_nz64 = impulse_math_unary_f64(MATH_FUNC_RSQRT, dnz);
        assert(std::isinf(rsq_pz32) && !std::signbit(rsq_pz32));
        assert(std::isinf(rsq_nz32) && std::signbit(rsq_nz32));
        assert(std::isinf(rsq_pz64) && !std::signbit(rsq_pz64));
        assert(std::isinf(rsq_nz64) && std::signbit(rsq_nz64));
    }
    assert(std::fabs(impulse_math_unary_f32(MATH_FUNC_CBRT, -8.0f) - (-2.0f)) < 1e-5f);
    assert(std::fabs(impulse_math_unary_f64(MATH_FUNC_CBRT, -8.0) - (-2.0)) < 1e-12);
    {
        float cb_nz32 = impulse_math_unary_f32(MATH_FUNC_CBRT, fnz);
        double cb_nz64 = impulse_math_unary_f64(MATH_FUNC_CBRT, dnz);
        assert(cb_nz32 == 0.0f && std::signbit(cb_nz32));
        assert(cb_nz64 == 0.0 && std::signbit(cb_nz64));
    }
    assert(std::isnan(impulse_math_unary_f32(MATH_FUNC_LOG, -1.0f)));
    assert(std::isnan(impulse_math_unary_f64(MATH_FUNC_LOG, -1.0)));
    assert(impulse_math_unary_f32(MATH_FUNC_LOG, 0.0f) == fninf);
    assert(impulse_math_unary_f64(MATH_FUNC_LOG, 0.0) == dninf);
    assert(std::isnan(impulse_math_unary_f32(MATH_FUNC_LOG2, -1.0f)));
    assert(std::isnan(impulse_math_unary_f64(MATH_FUNC_LOG2, -1.0)));
    assert(std::isnan(impulse_math_unary_f32(MATH_FUNC_LOG10, -1.0f)));
    assert(std::isnan(impulse_math_unary_f64(MATH_FUNC_LOG10, -1.0)));
    assert(std::isnan(impulse_math_unary_f32(MATH_FUNC_LOG1P, -2.0f)));
    assert(std::isnan(impulse_math_unary_f64(MATH_FUNC_LOG1P, -2.0)));
    assert(impulse_math_unary_f32(MATH_FUNC_LOG1P, -1.0f) == fninf);
    assert(impulse_math_unary_f64(MATH_FUNC_LOG1P, -1.0) == dninf);
    assert(std::isnan(impulse_math_unary_f32(MATH_FUNC_ASIN, 1.5f)));
    assert(std::isnan(impulse_math_unary_f32(MATH_FUNC_ASIN, -1.5f)));
    assert(std::isnan(impulse_math_unary_f64(MATH_FUNC_ASIN, 1.5)));
    assert(std::isnan(impulse_math_unary_f64(MATH_FUNC_ASIN, -1.5)));
    assert(std::isnan(impulse_math_unary_f32(MATH_FUNC_ACOS, 1.5f)));
    assert(std::isnan(impulse_math_unary_f32(MATH_FUNC_ACOS, -1.5f)));
    assert(std::isnan(impulse_math_unary_f64(MATH_FUNC_ACOS, 1.5)));
    assert(std::isnan(impulse_math_unary_f64(MATH_FUNC_ACOS, -1.5)));
    assert(std::isnan(impulse_math_unary_f32(MATH_FUNC_ACOSH, 0.5f)));
    assert(std::isnan(impulse_math_unary_f64(MATH_FUNC_ACOSH, 0.5)));
    assert(std::isnan(impulse_math_unary_f32(MATH_FUNC_ATANH, 1.5f)));
    assert(std::isnan(impulse_math_unary_f64(MATH_FUNC_ATANH, 1.5)));
    assert(impulse_math_unary_f32(MATH_FUNC_ATANH, 1.0f) == finf);
    assert(impulse_math_unary_f32(MATH_FUNC_ATANH, -1.0f) == fninf);
    assert(impulse_math_unary_f64(MATH_FUNC_ATANH, 1.0) == dinf);
    assert(impulse_math_unary_f64(MATH_FUNC_ATANH, -1.0) == dninf);
    assert(std::isnan(impulse_math_binary_f32(MATH_FUNC_POW, -2.0f, 0.5f)));
    assert(std::isnan(impulse_math_binary_f64(MATH_FUNC_POW, -2.0, 0.5)));
    assert(impulse_math_binary_f32(MATH_FUNC_POW, 1.0f, fnan) == 1.0f);
    assert(impulse_math_binary_f32(MATH_FUNC_POW, fnan, 0.0f) == 1.0f);
    assert(impulse_math_binary_f64(MATH_FUNC_POW, 1.0, dnan) == 1.0);
    assert(impulse_math_binary_f64(MATH_FUNC_POW, dnan, 0.0) == 1.0);
    assert(impulse_math_binary_f32(MATH_FUNC_HYPOT, finf, fnan) == finf);
    assert(impulse_math_binary_f64(MATH_FUNC_HYPOT, dninf, dnan) == dinf);
    assert(std::isnan(impulse_math_binary_f32(MATH_FUNC_FMOD, 5.0f, 0.0f)));
    assert(std::isnan(impulse_math_binary_f32(MATH_FUNC_FMOD, finf, 2.0f)));
    assert(std::isnan(impulse_math_binary_f64(MATH_FUNC_FMOD, 5.0, 0.0)));
    assert(std::isnan(impulse_math_binary_f64(MATH_FUNC_FMOD, dinf, 2.0)));
    assert(std::isnan(impulse_math_unary_f32(MATH_FUNC_SINC, fnan)));
    assert(std::isnan(impulse_math_unary_f64(MATH_FUNC_SINC, dnan)));
    assert(impulse_math_unary_f32(MATH_FUNC_SINC, fnz) == 1.0f);
    assert(impulse_math_unary_f64(MATH_FUNC_SINC, dnz) == 1.0);
    assert(impulse_math_unary_f32(MATH_FUNC_LGAMMA, 0.0f) == finf);
    assert(impulse_math_unary_f32(MATH_FUNC_LGAMMA, -1.0f) == finf);
    assert(impulse_math_unary_f32(MATH_FUNC_LGAMMA, -2.0f) == finf);
    assert(impulse_math_unary_f32(MATH_FUNC_LGAMMA, fninf) == finf);
    assert(impulse_math_unary_f32(MATH_FUNC_LGAMMA, finf) == finf);
    assert(std::isnan(impulse_math_unary_f32(MATH_FUNC_LGAMMA, fnan)));
    assert(impulse_math_unary_f64(MATH_FUNC_LGAMMA, 0.0) == dinf);
    assert(impulse_math_unary_f64(MATH_FUNC_LGAMMA, -3.0) == dinf);
    assert(impulse_math_unary_f64(MATH_FUNC_LGAMMA, dninf) == dinf);
    assert(impulse_math_unary_f64(MATH_FUNC_LGAMMA, dinf) == dinf);
    assert(std::isnan(impulse_math_unary_f64(MATH_FUNC_LGAMMA, dnan)));

    // 2. Signed zero (+0.0 vs -0.0 via std::signbit)
    {
        float a32 = impulse_math_unary_f32(MATH_FUNC_ABS, fnz);
        double a64 = impulse_math_unary_f64(MATH_FUNC_ABS, dnz);
        assert(a32 == 0.0f && !std::signbit(a32));
        assert(a64 == 0.0 && !std::signbit(a64));

        assert(impulse_math_binary_f32(MATH_FUNC_COPYSIGN, 1.0f, fnz) == -1.0f);
        assert(impulse_math_binary_f32(MATH_FUNC_COPYSIGN, -1.0f, fpz) == +1.0f);
        assert(std::isnan(impulse_math_binary_f32(MATH_FUNC_COPYSIGN, 5.0f, fnan)));
        assert(std::isnan(impulse_math_binary_f32(MATH_FUNC_COPYSIGN, fnan, 5.0f)));
        assert(impulse_math_binary_f64(MATH_FUNC_COPYSIGN, 1.0, dnz) == -1.0);
        assert(impulse_math_binary_f64(MATH_FUNC_COPYSIGN, -1.0, dpz) == +1.0);
        assert(std::isnan(impulse_math_binary_f64(MATH_FUNC_COPYSIGN, 5.0, dnan)));
        assert(std::isnan(impulse_math_binary_f64(MATH_FUNC_COPYSIGN, dnan, 5.0)));

        const double pi = 3.14159265358979323846;
        assert(std::fabs(impulse_math_binary_f32(MATH_FUNC_ATAN2, fnz, -1.0f) - static_cast<float>(-pi)) < 1e-5f);
        assert(std::fabs(impulse_math_binary_f32(MATH_FUNC_ATAN2, fpz, -1.0f) - static_cast<float>(+pi)) < 1e-5f);
        assert(std::fabs(impulse_math_binary_f64(MATH_FUNC_ATAN2, dnz, -1.0) - (-pi)) < 1e-12);
        assert(std::fabs(impulse_math_binary_f64(MATH_FUNC_ATAN2, dpz, -1.0) - (+pi)) < 1e-12);

        float r32 = impulse_math_unary_f32(MATH_FUNC_ROUND, -0.2f);
        double r64 = impulse_math_unary_f64(MATH_FUNC_ROUND, -0.2);
        assert(r32 == 0.0f && std::signbit(r32));
        assert(r64 == 0.0 && std::signbit(r64));
        assert(impulse_math_unary_f32(MATH_FUNC_ROUND, -2.5f) == -3.0f);
        assert(impulse_math_unary_f64(MATH_FUNC_ROUND, -2.5) == -3.0);

        float t32 = impulse_math_unary_f32(MATH_FUNC_TRUNC, -0.7f);
        double t64 = impulse_math_unary_f64(MATH_FUNC_TRUNC, -0.7);
        assert(t32 == 0.0f && std::signbit(t32));
        assert(t64 == 0.0 && std::signbit(t64));
    }

    // 3. Neural activations, clamp, safe_div & extreme ±Inf/NaN
    assert(std::isnan(impulse_math_unary_f32(MATH_FUNC_RELU, fnan)));
    assert(std::isnan(impulse_math_unary_f64(MATH_FUNC_RELU, dnan)));
    assert(std::isnan(impulse_math_unary_f32(MATH_FUNC_LEAKY_RELU, fnan)));
    assert(std::isnan(impulse_math_unary_f64(MATH_FUNC_LEAKY_RELU, dnan)));
    assert(std::isnan(impulse_math_binary_f32(MATH_FUNC_LEAKY_RELU, 2.0f, fnan)));
    assert(std::isnan(impulse_math_binary_f32(MATH_FUNC_LEAKY_RELU, fnan, 0.1f)));
    assert(std::isnan(impulse_math_binary_f64(MATH_FUNC_LEAKY_RELU, 2.0, dnan)));
    assert(std::isnan(impulse_math_binary_f64(MATH_FUNC_LEAKY_RELU, dnan, 0.1)));
    assert(impulse_math_unary_f32(MATH_FUNC_GELU, fninf) == 0.0f);
    assert(impulse_math_unary_f32(MATH_FUNC_GELU, finf) == finf);
    assert(impulse_math_unary_f32(MATH_FUNC_GELU, -20.0f) == 0.0f);
    assert(impulse_math_unary_f32(MATH_FUNC_GELU, 20.0f) == 20.0f);
    assert(std::isnan(impulse_math_unary_f32(MATH_FUNC_GELU, fnan)));
    assert(impulse_math_unary_f64(MATH_FUNC_GELU, dninf) == 0.0);
    assert(impulse_math_unary_f64(MATH_FUNC_GELU, dinf) == dinf);
    assert(impulse_math_unary_f64(MATH_FUNC_GELU, -20.0) == 0.0);
    assert(impulse_math_unary_f64(MATH_FUNC_GELU, 20.0) == 20.0);
    assert(std::isnan(impulse_math_unary_f64(MATH_FUNC_GELU, dnan)));
    assert(impulse_math_unary_f32(MATH_FUNC_SILU, fninf) == 0.0f);
    assert(impulse_math_unary_f32(MATH_FUNC_SILU, finf) == finf);
    assert(std::isnan(impulse_math_unary_f32(MATH_FUNC_SILU, fnan)));
    assert(impulse_math_unary_f64(MATH_FUNC_SILU, dninf) == 0.0);
    assert(impulse_math_unary_f64(MATH_FUNC_SILU, dinf) == dinf);
    assert(std::isnan(impulse_math_unary_f64(MATH_FUNC_SILU, dnan)));
    assert(impulse_math_unary_f32(MATH_FUNC_SOFTPLUS, 1000.0f) == 1000.0f);
    assert(impulse_math_unary_f32(MATH_FUNC_SOFTPLUS, fninf) == 0.0f);
    assert(impulse_math_unary_f32(MATH_FUNC_SOFTPLUS, finf) == finf);
    assert(std::isnan(impulse_math_unary_f32(MATH_FUNC_SOFTPLUS, fnan)));
    assert(impulse_math_unary_f64(MATH_FUNC_SOFTPLUS, 1000.0) == 1000.0);
    assert(impulse_math_unary_f64(MATH_FUNC_SOFTPLUS, dninf) == 0.0);
    assert(impulse_math_unary_f64(MATH_FUNC_SOFTPLUS, dinf) == dinf);
    assert(std::isnan(impulse_math_unary_f64(MATH_FUNC_SOFTPLUS, dnan)));

    assert(std::isnan(impulse_math_ternary_f32(MATH_FUNC_CLAMP, 5.0f, fnan, 10.0f)));
    assert(std::isnan(impulse_math_ternary_f32(MATH_FUNC_CLAMP, 5.0f, 0.0f, fnan)));
    assert(std::isnan(impulse_math_ternary_f32(MATH_FUNC_CLAMP, fnan, 0.0f, 10.0f)));
    assert(std::isnan(impulse_math_ternary_f64(MATH_FUNC_CLAMP, 5.0, dnan, 10.0)));
    assert(std::isnan(impulse_math_ternary_f64(MATH_FUNC_CLAMP, 5.0, 0.0, dnan)));
    assert(std::isnan(impulse_math_ternary_f64(MATH_FUNC_CLAMP, dnan, 0.0, 10.0)));

    assert(std::isnan(impulse_math_binary_f32(3, fnan, 2.0f)));
    assert(std::isnan(impulse_math_binary_f32(3, fnan, 0.0f)));
    assert(std::isnan(impulse_math_binary_f32(MATH_FUNC_SAFE_DIV, fnan, 0.0f)));
    assert(impulse_math_binary_f32(MATH_FUNC_SAFE_DIV, 10.0f, fnan) == 0.0f);
    assert(std::isnan(impulse_math_binary_f64(3, dnan, 0.0)));
    assert(std::isnan(impulse_math_binary_f64(MATH_FUNC_SAFE_DIV, dnan, 0.0)));
    assert(impulse_math_binary_f64(MATH_FUNC_SAFE_DIV, 10.0, dnan) == 0.0);
    assert(std::isnan(impulse_math_ternary_f32(MATH_FUNC_SAFE_DIV, fnan, 0.0f, 99.0f)));
    assert(impulse_math_ternary_f32(MATH_FUNC_SAFE_DIV, 10.0f, fnan, 99.0f) == 99.0f);
    assert(std::isnan(impulse_math_ternary_f64(MATH_FUNC_SAFE_DIV, dnan, 0.0, 99.0)));
    assert(impulse_math_ternary_f64(MATH_FUNC_SAFE_DIV, 10.0, dnan, 99.0) == 99.0);

    // 4. Bitwise UB guards (popcount, clz, ctz, rotl, rotr)
    assert(std::isnan(impulse_math_unary_f32(MATH_FUNC_POPCOUNT, fnan)));
    assert(std::isnan(impulse_math_unary_f32(MATH_FUNC_CLZ, fnan)));
    assert(std::isnan(impulse_math_unary_f32(MATH_FUNC_CTZ, fnan)));
    assert(std::isnan(impulse_math_unary_f64(MATH_FUNC_POPCOUNT, dnan)));
    assert(std::isnan(impulse_math_unary_f64(MATH_FUNC_CLZ, dnan)));
    assert(std::isnan(impulse_math_unary_f64(MATH_FUNC_CTZ, dnan)));

    // Negative / -0.0 / -Inf clamp to 0u -> popcount=0, clz=32/64, ctz=32/64
    for (float neg : { -5.0f, fnz, fninf }) {
        assert(impulse_math_unary_f32(MATH_FUNC_POPCOUNT, neg) == 0.0f);
        assert(impulse_math_unary_f32(MATH_FUNC_CLZ, neg) == 32.0f);
        assert(impulse_math_unary_f32(MATH_FUNC_CTZ, neg) == 32.0f);
    }
    for (double neg : { -5.0, dnz, dninf }) {
        assert(impulse_math_unary_f64(MATH_FUNC_POPCOUNT, neg) == 0.0);
        assert(impulse_math_unary_f64(MATH_FUNC_CLZ, neg) == 64.0);
        assert(impulse_math_unary_f64(MATH_FUNC_CTZ, neg) == 64.0);
    }
    // Huge / +Inf clamp to UINT32_MAX / UINT64_MAX -> popcount=32/64, clz=0, ctz=0
    for (float big : { 1e20f, finf }) {
        assert(impulse_math_unary_f32(MATH_FUNC_POPCOUNT, big) == 32.0f);
        assert(impulse_math_unary_f32(MATH_FUNC_CLZ, big) == 0.0f);
        assert(impulse_math_unary_f32(MATH_FUNC_CTZ, big) == 0.0f);
    }
    for (double big : { 1e20, dinf }) {
        assert(impulse_math_unary_f64(MATH_FUNC_POPCOUNT, big) == 64.0);
        assert(impulse_math_unary_f64(MATH_FUNC_CLZ, big) == 0.0);
        assert(impulse_math_unary_f64(MATH_FUNC_CTZ, big) == 0.0);
    }
    // rotl / rotr UB guards
    assert(std::isnan(impulse_math_binary_f32(MATH_FUNC_ROTL, fnan, 3.0f)));
    assert(std::isnan(impulse_math_binary_f32(MATH_FUNC_ROTL, 1.0f, fnan)));
    assert(std::isnan(impulse_math_binary_f32(MATH_FUNC_ROTR, fnan, 3.0f)));
    assert(std::isnan(impulse_math_binary_f32(MATH_FUNC_ROTR, 1.0f, fnan)));
    assert(std::isnan(impulse_math_binary_f64(MATH_FUNC_ROTL, dnan, 3.0)));
    assert(std::isnan(impulse_math_binary_f64(MATH_FUNC_ROTL, 1.0, dnan)));
    assert(std::isnan(impulse_math_binary_f64(MATH_FUNC_ROTR, dnan, 3.0)));
    assert(std::isnan(impulse_math_binary_f64(MATH_FUNC_ROTR, 1.0, dnan)));
    assert(impulse_math_binary_f32(MATH_FUNC_ROTL, -5.0f, 3.0f) == 0.0f);
    assert(impulse_math_binary_f32(MATH_FUNC_ROTL, 1.0f, finf) == 1.0f);
    assert(impulse_math_binary_f32(MATH_FUNC_ROTL, 1.0f, fninf) == 1.0f);
    assert(impulse_math_binary_f32(MATH_FUNC_ROTL, 1.0f, 1e20f) == 1.0f);
    assert(impulse_math_binary_f32(MATH_FUNC_ROTL, 1.0f, 65.0f) == 2.0f); // 65 & 31 == 1
    assert(impulse_math_binary_f32(MATH_FUNC_ROTR, 1.0f, -3.0f) == 8.0f); // (-3 & 31 == 29 -> rotr 29 == rotl 3 == 8)
    assert(impulse_math_binary_f64(MATH_FUNC_ROTL, -5.0, 3.0) == 0.0);
    assert(impulse_math_binary_f64(MATH_FUNC_ROTL, 1.0, dinf) == 1.0);
    assert(impulse_math_binary_f64(MATH_FUNC_ROTL, 1.0, 65.0) == 2.0); // 65 & 63 == 1
    assert(impulse_math_binary_f64(MATH_FUNC_ROTR, 1.0, -3.0) == 8.0); // (-3 & 63 == 61 -> rotr 61 == rotl 3 == 8)
    assert(impulse_math_binary_f32(MATH_FUNC_ROTL, 1e20f, 5.0f) == static_cast<float>(0xFFFFFFFFu));

    // 5. Full 54-ID Sweep across SIMD (<=2048), OpenMP (>2048), OP_VEC_MATH_*, and OP_STREAM_MATH_UNARY (0xAF)
    const std::vector<double> domain_samples = {
        -20.0, -2.5, -1.5, -1.0, -0.5, -0.0, +0.0, 0.5, 1.0, 1.5, 2.5, 25.0, 1000.0, 1e20,
        dnan, dinf, dninf
    };
    const size_t simd_n = 32;
    const size_t omp_n = 4096;
    std::vector<float> f32_in(omp_n), f32_in2(omp_n), f32_in3(omp_n), f32_out_simd(simd_n), f32_out_omp(omp_n);
    std::vector<double> f64_in(omp_n), f64_in2(omp_n), f64_in3(omp_n), f64_out_simd(simd_n), f64_out_omp(omp_n);
    for (size_t i = 0; i < omp_n; ++i) {
        double a = domain_samples[i % domain_samples.size()];
        double b = domain_samples[(i * 3 + 1) % domain_samples.size()];
        double c = domain_samples[(i * 7 + 2) % domain_samples.size()];
        f64_in[i] = a; f64_in2[i] = b; f64_in3[i] = c;
        f32_in[i] = static_cast<float>(a);
        f32_in2[i] = static_cast<float>(b);
        f32_in3[i] = static_cast<float>(c);
    }

    const std::vector<uint8_t> unary_ids = {
        MATH_FUNC_ABS, MATH_FUNC_SQRT, MATH_FUNC_RSQRT, MATH_FUNC_CBRT,
        MATH_FUNC_EXP, MATH_FUNC_EXP2, MATH_FUNC_EXP10, MATH_FUNC_EXPM1,
        MATH_FUNC_LOG, MATH_FUNC_LOG2, MATH_FUNC_LOG10, MATH_FUNC_LOG1P,
        MATH_FUNC_SIN, MATH_FUNC_COS, MATH_FUNC_TAN, MATH_FUNC_ASIN,
        MATH_FUNC_ACOS, MATH_FUNC_ATAN, MATH_FUNC_SINC,
        MATH_FUNC_SINH, MATH_FUNC_COSH, MATH_FUNC_TANH, MATH_FUNC_ASINH,
        MATH_FUNC_ACOSH, MATH_FUNC_ATANH,
        MATH_FUNC_FLOOR, MATH_FUNC_CEIL, MATH_FUNC_TRUNC, MATH_FUNC_ROUND,
        MATH_FUNC_RELU, MATH_FUNC_LEAKY_RELU, MATH_FUNC_SIGMOID, MATH_FUNC_GELU,
        MATH_FUNC_SILU, MATH_FUNC_SOFTPLUS,
        MATH_FUNC_ERF, MATH_FUNC_ERFC, MATH_FUNC_LGAMMA,
        MATH_FUNC_POPCOUNT, MATH_FUNC_CLZ, MATH_FUNC_CTZ,
        MATH_FUNC_ISNAN, MATH_FUNC_ISINF, MATH_FUNC_ISFINITE
    };
    const std::vector<uint8_t> binary_ids = {
        0, 1, 2, 3, 4,
        MATH_FUNC_POW, MATH_FUNC_HYPOT, MATH_FUNC_ATAN2, MATH_FUNC_COPYSIGN,
        MATH_FUNC_FMOD, MATH_FUNC_LEAKY_RELU, MATH_FUNC_ROTL, MATH_FUNC_ROTR,
        MATH_FUNC_SAFE_DIV
    };
    const std::vector<uint8_t> ternary_ids = {
        0, 1, MATH_FUNC_LERP, MATH_FUNC_CLAMP, MATH_FUNC_SAFE_DIV
    };

    // Build 17-edge star graph (node 0 -> targets 0..16) for OP_STREAM_MATH_UNARY (0xAF) parity check
    const size_t K = domain_samples.size();
    std::vector<uint32_t> star_offsets(K + 1, static_cast<uint32_t>(K));
    star_offsets[0] = 0;
    std::vector<uint32_t> star_targets(K);
    std::vector<float> edge_vals(K);
    for (size_t i = 0; i < K; ++i) {
        star_targets[i] = static_cast<uint32_t>(i);
        edge_vals[i] = static_cast<float>(domain_samples[i]);
    }

    for (uint8_t fid : unary_ids) {
        impulse_vector_math_unary_f32(fid, f32_out_simd.data(), f32_in.data(), simd_n);
        impulse_vector_math_unary_f32(fid, f32_out_omp.data(), f32_in.data(), omp_n);
        impulse_vector_math_unary_f64(fid, f64_out_simd.data(), f64_in.data(), simd_n);
        impulse_vector_math_unary_f64(fid, f64_out_omp.data(), f64_in.data(), omp_n);
        for (size_t i = 0; i < simd_n; ++i) {
            assert(float_eq_or_same_special(f32_out_simd[i], impulse_math_unary_f32(fid, f32_in[i]), 1e-6f));
            assert(double_eq_or_same_special(f64_out_simd[i], impulse_math_unary_f64(fid, f64_in[i]), 1e-12));
        }
        for (size_t i = 0; i < omp_n; ++i) {
            assert(float_eq_or_same_special(f32_out_omp[i], impulse_math_unary_f32(fid, f32_in[i]), 1e-6f));
            assert(double_eq_or_same_special(f64_out_omp[i], impulse_math_unary_f64(fid, f64_in[i]), 1e-12));
        }

        // Verify OP_VEC_MATH_UNARY (0x2D) vs OP_STREAM_MATH_UNARY (0xAF) across all 17 edge values!
        impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr);
        impulse_vm_context_mock_csr(ctx, 0, star_offsets.data(), star_targets.data(), K, K);
        impulse_vm_context_bind_inline_data(ctx, edge_vals.data(), K * sizeof(float));

        // Program computes:
        // R1 = inline edge_vals
        // R2 = OP_VEC_MATH_UNARY(fid, R1)
        // Edge stream on R0={0}:
        //   S0 = edge_attr[eIdx]
        //   S1 = OP_STREAM_MATH_UNARY(fid, S0)
        //   S2 = OP_STREAM_MATH_UNARY(ISNAN, S1)
        //   OP_STREAM_REDUCE(S1 -> R3, SUM)   (skips NaN edges!)
        //   OP_STREAM_REDUCE(S2 -> R4, SUM)   (1.0f iff S1 was NaN!)
        std::vector<impulse_instruction_t> prog = {
            { OP_INIT_INPUT_NODE, 0, 10, 0 }, // R10 = {0} (leave R0=NULL so mock_attr mask_reg=0 has no mask!)
            { OP_LOAD_INLINE_ARRAY, 0, 1, static_cast<uint32_t>(0 | (K << 16)) },
            { OP_INIT_MOCK_EDGE_ATTR, 0, 0, 1 }, // edge attr 0 = R1 (mask_reg=0 -> R0 is TYPE_NULL)
            { OP_VEC_MATH_UNARY, 0, 2, static_cast<uint32_t>(1 | (static_cast<uint32_t>(fid) << 8)) },
            { OP_CSR_WALK_STREAM, 5, 11, 10 | (0 << 16) }, // shader starts at PC=5
            { OP_STREAM_FUNC_BEGIN, 0, 0, 0 },
            { OP_STREAM_LOAD_EDGE, 0, 0, 0 },
            { OP_STREAM_MATH_UNARY, 0, 1, static_cast<uint32_t>(0 | (static_cast<uint32_t>(fid) << 16)) },
            { OP_STREAM_MATH_UNARY, 0, 2, static_cast<uint32_t>(1 | (static_cast<uint32_t>(MATH_FUNC_ISNAN) << 16)) },
            { OP_STREAM_REDUCE, 0, 1, 3 | (0 << 16) },
            { OP_STREAM_REDUCE, 0, 2, 4 | (0 << 16) },
            { OP_STREAM_FUNC_END, 0, 0, 0 },
            { OP_HALT, 0, 0, 0 }
        };
        impulse_vm_state_t st{};
        st.query_context = ctx;
        assert(impulse_vm_execute(prog.data(), prog.size(), &st, 0) == IMPULSE_VM_OK);
        const float* v_2d = impulse_vm_context_get_float_vector(ctx, st.registers[2]);
        const float* v_af_val = impulse_vm_context_get_float_vector(ctx, st.registers[3]);
        const float* v_af_nan = impulse_vm_context_get_float_vector(ctx, st.registers[4]);
        for (size_t i = 0; i < K; ++i) {
            float expected = impulse_math_unary_f32(fid, edge_vals[i]);
            assert(float_eq_or_same_special(v_2d[i], expected, 1e-6f));
            if (std::isnan(expected)) {
                assert(v_af_nan[i] == 1.0f);
                assert(v_af_val[i] == 0.0f); // NaN edge skipped by OP_STREAM_REDUCE!
            } else {
                assert(v_af_nan[i] == 0.0f);
                assert(float_eq_or_same_special(v_af_val[i], v_2d[i], 1e-6f));
            }
        }
        impulse_vm_context_destroy(ctx);
    }

    for (uint8_t fid : binary_ids) {
        impulse_vector_math_binary_f32(fid, f32_out_simd.data(), f32_in.data(), f32_in2.data(), simd_n);
        impulse_vector_math_binary_f32(fid, f32_out_omp.data(), f32_in.data(), f32_in2.data(), omp_n);
        impulse_vector_math_binary_f64(fid, f64_out_simd.data(), f64_in.data(), f64_in2.data(), simd_n);
        impulse_vector_math_binary_f64(fid, f64_out_omp.data(), f64_in.data(), f64_in2.data(), omp_n);
        for (size_t i = 0; i < omp_n; ++i) {
            assert(float_eq_or_same_special(f32_out_omp[i], impulse_math_binary_f32(fid, f32_in[i], f32_in2[i]), 1e-6f));
            assert(double_eq_or_same_special(f64_out_omp[i], impulse_math_binary_f64(fid, f64_in[i], f64_in2[i]), 1e-12));
        }

        // Also execute via OP_VEC_MATH_BINARY (0x2E) for both f32 and f64 vectors
        impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr);
        int hf1 = impulse_vm_context_acquire_float_vector(ctx);
        int hf2 = impulse_vm_context_acquire_float_vector(ctx);
        int hd1 = impulse_vm_context_acquire_double_vector(ctx);
        int hd2 = impulse_vm_context_acquire_double_vector(ctx);
        for (size_t i = 0; i < K; ++i) {
            impulse_vm_context_float_vector_set(ctx, hf1, i, f32_in[i]);
            impulse_vm_context_float_vector_set(ctx, hf2, i, f32_in2[i]);
            impulse_vm_context_double_vector_set(ctx, hd1, i, f64_in[i]);
            impulse_vm_context_double_vector_set(ctx, hd2, i, f64_in2[i]);
        }
        impulse_vm_state_t st{};
        st.query_context = ctx;
        st.registers[1] = hf1; st.register_types[1] = TYPE_FLOAT_VECTOR;
        st.registers[2] = hf2; st.register_types[2] = TYPE_FLOAT_VECTOR;
        st.registers[3] = hd1; st.register_types[3] = TYPE_DOUBLE_VECTOR;
        st.registers[4] = hd2; st.register_types[4] = TYPE_DOUBLE_VECTOR;
        std::vector<impulse_instruction_t> prog = {
            { OP_VEC_MATH_BINARY, 0, 5, static_cast<uint32_t>(1 | (2 << 8) | (static_cast<uint32_t>(fid) << 16)) },
            { OP_VEC_MATH_BINARY, 1, 6, static_cast<uint32_t>(3 | (4 << 8) | (static_cast<uint32_t>(fid) << 16)) },
            { OP_HALT, 0, 0, 0 }
        };
        assert(impulse_vm_execute(prog.data(), prog.size(), &st, 0) == IMPULSE_VM_OK);
        const float* out_f = impulse_vm_context_get_float_vector(ctx, st.registers[5]);
        const double* out_d = impulse_vm_context_get_double_vector(ctx, st.registers[6]);
        for (size_t i = 0; i < K; ++i) {
            assert(float_eq_or_same_special(out_f[i], f32_out_omp[i], 1e-6f));
            assert(double_eq_or_same_special(out_d[i], f64_out_omp[i], 1e-12));
        }
        impulse_vm_context_destroy(ctx);
    }

    for (uint8_t fid : ternary_ids) {
        impulse_vector_math_ternary_f32(fid, f32_out_simd.data(), f32_in.data(), f32_in2.data(), f32_in3.data(), simd_n);
        impulse_vector_math_ternary_f32(fid, f32_out_omp.data(), f32_in.data(), f32_in2.data(), f32_in3.data(), omp_n);
        impulse_vector_math_ternary_f64(fid, f64_out_simd.data(), f64_in.data(), f64_in2.data(), f64_in3.data(), simd_n);
        impulse_vector_math_ternary_f64(fid, f64_out_omp.data(), f64_in.data(), f64_in2.data(), f64_in3.data(), omp_n);
        for (size_t i = 0; i < omp_n; ++i) {
            assert(float_eq_or_same_special(f32_out_omp[i], impulse_math_ternary_f32(fid, f32_in[i], f32_in2[i], f32_in3[i]), 1e-6f));
            assert(double_eq_or_same_special(f64_out_omp[i], impulse_math_ternary_f64(fid, f64_in[i], f64_in2[i], f64_in3[i]), 1e-12));
        }

        impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr);
        int hf1 = impulse_vm_context_acquire_float_vector(ctx);
        int hf2 = impulse_vm_context_acquire_float_vector(ctx);
        int hf3 = impulse_vm_context_acquire_float_vector(ctx);
        int hd1 = impulse_vm_context_acquire_double_vector(ctx);
        int hd2 = impulse_vm_context_acquire_double_vector(ctx);
        int hd3 = impulse_vm_context_acquire_double_vector(ctx);
        for (size_t i = 0; i < K; ++i) {
            impulse_vm_context_float_vector_set(ctx, hf1, i, f32_in[i]);
            impulse_vm_context_float_vector_set(ctx, hf2, i, f32_in2[i]);
            impulse_vm_context_float_vector_set(ctx, hf3, i, f32_in3[i]);
            impulse_vm_context_double_vector_set(ctx, hd1, i, f64_in[i]);
            impulse_vm_context_double_vector_set(ctx, hd2, i, f64_in2[i]);
            impulse_vm_context_double_vector_set(ctx, hd3, i, f64_in3[i]);
        }
        impulse_vm_state_t st{};
        st.query_context = ctx;
        st.registers[1] = hf1; st.register_types[1] = TYPE_FLOAT_VECTOR;
        st.registers[2] = hf2; st.register_types[2] = TYPE_FLOAT_VECTOR;
        st.registers[3] = hf3; st.register_types[3] = TYPE_FLOAT_VECTOR;
        st.registers[4] = hd1; st.register_types[4] = TYPE_DOUBLE_VECTOR;
        st.registers[5] = hd2; st.register_types[5] = TYPE_DOUBLE_VECTOR;
        st.registers[6] = hd3; st.register_types[6] = TYPE_DOUBLE_VECTOR;
        std::vector<impulse_instruction_t> prog = {
            { OP_VEC_MATH_TERNARY, 0, 7, static_cast<uint32_t>(1 | (2 << 8) | (3 << 16) | (static_cast<uint32_t>(fid) << 24)) },
            { OP_VEC_MATH_TERNARY, 1, 8, static_cast<uint32_t>(4 | (5 << 8) | (6 << 16) | (static_cast<uint32_t>(fid) << 24)) },
            { OP_HALT, 0, 0, 0 }
        };
        assert(impulse_vm_execute(prog.data(), prog.size(), &st, 0) == IMPULSE_VM_OK);
        const float* out_f = impulse_vm_context_get_float_vector(ctx, st.registers[7]);
        const double* out_d = impulse_vm_context_get_double_vector(ctx, st.registers[8]);
        for (size_t i = 0; i < K; ++i) {
            assert(float_eq_or_same_special(out_f[i], f32_out_omp[i], 1e-6f));
            assert(double_eq_or_same_special(out_d[i], f64_out_omp[i], 1e-12));
        }
        impulse_vm_context_destroy(ctx);
    }

    std::cout << "  -> PASSED" << std::endl;
}

static void test_nullable_vectors_and_monoidic_reducers() {
    std::cout << "[Test] Nullable Columns (0x80->0x2D->0x3C->0x3B), Reducers (0x35, 0x45, 0xC8), & Stream NaN Reducers (0xA8, 0xB8, 0xB9, 0xBF)..." << std::endl;

    const float fnan = std::numeric_limits<float>::quiet_NaN();

    // -----------------------------------------------------------------------
    // Part A: Official SnapshotWriter NullableFLOAT32 / NullableFLOAT64 / NullableINT32
    //         OP_LOAD_COLUMN_VECTOR (0x80) -> OP_VEC_MATH_UNARY (0x2D)
    //         -> OP_EXTRACT_VALIDITY (0x3C) -> OP_COALESCE (0x3B) + OP_LANE_REDUCE_ATTR (0xC8)
    // -----------------------------------------------------------------------
    char snap_path[256];
    std::snprintf(snap_path, sizeof(snap_path), "/tmp/impulse_test_nan_null_%d.imps", static_cast<int>(IMP_GETPID()));
    {
        // 6 nodes: valid mask bits = 0b101101 = 0x2D (nodes 0, 2, 3, 5 valid; nodes 1, 4 NULL!)
        // Values at [0..5]: [-9.0, 999.0(NULL), 4.0, 16.0, 888.0(NULL), -4.0]
        // Note: after sqrt(x) on valid rows:
        //   node 0: sqrt(-9.0) -> NaN!
        //   node 1: NULL -> NaN!
        //   node 2: sqrt(4.0)  -> 2.0
        //   node 3: sqrt(16.0) -> 4.0
        //   node 4: NULL -> NaN!
        //   node 5: sqrt(-4.0) -> NaN!
        // So OP_EXTRACT_VALIDITY after sqrt has ONLY bits {2, 3} set!
        impulse_writer_t* w = impulse_writer_create(snap_path, 0);
        assert(w != nullptr);
        assert(impulse_writer_add_domain(w, 0, IMPULSE_KEY_TYPE_UINT32, "Node") == IMPULSE_OK);
        assert(impulse_writer_set_domain_node_count(w, 0, 6) == IMPULSE_OK);

        // CSR: seed 0 -> {0, 1, 2, 3}, seed 1 -> {1, 4} (all NULL!), seed 2 -> {} (untouched!)
        uint32_t row_off[7] = { 0, 4, 6, 6, 6, 6, 6 };
        uint32_t col_idx[6] = { 0, 1, 2, 3, 1, 4 };
        assert(impulse_writer_add_relation(w, 0, 0, IMPULSE_ENC_RAW, 6, 6, 0,
                                           row_off, sizeof(row_off), col_idx, sizeof(col_idx)) == IMPULSE_OK);

        // Build 128-byte padded validity bitmap + values for f32, f64, i32
        std::vector<uint8_t> f32_col(128 + 6 * sizeof(float), 0);
        f32_col[0] = 0x2Du; // bits 0, 2, 3, 5 valid
        float f32_vals[6] = { -9.0f, 999.0f, 4.0f, 16.0f, 888.0f, -4.0f };
        std::memcpy(f32_col.data() + 128, f32_vals, sizeof(f32_vals));
        assert(impulse_writer_add_domain_attribute(w, 0, "val_f32", 0x80u | 0x06u, 1,
                                                   f32_col.data(), f32_col.size(), nullptr, 0) == IMPULSE_OK);

        std::vector<uint8_t> f64_col(128 + 6 * sizeof(double), 0);
        f64_col[0] = 0x2Du;
        double f64_vals[6] = { -9.0, 999.0, 4.0, 16.0, 888.0, -4.0 };
        std::memcpy(f64_col.data() + 128, f64_vals, sizeof(f64_vals));
        assert(impulse_writer_add_domain_attribute(w, 0, "val_f64", 0x80u | 0x07u, 1,
                                                   f64_col.data(), f64_col.size(), nullptr, 0) == IMPULSE_OK);

        std::vector<uint8_t> i32_col(128 + 6 * sizeof(int32_t), 0);
        i32_col[0] = 0x2Du;
        int32_t i32_vals[6] = { -10, 777, -3, -5, 666, -20 };
        std::memcpy(i32_col.data() + 128, i32_vals, sizeof(i32_vals));
        assert(impulse_writer_add_domain_attribute(w, 0, "val_i32", 0x80u | 0x03u, 1,
                                                   i32_col.data(), i32_col.size(), nullptr, 0) == IMPULSE_OK);

        assert(impulse_writer_finalize(w) == IMPULSE_OK);
        impulse_writer_destroy(w);
    }

    impulse_status_t open_st = IMPULSE_OK;
    impulse_snapshot_t* snap = impulse_snapshot_open(snap_path, &open_st);
    assert(snap != nullptr && open_st == IMPULSE_OK);

    {
        impulse_vm_context_t* ctx = impulse_vm_context_create(snap);
        impulse_vm_state_t st{};
        st.query_context = ctx;

        // Scalar fallback registers: R20 = -1.0f (TYPE_FLOAT), R21 = -2.0 (TYPE_DOUBLE)
        float fb_f32 = -1.0f;
        uint32_t fb_f32_bits = 0;
        std::memcpy(&fb_f32_bits, &fb_f32, sizeof(float));
        st.registers[20] = fb_f32_bits;
        st.register_types[20] = TYPE_FLOAT;

        double fb_f64 = -2.0;
        std::memcpy(&st.registers[21], &fb_f64, sizeof(double));
        st.register_types[21] = TYPE_DOUBLE;

        std::vector<impulse_instruction_t> prog = {
            // f32 nullable pipeline: R1 = col0, R2 = sqrt(R1), R3 = validity(R2), R4 = coalesce(R2, -1.0f)
            { OP_LOAD_COLUMN_VECTOR, 0, 1, 0 | (0 << 16) },
            { OP_VEC_MATH_UNARY, 0, 2, 1 | (MATH_FUNC_SQRT << 8) },
            { OP_EXTRACT_VALIDITY, 0, 3, 2 },
            { OP_COALESCE, 0, 4, 2 | (20 << 16) },
            { OP_VECTOR_REDUCE_SUM, 0, 5, 2 }, // sum of [NaN, NaN, 2.0, 4.0, NaN, NaN] = 6.0f

            // f64 nullable pipeline: R6 = col1, R7 = sqrt(R6), R8 = validity(R7), R9 = coalesce(R7, -2.0)
            { OP_LOAD_COLUMN_VECTOR, 0, 6, 0 | (1 << 16) },
            { OP_VEC_MATH_UNARY, 1, 7, 6 | (MATH_FUNC_SQRT << 8) },
            { OP_EXTRACT_VALIDITY, 0, 8, 7 },
            { OP_COALESCE, 0, 9, 7 | (21 << 16) },
            { OP_VECTOR_REDUCE_SUM, 0, 10, 7 }, // sum of [NaN, NaN, 2.0, 4.0, NaN, NaN] = 6.0

            // Multi-lane reduce over nullable snapshot columns (3 seed lanes 0, 1, 2 -> CSR walk):
            // Lane 0 reaches {0, 1(NULL), 2, 3}; Lane 1 reaches {1(NULL), 4(NULL)}; Lane 2 reaches {}
            { OP_LANE_INIT_SEEDS, 1, 11, (3 << 16) },
            { OP_LANE_CSR_WALK, 0, 12, 11 | (0 << 16) },
            { OP_LANE_REDUCE_ATTR, 0, 13, 12 | (2 << 8) | (4 << 16) }, // COUNT on nullable int32 attr 2
            { OP_LANE_REDUCE_ATTR, 0, 14, 12 | (2 << 8) | (BINARY_OP_MAX << 16) }, // MAX on [-10, NULL, -3, -5]
            { OP_LANE_REDUCE_ATTR, 0, 15, 12 | (2 << 8) | (BINARY_OP_ARGMAX << 16) }, // ARGMAX -> node 2
            { OP_LANE_REDUCE_ATTR, 0, 16, 12 | (1 << 8) | (BINARY_OP_ADD << 16) }, // SUM on nullable f64 attr 1: -9 + 4 + 16 = 11
            { OP_HALT, 0, 0, 0 }
        };
        assert(impulse_vm_execute(prog.data(), prog.size(), &st, 0) == IMPULSE_VM_OK);

        // Check validity bitsets R3 and R8: only indices 2 and 3 valid!
        for (uint64_t u = 0; u < 6; ++u) {
            bool exp_valid = (u == 2 || u == 3);
            assert(impulse_vm_context_bitset_test(ctx, st.registers[3], u) == exp_valid);
            assert(impulse_vm_context_bitset_test(ctx, st.registers[8], u) == exp_valid);
        }

        // Check coalesced f32 (R4) and f64 (R9)
        assert(st.register_types[4] == TYPE_FLOAT_VECTOR);
        assert(st.register_types[9] == TYPE_DOUBLE_VECTOR);
        const float* c_f32 = impulse_vm_context_get_float_vector(ctx, st.registers[4]);
        const double* c_f64 = impulse_vm_context_get_double_vector(ctx, st.registers[9]);
        float exp_cf32[6] = { -1.0f, -1.0f, 2.0f, 4.0f, -1.0f, -1.0f };
        double exp_cf64[6] = { -2.0, -2.0, 2.0, 4.0, -2.0, -2.0 };
        for (size_t i = 0; i < 6; ++i) {
            assert(std::fabs(c_f32[i] - exp_cf32[i]) < 1e-6f);
            assert(std::fabs(c_f64[i] - exp_cf64[i]) < 1e-12);
        }

        // Check OP_VECTOR_REDUCE_SUM on R5 (f32) and R10 (f64)
        float sum32 = 0.0f;
        uint32_t sum32_bits = static_cast<uint32_t>(st.registers[5]);
        std::memcpy(&sum32, &sum32_bits, sizeof(float));
        double sum64 = 0.0;
        std::memcpy(&sum64, &st.registers[10], sizeof(double));
        assert(std::fabs(sum32 - 6.0f) < 1e-6f);
        assert(std::fabs(sum64 - 6.0) < 1e-12);

        // Check OP_LANE_REDUCE_ATTR over nullable snapshot columns!
        // Lane 0 has 3 non-null nodes (0, 2, 3); Lane 1 has 0 non-null nodes (both 1 & 4 are NULL!); Lane 2 has 0!
        const float* cnt_vec = impulse_vm_context_get_float_vector(ctx, st.registers[13]);
        assert(cnt_vec[0] == 3.0f && cnt_vec[1] == 0.0f && cnt_vec[2] == 0.0f);

        const float* max_vec = impulse_vm_context_get_float_vector(ctx, st.registers[14]);
        assert(max_vec[0] == -3.0f && max_vec[1] == 0.0f && max_vec[2] == 0.0f);

        const uint64_t* argmax_vec = impulse_vm_context_get_node_vector(ctx, st.registers[15]);
        assert(argmax_vec[0] == 2ULL && argmax_vec[1] == ~0ULL && argmax_vec[2] == ~0ULL);

        const float* sum_f64_lane = impulse_vm_context_get_float_vector(ctx, st.registers[16]);
        assert(std::fabs(sum_f64_lane[0] - 11.0f) < 1e-5f && sum_f64_lane[1] == 0.0f && sum_f64_lane[2] == 0.0f);

        // Also test vector-vector OP_COALESCE for TYPE_DOUBLE_VECTOR
        int hd_fb = impulse_vm_context_acquire_double_vector(ctx);
        for (size_t i = 0; i < 6; ++i) impulse_vm_context_double_vector_set(ctx, hd_fb, i, 100.0 + static_cast<double>(i));
        st.registers[22] = hd_fb;
        st.register_types[22] = TYPE_DOUBLE_VECTOR;
        std::vector<impulse_instruction_t> coalesce_vv = {
            { OP_COALESCE, 0, 23, 7 | (22 << 16) },
            { OP_HALT, 0, 0, 0 }
        };
        st.pc = 0;
        assert(impulse_vm_execute(coalesce_vv.data(), coalesce_vv.size(), &st, 0) == IMPULSE_VM_OK);
        const double* c_vv = impulse_vm_context_get_double_vector(ctx, st.registers[23]);
        assert(c_vv[0] == 100.0 && c_vv[1] == 101.0 && c_vv[2] == 2.0 && c_vv[3] == 4.0 && c_vv[4] == 104.0 && c_vv[5] == 105.0);

        impulse_vm_context_destroy(ctx);
    }

    impulse_snapshot_close(snap);
    std::remove(snap_path);

    // -----------------------------------------------------------------------
    // Part B: OP_REDUCE (0x45: ADD, MUL, MIN, MAX, ARGMIN, ARGMAX) on mixed negative+NaN & all-NaN
    // -----------------------------------------------------------------------
    {
        impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr);
        float mixed_f32[4] = { -10.0f, fnan, -2.0f, -5.0f };
        float all_nan_f32[4] = { fnan, fnan, fnan, fnan };
        float inline_buf[8];
        std::memcpy(inline_buf, mixed_f32, sizeof(mixed_f32));
        std::memcpy(inline_buf + 4, all_nan_f32, sizeof(all_nan_f32));
        impulse_vm_context_bind_inline_data(ctx, inline_buf, sizeof(inline_buf));

        std::vector<impulse_instruction_t> prog = {
            { OP_LOAD_INLINE_ARRAY, 0, 1, 0 | (4 << 16) },  // R1 = [-10, NaN, -2, -5]
            { OP_LOAD_INLINE_ARRAY, 0, 2, 16 | (4 << 16) }, // R2 = [NaN, NaN, NaN, NaN]
            { OP_REDUCE, 0, 10, 1 | (BINARY_OP_ADD << 16) },
            { OP_REDUCE, 0, 11, 1 | (BINARY_OP_MUL << 16) },
            { OP_REDUCE, 0, 12, 1 | (BINARY_OP_MIN << 16) },
            { OP_REDUCE, 0, 13, 1 | (BINARY_OP_MAX << 16) },
            { OP_REDUCE, 0, 14, 1 | (BINARY_OP_ARGMIN << 16) },
            { OP_REDUCE, 0, 15, 1 | (BINARY_OP_ARGMAX << 16) },
            { OP_REDUCE, 0, 20, 2 | (BINARY_OP_ADD << 16) },
            { OP_REDUCE, 0, 21, 2 | (BINARY_OP_MUL << 16) },
            { OP_REDUCE, 0, 22, 2 | (BINARY_OP_MIN << 16) },
            { OP_REDUCE, 0, 23, 2 | (BINARY_OP_MAX << 16) },
            { OP_REDUCE, 0, 24, 2 | (BINARY_OP_ARGMAX << 16) },
            { OP_HALT, 0, 0, 0 }
        };
        impulse_vm_state_t st{};
        st.query_context = ctx;
        assert(impulse_vm_execute(prog.data(), prog.size(), &st, 0) == IMPULSE_VM_OK);

        auto reg_f32 = [&](int r) {
            float v = 0.0f;
            uint32_t b = static_cast<uint32_t>(st.registers[r]);
            std::memcpy(&v, &b, sizeof(float));
            return v;
        };
        assert(reg_f32(10) == -17.0f);
        assert(reg_f32(11) == -100.0f);
        assert(reg_f32(12) == -10.0f);
        assert(reg_f32(13) == -2.0f);
        assert(st.registers[14] == 0ULL);
        assert(st.registers[15] == 2ULL);

        assert(reg_f32(20) == 0.0f);
        assert(reg_f32(21) == 0.0f);
        assert( std::isinf(reg_f32(22)) && !std::signbit(reg_f32(22)));
        assert( std::isinf(reg_f32(23)) &&  std::signbit(reg_f32(23)));
        assert(st.registers[24] == 0ULL);
        assert((st.flags & 0x1ULL) != 0ULL); // ZF set on all-NaN

        impulse_vm_context_destroy(ctx);
    }

    // -----------------------------------------------------------------------
    // Part C: OP_LANE_REDUCE_ATTR (0xC8) with Mock Mask + NaN + Untouched Lanes + Negative Values + ZF
    // -----------------------------------------------------------------------
    {
        impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr);
        // 5 nodes:
        // Seed lane 0 -> {0, 1, 2, 3}; Seed lane 1 -> {1, 4}; Seed lane 2 -> {} (untouched)
        // Mock attr values: [-8.0f, -99.0f(masked out!), fnan, -2.0f, fnan]
        // Mask bitset has bits {0, 2, 3, 4} set (bit 1 is unset -> NULL!).
        // Therefore:
        //   Lane 0 valid values = node 0 (-8.0f) and node 3 (-2.0f) -> COUNT=2, ADD=-10, MUL=16, MIN=-8, MAX=-2, ARGMIN=0, ARGMAX=3
        //   Lane 1 valid values = {} (node 1 masked out, node 4 is NaN!) -> COUNT=0, ADD=0, MUL=0, MIN=0, MAX=0, ARGMIN=UINT64_MAX, ARGMAX=UINT64_MAX
        //   Lane 2 valid values = {} (untouched) -> COUNT=0, ADD=0, MUL=0, MIN=0, MAX=0, ARGMIN=UINT64_MAX, ARGMAX=UINT64_MAX
        uint32_t csr_off[6] = { 0, 4, 6, 6, 6, 6 };
        uint32_t csr_tgt[6] = { 0, 1, 2, 3, 1, 4 };
        impulse_vm_context_mock_csr(ctx, 0, csr_off, csr_tgt, 5, 6);

        float mock_vals[5] = { -8.0f, -99.0f, fnan, -2.0f, fnan };
        impulse_vm_context_bind_inline_data(ctx, mock_vals, sizeof(mock_vals));

        int h_mask = impulse_vm_context_acquire_bitset(ctx);
        impulse_vm_context_bitset_add(ctx, h_mask, 0);
        impulse_vm_context_bitset_add(ctx, h_mask, 2);
        impulse_vm_context_bitset_add(ctx, h_mask, 3);
        impulse_vm_context_bitset_add(ctx, h_mask, 4); // node 1 intentionally omitted!

        impulse_vm_state_t st{};
        st.query_context = ctx;
        st.registers[5] = static_cast<uint64_t>(h_mask);
        st.register_types[5] = TYPE_BITSET_HANDLE;

        std::vector<impulse_instruction_t> prog = {
            { OP_LOAD_INLINE_ARRAY, 0, 1, 0 | (5 << 16) },
            { OP_INIT_MOCK_NODE_ATTR, 0, 7, 1 | (5 << 8) }, // attr 7 = R1 with validity mask R5!
            { OP_LANE_INIT_SEEDS, 1, 2, (3 << 16) },
            { OP_LANE_CSR_WALK, 0, 3, 2 | (0 << 16) },
            { OP_LANE_REDUCE_ATTR, 0, 10, 3 | (7 << 8) | (BINARY_OP_ADD << 16) },
            { OP_LANE_REDUCE_ATTR, 0, 11, 3 | (7 << 8) | (BINARY_OP_MUL << 16) },
            { OP_LANE_REDUCE_ATTR, 0, 12, 3 | (7 << 8) | (BINARY_OP_MIN << 16) },
            { OP_LANE_REDUCE_ATTR, 0, 13, 3 | (7 << 8) | (BINARY_OP_MAX << 16) },
            { OP_LANE_REDUCE_ATTR, 0, 14, 3 | (7 << 8) | (4 << 16) }, // COUNT
            { OP_LANE_REDUCE_ATTR, 0, 15, 3 | (7 << 8) | (BINARY_OP_ARGMIN << 16) },
            { OP_LANE_REDUCE_ATTR, 0, 16, 3 | (7 << 8) | (BINARY_OP_ARGMAX << 16) },
            { OP_HALT, 0, 0, 0 }
        };
        assert(impulse_vm_execute(prog.data(), prog.size(), &st, 0) == IMPULSE_VM_OK);
        assert((st.flags & 0x1ULL) == 0ULL); // ZF == false because lane 0 found a valid node!

        const float* v_add = impulse_vm_context_get_float_vector(ctx, st.registers[10]);
        const float* v_mul = impulse_vm_context_get_float_vector(ctx, st.registers[11]);
        const float* v_min = impulse_vm_context_get_float_vector(ctx, st.registers[12]);
        const float* v_max = impulse_vm_context_get_float_vector(ctx, st.registers[13]);
        const float* v_cnt = impulse_vm_context_get_float_vector(ctx, st.registers[14]);
        const uint64_t* v_argmin = impulse_vm_context_get_node_vector(ctx, st.registers[15]);
        const uint64_t* v_argmax = impulse_vm_context_get_node_vector(ctx, st.registers[16]);

        assert(v_add[0] == -10.0f && v_add[1] == 0.0f && v_add[2] == 0.0f);
        assert(v_mul[0] == 16.0f  && v_mul[1] == 0.0f && v_mul[2] == 0.0f);
        assert(v_min[0] == -8.0f  && v_min[1] == 0.0f && v_min[2] == 0.0f);
        assert(v_max[0] == -2.0f  && v_max[1] == 0.0f && v_max[2] == 0.0f);
        assert(v_cnt[0] == 2.0f   && v_cnt[1] == 0.0f && v_cnt[2] == 0.0f);
        assert(v_argmin[0] == 0ULL && v_argmin[1] == ~0ULL && v_argmin[2] == ~0ULL);
        assert(v_argmax[0] == 3ULL && v_argmax[1] == ~0ULL && v_argmax[2] == ~0ULL);

        // Now verify ZF == true when ALL lanes only see masked/NaN attributes (seed = node 1 -> {1, 4})!
        st.registers[20] = 1ULL;
        st.register_types[20] = TYPE_NODE_ID;
        std::vector<impulse_instruction_t> all_null_prog = {
            { OP_LANE_INIT_SEEDS, 0, 21, 20 },
            { OP_LANE_CSR_WALK, 0, 22, 21 | (0 << 16) },
            { OP_LANE_REDUCE_ATTR, 0, 23, 22 | (7 << 8) | (4 << 16) }, // COUNT -> 0, ZF=true!
            { OP_HALT, 0, 0, 0 }
        };
        st.pc = 0;
        assert(impulse_vm_execute(all_null_prog.data(), all_null_prog.size(), &st, 0) == IMPULSE_VM_OK);
        assert((st.flags & 0x1ULL) != 0ULL); // ZF == true!
        const float* v_cnt_null = impulse_vm_context_get_float_vector(ctx, st.registers[23]);
        assert(v_cnt_null[0] == 0.0f);

        std::vector<impulse_instruction_t> all_null_arg = {
            { OP_LANE_REDUCE_ATTR, 0, 24, 22 | (7 << 8) | (BINARY_OP_ARGMIN << 16) }, // ARGMIN -> ~0ULL, ZF=true!
            { OP_HALT, 0, 0, 0 }
        };
        st.pc = 0;
        assert(impulse_vm_execute(all_null_arg.data(), all_null_arg.size(), &st, 0) == IMPULSE_VM_OK);
        assert((st.flags & 0x1ULL) != 0ULL); // ZF == true!
        const uint64_t* v_arg_null = impulse_vm_context_get_node_vector(ctx, st.registers[24]);
        assert(v_arg_null[0] == ~0ULL);

        impulse_vm_context_destroy(ctx);
    }

    // -----------------------------------------------------------------------
    // Part D: Stream Reducers (0xA8, 0xB8, 0xB9, 0xBF) with Negative + NaN & All-NaN Edges
    // -----------------------------------------------------------------------
    {
        // Graph: 4 nodes (0..3)
        // Edges from active frontier {0, 1, 2}:
        //   0 -> 3 (weight NaN)
        //   1 -> 3 (weight -10.0f)
        //   2 -> 3 (weight -3.0f)
        //   0 -> 2 (weight NaN — ONLY NaN touches target 2!)
        uint32_t csr_off[5] = { 0, 2, 3, 4, 4 };
        uint32_t csr_tgt[4] = { 3, 2, 3, 3 };
        float edge_w[4]     = { fnan, fnan, -10.0f, -3.0f };

        impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr);
        impulse_vm_context_mock_csr(ctx, 0, csr_off, csr_tgt, 4, 4);
        impulse_vm_context_bind_inline_data(ctx, edge_w, sizeof(edge_w));

        int h_front = impulse_vm_context_acquire_bitset(ctx);
        impulse_vm_context_bitset_add(ctx, h_front, 0);
        impulse_vm_context_bitset_add(ctx, h_front, 1);
        impulse_vm_context_bitset_add(ctx, h_front, 2);

        // Pre-allocate scatter vector handle `h_scat` initialized to -5.0f
        int h_scat = impulse_vm_context_acquire_float_vector(ctx);
        for (size_t i = 0; i < 4; ++i) impulse_vm_context_float_vector_set(ctx, h_scat, i, -5.0f);

        impulse_vm_state_t st{};
        st.query_context = ctx;
        st.registers[10] = static_cast<uint64_t>(h_front);
        st.register_types[10] = TYPE_BITSET_HANDLE;

        std::vector<impulse_instruction_t> prog = {
            { OP_LOAD_INLINE_ARRAY, 0, 1, 0 | (4 << 16) },
            { OP_INIT_MOCK_EDGE_ATTR, 0, 0, 1 }, // edge attr 0 = R1 (R0 is TYPE_NULL)
            { OP_CSR_WALK_STREAM, 3, 11, 10 | (0 << 16) },
            { OP_STREAM_FUNC_BEGIN, 0, 0, 0 },
            { OP_STREAM_LOAD_EDGE, 0, 0, 0 },
            { OP_STREAM_REDUCE, 0, 0, 2 | (0 << 16) }, // R2 = SUM
            { OP_STREAM_REDUCE, 0, 0, 3 | (1 << 16) }, // R3 = MAX
            { OP_STREAM_REDUCE, 0, 0, 4 | (2 << 16) }, // R4 = MIN
            { OP_STREAM_REDUCE_ARGMIN, 0, 0, 5 | (6 << 16) }, // R5 = min val vec, R6 = argmin src node
            { OP_STREAM_REDUCE_ARGMAX, 0, 0, 7 | (8 << 16) }, // R7 = max val vec, R8 = argmax src node
            { OP_STREAM_SCATTER_REDUCE, 0, 0, static_cast<uint32_t>(h_scat) | (1 << 16) }, // MAX into h_scat
            { OP_STREAM_FUNC_END, 0, 0, 0 },
            { OP_HALT, 0, 0, 0 }
        };
        assert(impulse_vm_execute(prog.data(), prog.size(), &st, 0) == IMPULSE_VM_OK);

        const float* r_sum = impulse_vm_context_get_float_vector(ctx, st.registers[2]);
        const float* r_max = impulse_vm_context_get_float_vector(ctx, st.registers[3]);
        const float* r_min = impulse_vm_context_get_float_vector(ctx, st.registers[4]);
        const float* r_amin_v = impulse_vm_context_get_float_vector(ctx, st.registers[5]);
        const float* r_amax_v = impulse_vm_context_get_float_vector(ctx, st.registers[7]);
        const float* r_scat = impulse_vm_context_get_float_vector(ctx, h_scat);

        // Target 3 had [NaN (from 0), -10.0f (from 1), -3.0f (from 2)]:
        assert(r_sum[3] == -13.0f);
        assert(r_max[3] == -3.0f);   // negative MAX preserved, NaN skipped!
        assert(r_min[3] == -10.0f);
        assert(r_amin_v[3] == -10.0f && st.registers[6] == 1ULL);
        assert(r_amax_v[3] == -3.0f  && st.registers[8] == 2ULL);
        assert(r_scat[3] == -3.0f);  // max(-5.0f, -10.0f, -3.0f) == -3.0f, NaN skipped!

        // Target 2 ONLY received a NaN edge (0 -> 2): untouched entry stays 0.0f (and -5.0f in scatter)!
        assert(r_sum[2] == 0.0f);
        assert(r_max[2] == 0.0f);
        assert(r_min[2] == 0.0f);
        assert(r_amin_v[2] == 0.0f);
        assert(r_amax_v[2] == 0.0f);
        assert(r_scat[2] == -5.0f);

        // All-NaN edge stream (frontier = {0} only -> edges 0->3 (NaN) and 0->2 (NaN)):
        // Global target float vectors MUST still be allocated as TYPE_FLOAT_VECTOR with 0.0f entries!
        int h_only0 = impulse_vm_context_acquire_bitset(ctx);
        impulse_vm_context_bitset_add(ctx, h_only0, 0);
        impulse_vm_state_t st_nan{};
        st_nan.query_context = ctx;
        st_nan.registers[10] = static_cast<uint64_t>(h_only0);
        st_nan.register_types[10] = TYPE_BITSET_HANDLE;
        std::vector<impulse_instruction_t> nan_stream_prog = {
            { OP_CSR_WALK_STREAM, 1, 11, 10 | (0 << 16) },
            { OP_STREAM_FUNC_BEGIN, 0, 0, 0 },
            { OP_STREAM_LOAD_EDGE, 0, 0, 0 },
            { OP_STREAM_REDUCE, 0, 0, 2 | (1 << 16) },
            { OP_STREAM_REDUCE_ARGMIN, 0, 0, 5 | (6 << 16) },
            { OP_STREAM_REDUCE_ARGMAX, 0, 0, 7 | (8 << 16) },
            { OP_STREAM_FUNC_END, 0, 0, 0 },
            { OP_HALT, 0, 0, 0 }
        };
        assert(impulse_vm_execute(nan_stream_prog.data(), nan_stream_prog.size(), &st_nan, 0) == IMPULSE_VM_OK);
        assert(st_nan.register_types[2] == TYPE_FLOAT_VECTOR);
        assert(st_nan.register_types[5] == TYPE_FLOAT_VECTOR);
        assert(st_nan.register_types[7] == TYPE_FLOAT_VECTOR);
        assert(st_nan.register_types[6] == TYPE_NULL); // argReg untouched on all-NaN!
        assert(st_nan.register_types[8] == TYPE_NULL);
        const float* nan_r2 = impulse_vm_context_get_float_vector(ctx, st_nan.registers[2]);
        assert(nan_r2[2] == 0.0f && nan_r2[3] == 0.0f);

        impulse_vm_context_destroy(ctx);
    }

    std::cout << "  -> PASSED" << std::endl;
}

int main() {
    std::cout << "=== ImpulseVM Vector Math & Multi-Layout Sweep Test Suite ===" << std::endl;
    test_all_42_math_functions_scalar_and_simd();
    test_vm_vector_math_opcodes();
    test_vector_predicates_and_masks();
    test_fixpoint_and_frontier_diff();
    test_multi_layout_sweeps();
    test_fp_assertions_and_safe_math();
    test_exhaustive_54_math_ieee754_domain_and_omp();
    test_nullable_vectors_and_monoidic_reducers();
    std::cout << "=== ALL VECTOR MATH TESTS PASSED ===" << std::endl;
    return 0;
}
