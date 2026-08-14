/**
 * @file impulse_math_ops.h
 * @brief Table-Driven 42-Function Vector Math Engine & SIMD Transcendental Catalog.
 *
 * Implements vectorized mathematical, trigonometric, exponential, hyperbolic,
 * statistical, rounding, and GNN neural activation functions over off-heap float/double/int vectors.
 */

#ifndef IMPULSE_MATH_OPS_H
#define IMPULSE_MATH_OPS_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <math.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Canonical 42 Vector Math Function Identifiers (func_id).
 */
typedef enum {
    // --- Algebraic & Roots (0x01 - 0x07) ---
    MATH_FUNC_ABS          = 0x01,  /**< |x| */
    MATH_FUNC_SQRT         = 0x02,  /**< sqrt(x) */
    MATH_FUNC_RSQRT        = 0x03,  /**< 1 / sqrt(x) */
    MATH_FUNC_CBRT         = 0x04,  /**< cbrt(x) */
    MATH_FUNC_POW          = 0x05,  /**< pow(x, y) */
    MATH_FUNC_HYPOT        = 0x06,  /**< sqrt(x^2 + y^2) */
    MATH_FUNC_LERP         = 0x07,  /**< a + t * (b - a) */

    // --- Exponential & Logarithmic (0x08 - 0x0F) ---
    MATH_FUNC_EXP          = 0x08,  /**< e^x */
    MATH_FUNC_EXP2         = 0x09,  /**< 2^x */
    MATH_FUNC_EXP10        = 0x0A,  /**< 10^x */
    MATH_FUNC_EXPM1        = 0x0B,  /**< e^x - 1 */
    MATH_FUNC_LOG          = 0x0C,  /**< ln(x) */
    MATH_FUNC_LOG2         = 0x0D,  /**< log2(x) */
    MATH_FUNC_LOG10        = 0x0E,  /**< log10(x) */
    MATH_FUNC_LOG1P        = 0x0F,  /**< ln(1 + x) */

    // --- Trigonometric & Spatial (0x10 - 0x17) ---
    MATH_FUNC_SIN          = 0x10,  /**< sin(x) */
    MATH_FUNC_COS          = 0x11,  /**< cos(x) */
    MATH_FUNC_TAN          = 0x12,  /**< tan(x) */
    MATH_FUNC_ASIN         = 0x13,  /**< asin(x) */
    MATH_FUNC_ACOS         = 0x14,  /**< acos(x) */
    MATH_FUNC_ATAN         = 0x15,  /**< atan(x) */
    MATH_FUNC_ATAN2        = 0x16,  /**< atan2(y, x) */
    MATH_FUNC_SINC         = 0x17,  /**< sin(x) / x */

    // --- Hyperbolic & Poincaré GNN (0x18 - 0x1D) ---
    MATH_FUNC_SINH         = 0x18,  /**< sinh(x) */
    MATH_FUNC_COSH         = 0x19,  /**< cosh(x) */
    MATH_FUNC_TANH         = 0x1A,  /**< tanh(x) */
    MATH_FUNC_ASINH        = 0x1B,  /**< asinh(x) */
    MATH_FUNC_ACOSH        = 0x1C,  /**< acosh(x) */
    MATH_FUNC_ATANH        = 0x1D,  /**< atanh(x) */

    // --- Rounding & Clamping (0x1E - 0x24) ---
    MATH_FUNC_FLOOR        = 0x1E,  /**< floor(x) */
    MATH_FUNC_CEIL         = 0x1F,  /**< ceil(x) */
    MATH_FUNC_TRUNC        = 0x20,  /**< trunc(x) */
    MATH_FUNC_ROUND        = 0x21,  /**< round(x) */
    MATH_FUNC_CLAMP        = 0x22,  /**< clamp(x, min, max) */
    MATH_FUNC_COPYSIGN     = 0x23,  /**< copysign(x, y) */
    MATH_FUNC_FMOD         = 0x24,  /**< fmod(x, y) */

    // --- GNN & Neural Activations (0x25 - 0x2A) ---
    MATH_FUNC_RELU         = 0x25,  /**< max(0, x) */
    MATH_FUNC_LEAKY_RELU   = 0x26,  /**< x > 0 ? x : 0.01 * x */
    MATH_FUNC_SIGMOID      = 0x27,  /**< 1 / (1 + exp(-x)) */
    MATH_FUNC_GELU         = 0x28,  /**< 0.5 * x * (1 + tanh(sqrt(2/pi) * (x + 0.044715 * x^3))) */
    MATH_FUNC_SILU         = 0x29,  /**< x * sigmoid(x) (Swish) */
    MATH_FUNC_SOFTPLUS     = 0x2A,  /**< ln(1 + exp(x)) */

    // --- Statistics & Special (0x2B - 0x2D) ---
    MATH_FUNC_ERF          = 0x2B,  /**< erf(x) */
    MATH_FUNC_ERFC         = 0x2C,  /**< erfc(x) */
    MATH_FUNC_LGAMMA       = 0x2D,  /**< lgamma(x) */

    // --- Discrete & Bitwise (0x2E - 0x32) ---
    MATH_FUNC_POPCOUNT     = 0x2E,  /**< popcount(x) */
    MATH_FUNC_CLZ          = 0x2F,  /**< count leading zeros */
    MATH_FUNC_CTZ          = 0x30,  /**< count trailing zeros */
    MATH_FUNC_ROTL         = 0x31,  /**< rotate left */
    MATH_FUNC_ROTR         = 0x32,  /**< rotate right */

    // --- Safe Arithmetic & IEEE 754 Sanitizers (0x33 - 0x36) ---
    MATH_FUNC_SAFE_DIV     = 0x33,  /**< y == 0 || isnan(y) ? fallback : (x / y) */
    MATH_FUNC_ISNAN        = 0x34,  /**< isnan(x) ? 1.0 : 0.0 */
    MATH_FUNC_ISINF        = 0x35,  /**< isinf(x) ? 1.0 : 0.0 */
    MATH_FUNC_ISFINITE     = 0x36   /**< isfinite(x) ? 1.0 : 0.0 */
} impulse_math_func_id_t;

