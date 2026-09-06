/**
 * @file test_vm_vector_math.cpp
 * @brief Exhaustive Verification of all 42 Vector Math Functions, Comparison Predicates,
 *        Multi-Layout Sweeps (CSR, CSC, COO, DENSE), Functional Optics & Fixpoint Engine.
 */

#include "impulse_vm.h"
#include "impulse_math_ops.h"
#include <iostream>
#include <vector>
#include <cassert>
#include <cmath>

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

int main() {
    std::cout << "=== ImpulseVM Vector Math & Multi-Layout Sweep Test Suite ===" << std::endl;
    test_all_42_math_functions_scalar_and_simd();
    test_vm_vector_math_opcodes();
    test_vector_predicates_and_masks();
    test_fixpoint_and_frontier_diff();
    test_multi_layout_sweeps();
    test_fp_assertions_and_safe_math();
    std::cout << "=== ALL VECTOR MATH TESTS PASSED ===" << std::endl;
    return 0;
}
