#include "impulse_simd.h"
#include "impulse_assert.h"
#include <iostream>
#include <vector>
#include <algorithm>
#include <cstring>

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "impulse_simd.cpp"
#include "hwy/foreach_target.h"
#include "hwy/highway.h"

HWY_BEFORE_NAMESPACE();
namespace impulse {
namespace HWY_NAMESPACE {

namespace hn = hwy::HWY_NAMESPACE;

// Kernel 1: Float32 Dot Product
float DotProductF32(const float* HWY_RESTRICT a, const float* HWY_RESTRICT b, size_t len) {
    IMPULSE_ASSERT(len == 0 || (a != nullptr && b != nullptr));
    const hn::ScalableTag<float> d;
    auto sum = hn::Zero(d);
    const size_t N = hn::Lanes(d);
    size_t i = 0;
    for (; i + N <= len; i += N) {
        const auto va = hn::LoadU(d, a + i);
        const auto vb = hn::LoadU(d, b + i);
        sum = hn::MulAdd(va, vb, sum);
    }
    float total = hn::ReduceSum(d, sum);
    for (; i < len; ++i) {
        total += a[i] * b[i];
    }
    return total;
}

// Kernel 2: Float32 Vector Sum
void VectorSumF32(const float* HWY_RESTRICT a, const float* HWY_RESTRICT b, float* HWY_RESTRICT out, size_t len) {
    IMPULSE_ASSERT(len == 0 || (a != nullptr && b != nullptr && out != nullptr));
    const hn::ScalableTag<float> d;
    const size_t N = hn::Lanes(d);
    size_t i = 0;
    for (; i + N <= len; i += N) {
        const auto va = hn::LoadU(d, a + i);
        const auto vb = hn::LoadU(d, b + i);
        hn::StoreU(hn::Add(va, vb), d, out + i);
    }
    for (; i < len; ++i) {
        out[i] = a[i] + b[i];
    }
}

// Kernel 3: Float32 Reduce Sum
float ReduceSumF32(const float* HWY_RESTRICT a, size_t len) {
    const hn::ScalableTag<float> d;
    auto sum = hn::Zero(d);
    const size_t N = hn::Lanes(d);
    size_t i = 0;
    for (; i + N <= len; i += N) {
        const auto va = hn::LoadU(d, a + i);
        sum = hn::Add(sum, va);
    }
    float total = hn::ReduceSum(d, sum);
    for (; i < len; ++i) {
        total += a[i];
    }
    return total;
}

// Kernel 4: Float32 Vector Scale
void VectorScaleF32(float* HWY_RESTRICT a, float scalar, size_t len) {
    const hn::ScalableTag<float> d;
    const auto vscalar = hn::Set(d, scalar);
    const size_t N = hn::Lanes(d);
    size_t i = 0;
    for (; i + N <= len; i += N) {
        const auto va = hn::LoadU(d, a + i);
        hn::StoreU(hn::Mul(va, vscalar), d, a + i);
    }
    for (; i < len; ++i) {
        a[i] *= scalar;
    }
}

// Kernel 5: Float32 Vector Multiply
void VectorMulF32(float* HWY_RESTRICT a, const float* HWY_RESTRICT b, size_t len) {
    const hn::ScalableTag<float> d;
    const size_t N = hn::Lanes(d);
    size_t i = 0;
    for (; i + N <= len; i += N) {
        const auto va = hn::LoadU(d, a + i);
        const auto vb = hn::LoadU(d, b + i);
        hn::StoreU(hn::Mul(va, vb), d, a + i);
    }
    for (; i < len; ++i) {
        a[i] *= b[i];
    }
}

// Kernel 6: Sorted Uint32 Array Intersection
size_t IntersectSortedU32(const uint32_t* HWY_RESTRICT a, size_t len_a,
                           const uint32_t* HWY_RESTRICT b, size_t len_b,
                           uint32_t* HWY_RESTRICT out_intersection) {
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

}  // namespace HWY_NAMESPACE
}  // namespace impulse
HWY_AFTER_NAMESPACE();

#if HWY_ONCE

namespace impulse {
HWY_EXPORT(DotProductF32);
HWY_EXPORT(VectorSumF32);
HWY_EXPORT(ReduceSumF32);
HWY_EXPORT(VectorScaleF32);
HWY_EXPORT(VectorMulF32);
HWY_EXPORT(IntersectSortedU32);

const char* GetTargetName() {
    return hwy::TargetName(HWY_TARGET);
}

float DispatchDotProductF32(const float* a, const float* b, size_t len) {
    return HWY_DYNAMIC_DISPATCH(DotProductF32)(a, b, len);
}

void DispatchVectorSumF32(const float* a, const float* b, float* out, size_t len) {
    HWY_DYNAMIC_DISPATCH(VectorSumF32)(a, b, out, len);
}

size_t DispatchIntersectSortedU32(const uint32_t* a, size_t len_a, const uint32_t* b, size_t len_b, uint32_t* out_intersection) {
    return HWY_DYNAMIC_DISPATCH(IntersectSortedU32)(a, len_a, b, len_b, out_intersection);
}

float DispatchReduceSumF32(const float* a, size_t len) {
    return HWY_DYNAMIC_DISPATCH(ReduceSumF32)(a, len);
}

void DispatchVectorScaleF32(float* a, float scalar, size_t len) {
    HWY_DYNAMIC_DISPATCH(VectorScaleF32)(a, scalar, len);
}

void DispatchVectorMulF32(float* a, const float* b, size_t len) {
    HWY_DYNAMIC_DISPATCH(VectorMulF32)(a, b, len);
}

}  // namespace impulse

extern "C" {

IMPULSE_API const char* impulse_simd_get_target_name(void) {
    return impulse::GetTargetName();
}

IMPULSE_API float impulse_simd_dot_product_f32(const float* a, const float* b, size_t len) {
    if (!a || !b || len == 0) return 0.0f;
    return impulse::DispatchDotProductF32(a, b, len);
}

IMPULSE_API impulse_status_t impulse_simd_vector_sum_f32(const float* a, const float* b, float* out, size_t len) {
    if (!a || !b || !out) return IMPULSE_ERR_INVALID_ARGUMENT;
    if (len == 0) return IMPULSE_OK;
    impulse::DispatchVectorSumF32(a, b, out, len);
    return IMPULSE_OK;
}

IMPULSE_API impulse_status_t impulse_simd_intersect_sorted_u32(
    const uint32_t* a, size_t len_a,
    const uint32_t* b, size_t len_b,
    uint32_t* out_intersection,
    size_t* out_count
) {
    if (!a || !b || !out_intersection || !out_count) return IMPULSE_ERR_INVALID_ARGUMENT;
    *out_count = impulse::DispatchIntersectSortedU32(a, len_a, b, len_b, out_intersection);
    return IMPULSE_OK;
}

IMPULSE_API float impulse_simd_reduce_sum_f32(const float* a, size_t len) {
    if (!a || len == 0) return 0.0f;
    return impulse::DispatchReduceSumF32(a, len);
}

IMPULSE_API impulse_status_t impulse_simd_vector_scale_f32(float* a, float scalar, size_t len) {
    if (!a) return IMPULSE_ERR_INVALID_ARGUMENT;
    if (len == 0) return IMPULSE_OK;
    impulse::DispatchVectorScaleF32(a, scalar, len);
    return IMPULSE_OK;
}

IMPULSE_API impulse_status_t impulse_simd_vector_mul_f32(float* a, const float* b, size_t len) {
    if (!a || !b) return IMPULSE_ERR_INVALID_ARGUMENT;
    if (len == 0) return IMPULSE_OK;
    impulse::DispatchVectorMulF32(a, b, len);
    return IMPULSE_OK;
}

}  // extern "C"

#endif  // HWY_ONCE