#if defined(_MSC_VER)
#include <intrin.h>
static inline uint32_t impulse_builtin_popcount32(uint32_t x) {
    return (uint32_t)__popcnt(x);
}
static inline uint32_t impulse_builtin_clz32(uint32_t x) {
    unsigned long idx;
    return _BitScanReverse(&idx, x) ? (31 - idx) : 32;
}
static inline uint32_t impulse_builtin_ctz32(uint32_t x) {
    unsigned long idx;
    return _BitScanForward(&idx, x) ? idx : 32;
}
static inline uint32_t impulse_builtin_popcount64(uint64_t x) {
#if defined(_M_X64) || defined(_M_ARM64)
    return (uint32_t)__popcnt64(x);
#else
    return (uint32_t)(__popcnt((uint32_t)x) + __popcnt((uint32_t)(x >> 32)));
#endif
}
static inline uint32_t impulse_builtin_clz64(uint64_t x) {
#if defined(_M_X64) || defined(_M_ARM64)
    unsigned long idx;
    return _BitScanReverse64(&idx, x) ? (63 - idx) : 64;
#else
    uint32_t high = (uint32_t)(x >> 32);
    if (high != 0) return impulse_builtin_clz32(high);
    return 32 + impulse_builtin_clz32((uint32_t)x);
#endif
}
static inline uint32_t impulse_builtin_ctz64(uint64_t x) {
#if defined(_M_X64) || defined(_M_ARM64)
    unsigned long idx;
    return _BitScanForward64(&idx, x) ? idx : 64;
#else
    uint32_t low = (uint32_t)x;
    if (low != 0) return impulse_builtin_ctz32(low);
    return 32 + impulse_builtin_ctz32((uint32_t)(x >> 32));
#endif
}
#else
static inline uint32_t impulse_builtin_popcount32(uint32_t x) {
    return (uint32_t)__builtin_popcount((unsigned int)x);
}
static inline uint32_t impulse_builtin_clz32(uint32_t x) {
    return x == 0 ? 32 : (uint32_t)__builtin_clz((unsigned int)x);
}
static inline uint32_t impulse_builtin_ctz32(uint32_t x) {
    return x == 0 ? 32 : (uint32_t)__builtin_ctz((unsigned int)x);
}
static inline uint32_t impulse_builtin_popcount64(uint64_t x) {
    return (uint32_t)__builtin_popcountll((unsigned long long)x);
}
static inline uint32_t impulse_builtin_clz64(uint64_t x) {
    return x == 0 ? 64 : (uint32_t)__builtin_clzll((unsigned long long)x);
}
static inline uint32_t impulse_builtin_ctz64(uint64_t x) {
    return x == 0 ? 64 : (uint32_t)__builtin_ctzll((unsigned long long)x);
}
#endif

