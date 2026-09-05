#include "impulse_simd.h"
#include <iostream>
#include <vector>
#include <cassert>
#include <cmath>

void test_target_name() {
    const char* target_name = impulse_simd_get_target_name();
    std::cout << "[SIMD Test] Active Highway Dynamic Target: " << target_name << std::endl;
    assert(target_name != nullptr);
}

void test_dot_product() {
    const size_t N = 1000;
    std::vector<float> a(N), b(N);
    float expected_sum = 0.0f;
    for (size_t i = 0; i < N; ++i) {
        a[i] = static_cast<float>(i + 1) * 0.1f;
        b[i] = 2.0f;
        expected_sum += a[i] * b[i];
    }

    float res = impulse_simd_dot_product_f32(a.data(), b.data(), N);
    std::cout << "[SIMD Test] Dot Product (N=1000): " << res << " (expected: " << expected_sum << ")" << std::endl;
    assert(std::abs(res - expected_sum) < 1e-3f);
}

void test_vector_sum() {
    const size_t N = 512;
    std::vector<float> a(N), b(N), out(N, 0.0f);
    for (size_t i = 0; i < N; ++i) {
        a[i] = static_cast<float>(i);
        b[i] = static_cast<float>(N - i);
    }

    impulse_status_t status = impulse_simd_vector_sum_f32(a.data(), b.data(), out.data(), N);
    assert(status == IMPULSE_OK);
    (void)status;

    for (size_t i = 0; i < N; ++i) {
        assert(out[i] == static_cast<float>(N));
    }
    std::cout << "[SIMD Test] Vector Sum (N=512): PASSED" << std::endl;
}

void test_sorted_intersection() {
    std::vector<uint32_t> a = {1, 5, 10, 15, 20, 25, 30, 40, 50};
    std::vector<uint32_t> b = {2, 5, 8, 10, 20, 30, 60};
    std::vector<uint32_t> out(std::max(a.size(), b.size()));
    size_t count = 0;

    impulse_status_t status = impulse_simd_intersect_sorted_u32(a.data(), a.size(), b.data(), b.size(), out.data(), &count);
    assert(status == IMPULSE_OK);
    (void)status;
    assert(count == 4);
    assert(out[0] == 5);
    assert(out[1] == 10);
    assert(out[2] == 20);
    assert(out[3] == 30);

    std::cout << "[SIMD Test] Sorted Intersection (Count=4): PASSED" << std::endl;
}

