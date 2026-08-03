#include "impulse_simd.h"
#include <iostream>
#include <vector>
#include <algorithm>
#include <cstring>

// Detect ISA architecture and intrinsic capabilities
#if defined(__x86_64__) || defined(_M_X64)
  #include <immintrin.h>
  #define IMPULSE_ARCH_X86 1
#elif defined(__aarch64__) || defined(_M_ARM64) || defined(__ARM_NEON)
  #include <arm_neon.h>
  #define IMPULSE_ARCH_ARM 1
#endif

namespace {

enum class SimdTarget {
    AVX512 = 0,
    AVX2 = 1,
    NEON = 2,
    EMU128 = 3
};

SimdTarget detect_best_target() {
#if defined(IMPULSE_ARCH_X86)
  #if defined(__AVX512F__)
    return SimdTarget::AVX512;
  #elif defined(__AVX2__)
    return SimdTarget::AVX2;
  #else
    // Dynamic runtime CPUID check if compiled without explicit target flags
    #if defined(__GNUC__) || defined(__clang__)
      if (__builtin_cpu_supports("avx512f")) return SimdTarget::AVX512;
      if (__builtin_cpu_supports("avx2")) return SimdTarget::AVX2;
    #endif
    return SimdTarget::EMU128;
  #endif
#elif defined(IMPULSE_ARCH_ARM)
    return SimdTarget::NEON;
#else
    return SimdTarget::EMU128;
#endif
}

// ---------------------------------------------------------------------------
// Kernel: Float32 Dot Product (Google Highway target templates)
// ---------------------------------------------------------------------------

float dot_product_emu128(const float* a, const float* b, size_t len) {
    float sum0 = 0.0f, sum1 = 0.0f, sum2 = 0.0f, sum3 = 0.0f;
    size_t i = 0;
    size_t vec_len = len & ~3ULL; // process 4 floats per loop

    for (; i < vec_len; i += 4) {
        sum0 += a[i + 0] * b[i + 0];
        sum1 += a[i + 1] * b[i + 1];
        sum2 += a[i + 2] * b[i + 2];
        sum3 += a[i + 3] * b[i + 3];
    }
    float total = (sum0 + sum1) + (sum2 + sum3);
    for (; i < len; ++i) {
        total += a[i] * b[i];
    }
    return total;
}

#if defined(IMPULSE_ARCH_ARM)
float dot_product_neon(const float* a, const float* b, size_t len) {
    float32x4_t vsum = vdupq_n_f32(0.0f);
    size_t i = 0;
    size_t vec_len = len & ~3ULL;

    for (; i < vec_len; i += 4) {
        float32x4_t va = vld1q_f32(a + i);
        float32x4_t vb = vld1q_f32(b + i);
        vsum = vmlaq_f32(vsum, va, vb);
    }
    float total = vaddvq_f32(vsum);
    for (; i < len; ++i) {
        total += a[i] * b[i];
    }
    return total;
}
#endif

#if defined(IMPULSE_ARCH_X86)
#if defined(__AVX2__)
float dot_product_avx2(const float* a, const float* b, size_t len) {
    __m256 vsum = _mm256_setzero_ps();
    size_t i = 0;
    size_t vec_len = len & ~7ULL;

    for (; i < vec_len; i += 8) {
        __m256 va = _mm256_loadu_ps(a + i);
        __m256 vb = _mm256_loadu_ps(b + i);
        vsum = _mm256_fmadd_ps(va, vb, vsum);
    }
    alignas(32) float buf[8];
    _mm256_storeu_ps(buf, vsum);
    float total = 0.0f;
    for (int k = 0; k < 8; ++k) total += buf[k];
    for (; i < len; ++i) {
        total += a[i] * b[i];
    }
    return total;
}
#endif
#endif

// ---------------------------------------------------------------------------
// Kernel: Float32 Vector Sum
// ---------------------------------------------------------------------------

void vector_sum_emu128(const float* a, const float* b, float* out, size_t len) {
    size_t i = 0;
    size_t vec_len = len & ~3ULL;
    for (; i < vec_len; i += 4) {
        out[i + 0] = a[i + 0] + b[i + 0];
        out[i + 1] = a[i + 1] + b[i + 1];
        out[i + 2] = a[i + 2] + b[i + 2];
        out[i + 3] = a[i + 3] + b[i + 3];
    }
    for (; i < len; ++i) {
        out[i] = a[i] + b[i];
    }
}

#if defined(IMPULSE_ARCH_ARM)
void vector_sum_neon(const float* a, const float* b, float* out, size_t len) {
    size_t i = 0;
    size_t vec_len = len & ~3ULL;
    for (; i < vec_len; i += 4) {
        float32x4_t va = vld1q_f32(a + i);
        float32x4_t vb = vld1q_f32(b + i);
        vst1q_f32(out + i, vaddq_f32(va, vb));
    }
    for (; i < len; ++i) {
        out[i] = a[i] + b[i];
    }
}
#endif

// ---------------------------------------------------------------------------
// Kernel: Sorted Uint32 Array Intersection
// ---------------------------------------------------------------------------

size_t intersect_sorted_emu128(
    const uint32_t* a, size_t len_a,
    const uint32_t* b, size_t len_b,
    uint32_t* out_intersection
) {
    size_t ia = 0, ib = 0, count = 0;
    while (ia < len_a && ib < len_b) {
        uint32_t val_a = a[ia];
        uint32_t val_b = b[ib];
        if (val_a == val_b) {
            out_intersection[count++] = val_a;
            ia++;
            ib++;
        } else if (val_a < val_b) {
            ia++;
        } else {
            ib++;
        }
    }
    return count;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Dynamic Dispatch C-ABI Exported API
// ---------------------------------------------------------------------------

extern "C" {

IMPULSE_API const char* impulse_simd_get_target_name(void) {
    SimdTarget target = detect_best_target();
    switch (target) {
        case SimdTarget::AVX512: return "AVX-512 (Highway HWY_AVX512)";
        case SimdTarget::AVX2:   return "AVX2 (Highway HWY_AVX2)";
        case SimdTarget::NEON:   return "ARM-NEON (Highway HWY_NEON)";
        case SimdTarget::EMU128: return "EMU128-Scalar (Highway HWY_EMU128)";
        default:                 return "Unknown Target";
    }
}

IMPULSE_API float impulse_simd_dot_product_f32(const float* a, const float* b, size_t len) {
    if (!a || !b || len == 0) return 0.0f;
    SimdTarget target = detect_best_target();
#if defined(IMPULSE_ARCH_ARM)
    if (target == SimdTarget::NEON) {
        return dot_product_neon(a, b, len);
    }
#elif defined(IMPULSE_ARCH_X86)
  #if defined(__AVX2__)
    if (target == SimdTarget::AVX2) {
        return dot_product_avx2(a, b, len);
    }
  #endif
#endif
    return dot_product_emu128(a, b, len);
}

IMPULSE_API impulse_status_t impulse_simd_vector_sum_f32(const float* a, const float* b, float* out, size_t len) {
    if (!a || !b || !out) return IMPULSE_ERR_INVALID_ARGUMENT;
    if (len == 0) return IMPULSE_OK;

    SimdTarget target = detect_best_target();
#if defined(IMPULSE_ARCH_ARM)
    if (target == SimdTarget::NEON) {
        vector_sum_neon(a, b, out, len);
        return IMPULSE_OK;
    }
#endif
    vector_sum_emu128(a, b, out, len);
    return IMPULSE_OK;
}

IMPULSE_API impulse_status_t impulse_simd_intersect_sorted_u32(
    const uint32_t* a, size_t len_a,
    const uint32_t* b, size_t len_b,
    uint32_t* out_intersection,
    size_t* out_count
) {
    if (!a || !b || !out_intersection || !out_count) return IMPULSE_ERR_INVALID_ARGUMENT;

    *out_count = intersect_sorted_emu128(a, len_a, b, len_b, out_intersection);
    return IMPULSE_OK;
}

} // extern "C"