/**
 * @brief Evaluates a scalar unary math operation on float.
 */
static inline float impulse_math_unary_f32(uint8_t func_id, float x) {
    switch (func_id) {
        case MATH_FUNC_ABS:        return fabsf(x);
        case MATH_FUNC_SQRT:       return sqrtf(x);
        case MATH_FUNC_RSQRT:      return 1.0f / sqrtf(x);
        case MATH_FUNC_CBRT:       return cbrtf(x);
        case MATH_FUNC_EXP:        return expf(x);
        case MATH_FUNC_EXP2:       return exp2f(x);
        case MATH_FUNC_EXP10:      return powf(10.0f, x);
        case MATH_FUNC_EXPM1:      return expm1f(x);
        case MATH_FUNC_LOG:        return logf(x);
        case MATH_FUNC_LOG2:       return log2f(x);
        case MATH_FUNC_LOG10:      return log10f(x);
        case MATH_FUNC_LOG1P:      return log1pf(x);
        case MATH_FUNC_SIN:        return sinf(x);
        case MATH_FUNC_COS:        return cosf(x);
        case MATH_FUNC_TAN:        return tanf(x);
        case MATH_FUNC_ASIN:       return asinf(x);
        case MATH_FUNC_ACOS:       return acosf(x);
        case MATH_FUNC_ATAN:       return atanf(x);
        case MATH_FUNC_SINC:       return (fabsf(x) < 1e-7f) ? 1.0f : (sinf(x) / x);
        case MATH_FUNC_SINH:       return sinhf(x);
        case MATH_FUNC_COSH:       return coshf(x);
        case MATH_FUNC_TANH:       return tanhf(x);
        case MATH_FUNC_ASINH:      return asinhf(x);
        case MATH_FUNC_ACOSH:      return acoshf(x);
        case MATH_FUNC_ATANH:      return atanhf(x);
        case MATH_FUNC_FLOOR:      return floorf(x);
        case MATH_FUNC_CEIL:       return ceilf(x);
        case MATH_FUNC_TRUNC:      return truncf(x);
        case MATH_FUNC_ROUND:      return roundf(x);
        case MATH_FUNC_RELU:       return (x > 0.0f) ? x : 0.0f;
        case MATH_FUNC_LEAKY_RELU: return (x > 0.0f) ? x : (0.01f * x);
        case MATH_FUNC_SIGMOID:    return 1.0f / (1.0f + expf(-x));
        case MATH_FUNC_GELU: {
            const float k0 = 0.7978845608f; // sqrt(2/pi)
            const float k1 = 0.044715f;
            return 0.5f * x * (1.0f + tanhf(k0 * (x + k1 * x * x * x)));
        }
        case MATH_FUNC_SILU:       return x / (1.0f + expf(-x));
        case MATH_FUNC_SOFTPLUS:   return log1pf(expf(x));
        case MATH_FUNC_ERF:        return erff(x);
        case MATH_FUNC_ERFC:       return erfcf(x);
        case MATH_FUNC_LGAMMA:     return lgammaf(x);
        case MATH_FUNC_POPCOUNT:   return (float)impulse_builtin_popcount32((uint32_t)x);
        case MATH_FUNC_CLZ:        return (float)impulse_builtin_clz32((uint32_t)x);
        case MATH_FUNC_CTZ:        return (float)impulse_builtin_ctz32((uint32_t)x);
        case MATH_FUNC_ISNAN:      return isnan(x) ? 1.0f : 0.0f;
        case MATH_FUNC_ISINF:      return isinf(x) ? 1.0f : 0.0f;
        case MATH_FUNC_ISFINITE:   return isfinite(x) ? 1.0f : 0.0f;
        default:                   return x;
    }
}

/**
 * @brief Evaluates a scalar unary math operation on double.
 */
