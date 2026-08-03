#ifndef IMPULSE_SIMD_H
#define IMPULSE_SIMD_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "impulse_graph.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Returns the human-readable string name of the active dynamic SIMD runtime target
 * (e.g., "AVX-512", "AVX2", "ARM-NEON", "WASM-SIMD128", "EMU128-Scalar").
 */
IMPULSE_API const char* impulse_simd_get_target_name(void);

/**
 * Architecture-independent Highway SIMD float32 dot product.
 * Computes dot_product(a, b) = sum_{i=0..len-1} (a[i] * b[i]).
 */
IMPULSE_API float impulse_simd_dot_product_f32(const float* a, const float* b, size_t len);

/**
 * Architecture-independent Highway SIMD float32 elementwise sum.
 * Computes out[i] = a[i] + b[i] for i in 0..len-1.
 */
IMPULSE_API impulse_status_t impulse_simd_vector_sum_f32(const float* a, const float* b, float* out, size_t len);

/**
 * Architecture-independent Highway SIMD sorted uint32 array intersection.
 * Computes intersection of sorted arrays `a` and `b`, writing common elements to `out_intersection`.
 * Returns total number of intersected elements written via `out_count`.
 */
IMPULSE_API impulse_status_t impulse_simd_intersect_sorted_u32(
    const uint32_t* a, size_t len_a,
    const uint32_t* b, size_t len_b,
    uint32_t* out_intersection,
    size_t* out_count
);

#ifdef __cplusplus
}
#endif

#endif // IMPULSE_SIMD_H