void test_simd_mcdc_boundaries() {
    std::cout << "[SIMD Test] Running MC/DC Boundary & Null Argument Tests..." << std::endl;

    // 1. Null and len == 0 checks for dot product
    float fa[] = { 1.0f, 2.0f, 3.0f };
    float fb[] = { 4.0f, 5.0f, 6.0f };
    float fout[8] = { 0.0f };
    size_t count = 0;

    assert(impulse_simd_dot_product_f32(nullptr, fb, 3) == 0.0f);
    assert(impulse_simd_dot_product_f32(fa, nullptr, 3) == 0.0f);
    assert(impulse_simd_dot_product_f32(fa, fb, 0) == 0.0f);

    // 2. Vector Sum Null & len == 0 checks
    assert(impulse_simd_vector_sum_f32(nullptr, fb, fout, 3) == IMPULSE_ERR_INVALID_ARGUMENT);
    assert(impulse_simd_vector_sum_f32(fa, nullptr, fout, 3) == IMPULSE_ERR_INVALID_ARGUMENT);
    assert(impulse_simd_vector_sum_f32(fa, fb, nullptr, 3) == IMPULSE_ERR_INVALID_ARGUMENT);
    assert(impulse_simd_vector_sum_f32(fa, fb, fout, 0) == IMPULSE_OK);

    // 3. Intersect Sorted U32 Null checks & len == 0 checks
    uint32_t ua[] = { 1, 3, 5, 7 };
    uint32_t ub[] = { 2, 3, 6, 7 };
    uint32_t uout[8] = { 0 };

    assert(impulse_simd_intersect_sorted_u32(nullptr, 4, ub, 4, uout, &count) == IMPULSE_ERR_INVALID_ARGUMENT);
    assert(impulse_simd_intersect_sorted_u32(ua, 4, nullptr, 4, uout, &count) == IMPULSE_ERR_INVALID_ARGUMENT);
    assert(impulse_simd_intersect_sorted_u32(ua, 4, ub, 4, nullptr, &count) == IMPULSE_ERR_INVALID_ARGUMENT);
    assert(impulse_simd_intersect_sorted_u32(ua, 4, ub, 4, uout, nullptr) == IMPULSE_ERR_INVALID_ARGUMENT);
    assert(impulse_simd_intersect_sorted_u32(ua, 0, ub, 4, uout, &count) == IMPULSE_OK && count == 0);
    assert(impulse_simd_intersect_sorted_u32(ua, 4, ub, 0, uout, &count) == IMPULSE_OK && count == 0);

    // 4. Reduce Sum F32 Null & len == 0
    assert(impulse_simd_reduce_sum_f32(nullptr, 3) == 0.0f);
    assert(impulse_simd_reduce_sum_f32(fa, 0) == 0.0f);
    assert(std::abs(impulse_simd_reduce_sum_f32(fa, 3) - 6.0f) < 1e-4f);

    // 5. Vector Scale F32 Null & len == 0
    assert(impulse_simd_vector_scale_f32(nullptr, 2.0f, 3) == IMPULSE_ERR_INVALID_ARGUMENT);
    assert(impulse_simd_vector_scale_f32(fa, 2.0f, 0) == IMPULSE_OK);
    assert(impulse_simd_vector_scale_f32(fa, 2.0f, 3) == IMPULSE_OK);
    assert(fa[0] == 2.0f && fa[1] == 4.0f && fa[2] == 6.0f);

    // 6. Vector Mul F32 Null & len == 0
    assert(impulse_simd_vector_mul_f32(nullptr, fb, 3) == IMPULSE_ERR_INVALID_ARGUMENT);
    assert(impulse_simd_vector_mul_f32(fa, nullptr, 3) == IMPULSE_ERR_INVALID_ARGUMENT);
    assert(impulse_simd_vector_mul_f32(fa, fb, 0) == IMPULSE_OK);
    assert(impulse_simd_vector_mul_f32(fa, fb, 3) == IMPULSE_OK);
    assert(fa[0] == 8.0f && fa[1] == 20.0f && fa[2] == 36.0f);

    // 7. SIMD boundary lane lengths across various widths (1, W-1, W, W+1, 2W)
    std::vector<size_t> test_lengths = { 1, 2, 3, 4, 5, 7, 8, 9, 15, 16, 17, 31, 32, 33, 64, 65, 127, 128, 129 };
    for (size_t len : test_lengths) {
        std::vector<float> vec_a(len, 1.5f);
        std::vector<float> vec_b(len, 2.0f);
        std::vector<float> vec_out(len, 0.0f);

        float dot = impulse_simd_dot_product_f32(vec_a.data(), vec_b.data(), len);
        float expected_dot = static_cast<float>(len) * 3.0f;
        if (len > 0) {
            assert(std::abs(dot - expected_dot) < 1e-2f);
        }

        impulse_status_t r_sum = impulse_simd_vector_sum_f32(vec_a.data(), vec_b.data(), vec_out.data(), len);
        assert(r_sum == IMPULSE_OK);
        for (size_t k = 0; k < len; ++k) {
            assert(std::abs(vec_out[k] - 3.5f) < 1e-4f);
        }

        float r_red = impulse_simd_reduce_sum_f32(vec_a.data(), len);
        float expected_red = static_cast<float>(len) * 1.5f;
        if (len > 0) {
            assert(std::abs(r_red - expected_red) < 1e-2f);
        }

        impulse_status_t r_scale = impulse_simd_vector_scale_f32(vec_a.data(), 2.0f, len);
        assert(r_scale == IMPULSE_OK);
        for (size_t k = 0; k < len; ++k) {
            assert(std::abs(vec_a[k] - 3.0f) < 1e-4f);
        }

        impulse_status_t r_mul = impulse_simd_vector_mul_f32(vec_a.data(), vec_b.data(), len);
        assert(r_mul == IMPULSE_OK);
        for (size_t k = 0; k < len; ++k) {
            assert(std::abs(vec_a[k] - 6.0f) < 1e-4f);
        }
    }
}

int main() {
    std::cout << "--- Impulse Highway SIMD Unit Test Suite ---" << std::endl;
    test_target_name();
    test_dot_product();
    test_vector_sum();
    test_sorted_intersection();
    test_simd_mcdc_boundaries();
    std::cout << "All SIMD tests completed successfully!" << std::endl;
    return 0;
}