static inline double impulse_math_unary_f64(uint8_t func_id, double x) {
    switch (func_id) {
        case MATH_FUNC_ABS:        return fabs(x);
        case MATH_FUNC_SQRT:       return sqrt(x);
        case MATH_FUNC_RSQRT:      return 1.0 / sqrt(x);
        case MATH_FUNC_CBRT:       return cbrt(x);
        case MATH_FUNC_EXP:        return exp(x);
        case MATH_FUNC_EXP2:       return exp2(x);
        case MATH_FUNC_EXP10:      return pow(10.0, x);
        case MATH_FUNC_EXPM1:      return expm1(x);
        case MATH_FUNC_LOG:        return log(x);
        case MATH_FUNC_LOG2:       return log2(x);
        case MATH_FUNC_LOG10:      return log10(x);
        case MATH_FUNC_LOG1P:      return log1p(x);
        case MATH_FUNC_SIN:        return sin(x);
        case MATH_FUNC_COS:        return cos(x);
        case MATH_FUNC_TAN:        return tan(x);
        case MATH_FUNC_ASIN:       return asin(x);
        case MATH_FUNC_ACOS:       return acos(x);
        case MATH_FUNC_ATAN:       return atan(x);
        case MATH_FUNC_SINC:       return (fabs(x) < 1e-15) ? 1.0 : (sin(x) / x);
        case MATH_FUNC_SINH:       return sinh(x);
        case MATH_FUNC_COSH:       return cosh(x);
        case MATH_FUNC_TANH:       return tanh(x);
        case MATH_FUNC_ASINH:      return asinh(x);
        case MATH_FUNC_ACOSH:      return acosh(x);
        case MATH_FUNC_ATANH:      return atanh(x);
        case MATH_FUNC_FLOOR:      return floor(x);
        case MATH_FUNC_CEIL:       return ceil(x);
        case MATH_FUNC_TRUNC:      return trunc(x);
        case MATH_FUNC_ROUND:      return round(x);
        case MATH_FUNC_RELU:       return (x > 0.0) ? x : 0.0;
        case MATH_FUNC_LEAKY_RELU: return (x > 0.0) ? x : (0.01 * x);
        case MATH_FUNC_SIGMOID:    return 1.0 / (1.0 + exp(-x));
        case MATH_FUNC_GELU: {
            const double k0 = 0.7978845608028654; // sqrt(2/pi)
            const double k1 = 0.044715;
            return 0.5 * x * (1.0 + tanh(k0 * (x + k1 * x * x * x)));
        }
        case MATH_FUNC_SILU:       return x / (1.0 + exp(-x));
        case MATH_FUNC_SOFTPLUS:   return log1p(exp(x));
        case MATH_FUNC_ERF:        return erf(x);
        case MATH_FUNC_ERFC:       return erfc(x);
        case MATH_FUNC_LGAMMA:     return lgamma(x);
        case MATH_FUNC_POPCOUNT:   return (double)impulse_builtin_popcount64((uint64_t)x);
        case MATH_FUNC_CLZ:        return (double)impulse_builtin_clz64((uint64_t)x);
        case MATH_FUNC_CTZ:        return (double)impulse_builtin_ctz64((uint64_t)x);
        case MATH_FUNC_ISNAN:      return isnan(x) ? 1.0 : 0.0;
        case MATH_FUNC_ISINF:      return isinf(x) ? 1.0 : 0.0;
        case MATH_FUNC_ISFINITE:   return isfinite(x) ? 1.0 : 0.0;
        default:                   return x;
    }
}

/**
 * @brief Evaluates a scalar binary math operation on float.
 */
static inline float impulse_math_binary_f32(uint8_t func_id, float x, float y) {
    switch (func_id) {
        case MATH_FUNC_POW:        return powf(x, y);
        case MATH_FUNC_HYPOT:      return hypotf(x, y);
        case MATH_FUNC_ATAN2:      return atan2f(x, y);
        case MATH_FUNC_COPYSIGN:   return copysignf(x, y);
        case MATH_FUNC_FMOD:       return fmodf(x, y);
        case MATH_FUNC_LEAKY_RELU: return (x > 0.0f) ? x : (y * x);
        case MATH_FUNC_SAFE_DIV:   return (y == 0.0f || isnan(y)) ? 0.0f : (x / y);
        case MATH_FUNC_ROTL: {
            uint32_t ux = (uint32_t)x;
            uint32_t s = (uint32_t)y & 31;
            return (float)((ux << s) | (ux >> ((32 - s) & 31)));
        }
        case MATH_FUNC_ROTR: {
            uint32_t ux = (uint32_t)x;
            uint32_t s = (uint32_t)y & 31;
            return (float)((ux >> s) | (ux << ((32 - s) & 31)));
        }
        default:                   return x;
    }
}

/**
 * @brief Evaluates a scalar binary math operation on double.
 */
