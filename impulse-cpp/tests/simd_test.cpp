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
    assert(count == 4);
    assert(out[0] == 5);
    assert(out[1] == 10);
    assert(out[2] == 20);
    assert(out[3] == 30);

    std::cout << "[SIMD Test] Sorted Intersection (Count=4): PASSED" << std::endl;
}

int main() {
    std::cout << "--- Impulse Highway SIMD Unit Test Suite ---" << std::endl;
    test_target_name();
    test_dot_product();
    test_vector_sum();
    test_sorted_intersection();
    std::cout << "All SIMD tests completed successfully!" << std::endl;
    return 0;
}