static inline double impulse_math_binary_f64(uint8_t func_id, double x, double y) {
    switch (func_id) {
        case MATH_FUNC_POW:        return pow(x, y);
        case MATH_FUNC_HYPOT:      return hypot(x, y);
        case MATH_FUNC_ATAN2:      return atan2(x, y);
        case MATH_FUNC_COPYSIGN:   return copysign(x, y);
        case MATH_FUNC_FMOD:       return fmod(x, y);
        case MATH_FUNC_LEAKY_RELU: return (x > 0.0) ? x : (y * x);
        case MATH_FUNC_SAFE_DIV:   return (y == 0.0 || isnan(y)) ? 0.0 : (x / y);
        case MATH_FUNC_ROTL: {
            uint64_t ux = (uint64_t)x;
            uint32_t s = (uint32_t)y & 63;
            return (double)((ux << s) | (ux >> ((64 - s) & 63)));
        }
        case MATH_FUNC_ROTR: {
            uint64_t ux = (uint64_t)x;
            uint32_t s = (uint32_t)y & 63;
            return (double)((ux >> s) | (ux << ((64 - s) & 63)));
        }
        default:                   return x;
    }
}

/**
 * @brief Evaluates a scalar ternary math operation on float.
 */
static inline float impulse_math_ternary_f32(uint8_t func_id, float x, float y, float z) {
    switch (func_id) {
        case MATH_FUNC_LERP:     return x + z * (y - x);
        case MATH_FUNC_CLAMP:    return (x < y) ? y : ((x > z) ? z : x);
        case MATH_FUNC_SAFE_DIV: return (y == 0.0f || isnan(y)) ? z : (x / y);
        default:                 return x;
    }
}

/**
 * @brief Evaluates a scalar ternary math operation on double.
 */
static inline double impulse_math_ternary_f64(uint8_t func_id, double x, double y, double z) {
    switch (func_id) {
        case MATH_FUNC_LERP:     return x + z * (y - x);
        case MATH_FUNC_CLAMP:    return (x < y) ? y : ((x > z) ? z : x);
        case MATH_FUNC_SAFE_DIV: return (y == 0.0 || isnan(y)) ? z : (x / y);
        default:                 return x;
    }
}

/**
 * @brief Vectorized Unary Vector Math Evaluation.
 */
static inline void impulse_vector_math_unary_f32(uint8_t func_id, float* dst, const float* src, size_t count) {
    #pragma omp parallel for schedule(static, 1024) if (count > 2048)
    for (size_t i = 0; i < count; ++i) {
        dst[i] = impulse_math_unary_f32(func_id, src[i]);
    }
}

static inline void impulse_vector_math_unary_f64(uint8_t func_id, double* dst, const double* src, size_t count) {
    #pragma omp parallel for schedule(static, 1024) if (count > 2048)
    for (size_t i = 0; i < count; ++i) {
        dst[i] = impulse_math_unary_f64(func_id, src[i]);
    }
}

/**
 * @brief Vectorized Binary Vector Math Evaluation.
 */
static inline void impulse_vector_math_binary_f32(uint8_t func_id, float* dst, const float* src1, const float* src2, size_t count) {
    #pragma omp parallel for schedule(static, 1024) if (count > 2048)
    for (size_t i = 0; i < count; ++i) {
        dst[i] = impulse_math_binary_f32(func_id, src1[i], src2[i]);
    }
}

static inline void impulse_vector_math_binary_f64(uint8_t func_id, double* dst, const double* src1, const double* src2, size_t count) {
    #pragma omp parallel for schedule(static, 1024) if (count > 2048)
    for (size_t i = 0; i < count; ++i) {
        dst[i] = impulse_math_binary_f64(func_id, src1[i], src2[i]);
    }
}

/**
 * @brief Vectorized Ternary Vector Math Evaluation.
 */
static inline void impulse_vector_math_ternary_f32(uint8_t func_id, float* dst, const float* src1, const float* src2, const float* src3, size_t count) {
    #pragma omp parallel for schedule(static, 1024) if (count > 2048)
    for (size_t i = 0; i < count; ++i) {
        dst[i] = impulse_math_ternary_f32(func_id, src1[i], src2[i], src3[i]);
    }
}

static inline void impulse_vector_math_ternary_f64(uint8_t func_id, double* dst, const double* src1, const double* src2, const double* src3, size_t count) {
    #pragma omp parallel for schedule(static, 1024) if (count > 2048)
    for (size_t i = 0; i < count; ++i) {
        dst[i] = impulse_math_ternary_f64(func_id, src1[i], src2[i], src3[i]);
    }
}

#ifdef __cplusplus
}
#endif

#endif // IMPULSE_MATH_OPS_H
