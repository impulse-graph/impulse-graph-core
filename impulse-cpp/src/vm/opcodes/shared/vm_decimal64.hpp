/**
 * @file vm_decimal64.hpp
 * @brief Normative 128-bit exact fixed-point DECIMAL64 arithmetic for ImpulseVM v0.11.0.
 *
 * Mirrors impulse-ocaml/lib/vm/decimal64.ml bit-for-bit:
 * - All operations are overflow-safe, deterministic, and UB-free.
 * - Half-away-from-zero rounding (ROUND_HALF_UP): for exact |N|/|D| = q rem r,
 *   round up iff r >= |D| - r, preserving the sign of N * D.
 * - Zero heap allocations, zero STL container includes.
 */
#pragma once

#include "p1_helpers.hpp"

#include <cmath>
#include <cstdint>
#include <limits>

namespace impulse::vm {

inline constexpr int64_t kDecPow10[19] = {
    1LL,
    10LL,
    100LL,
    1000LL,
    10000LL,
    100000LL,
    1000000LL,
    10000000LL,
    100000000LL,
    1000000000LL,
    10000000000LL,
    100000000000LL,
    1000000000000LL,
    10000000000000LL,
    100000000000000LL,
    1000000000000000LL,
    10000000000000000LL,
    100000000000000000LL,
    1000000000000000000LL,
};

inline constexpr double kDecPow10F64[19] = {
    1.0,
    1e1,
    1e2,
    1e3,
    1e4,
    1e5,
    1e6,
    1e7,
    1e8,
    1e9,
    1e10,
    1e11,
    1e12,
    1e13,
    1e14,
    1e15,
    1e16,
    1e17,
    1e18,
};

inline uint64_t dec_abs_u64(int64_t x) noexcept {
    return (x < 0) ? (0ULL - static_cast<uint64_t>(x)) : static_cast<uint64_t>(x);
}

inline impulse_vm_status_t dec_signed_i64_of_u64(bool neg_res, uint64_t q, int64_t* out) noexcept {
    if (neg_res) {
        if (q > 0x8000000000000000ULL) return IMPULSE_VM_ERR_ARITHMETIC_OVERFLOW;
        *out = static_cast<int64_t>(0ULL - q);
        return IMPULSE_VM_OK;
    }
    if (q > 0x7FFFFFFFFFFFFFFFULL) return IMPULSE_VM_ERR_ARITHMETIC_OVERFLOW;
    *out = static_cast<int64_t>(q);
    return IMPULSE_VM_OK;
}

inline impulse_vm_status_t dec_round_half_up_to_i64(
    bool neg_res, uint64_t q, uint64_t r, uint64_t ud, int64_t* out
) noexcept {
    const bool round_up = (r >= (ud - r));
    if (round_up) {
        if (q == UINT64_MAX) return IMPULSE_VM_ERR_ARITHMETIC_OVERFLOW;
        q += 1ULL;
    }
    return dec_signed_i64_of_u64(neg_res, q, out);
}

inline void dec_mul_u64_to_u128(uint64_t ua, uint64_t ub, uint64_t* out_hi, uint64_t* out_lo) noexcept {
#if defined(__SIZEOF_INT128__)
    unsigned __int128 prod = static_cast<unsigned __int128>(ua) * static_cast<unsigned __int128>(ub);
    *out_hi = static_cast<uint64_t>(prod >> 64);
    *out_lo = static_cast<uint64_t>(prod);
#else
    const uint64_t a0 = ua & 0xFFFFFFFFULL;
    const uint64_t a1 = ua >> 32;
    const uint64_t b0 = ub & 0xFFFFFFFFULL;
    const uint64_t b1 = ub >> 32;
    const uint64_t p00 = a0 * b0;
    const uint64_t p01 = a0 * b1;
    const uint64_t p10 = a1 * b0;
    const uint64_t p11 = a1 * b1;
    const uint64_t mid = (p00 >> 32) + (p01 & 0xFFFFFFFFULL) + (p10 & 0xFFFFFFFFULL);
    *out_lo = (mid << 32) | (p00 & 0xFFFFFFFFULL);
    *out_hi = p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
#endif
}

inline bool dec_div_rem_u128_by_u64(uint64_t hi, uint64_t lo, uint64_t ud,
                                    uint64_t* out_q, uint64_t* out_r) noexcept {
    if (hi >= ud) return false;
    if (hi == 0ULL) {
        *out_q = lo / ud;
        *out_r = lo % ud;
        return true;
    }
#if defined(__SIZEOF_INT128__)
    unsigned __int128 num = (static_cast<unsigned __int128>(hi) << 64) | static_cast<unsigned __int128>(lo);
    *out_q = static_cast<uint64_t>(num / ud);
    *out_r = static_cast<uint64_t>(num % ud);
    return true;
#else
    uint64_t rem = hi;
    uint64_t quot = 0ULL;
    for (int bit = 63; bit >= 0; --bit) {
        const bool carry = (rem >> 63) != 0ULL;
        const uint64_t lo_bit = (lo >> bit) & 1ULL;
        rem = (rem << 1) | lo_bit;
        if (carry || rem >= ud) {
            rem -= ud;
            quot |= (1ULL << bit);
        }
    }
    *out_q = quot;
    *out_r = rem;
    return true;
#endif
}

inline impulse_vm_status_t dec_add_chk(int64_t a, int64_t b, int64_t* out) noexcept {
#if defined(__has_builtin)
#  if __has_builtin(__builtin_add_overflow)
    if (__builtin_add_overflow(a, b, out)) return IMPULSE_VM_ERR_ARITHMETIC_OVERFLOW;
    return IMPULSE_VM_OK;
#  endif
#elif defined(__GNUC__) && (__GNUC__ >= 5)
    if (__builtin_add_overflow(a, b, out)) return IMPULSE_VM_ERR_ARITHMETIC_OVERFLOW;
    return IMPULSE_VM_OK;
#endif
    if ((b > 0 && a > (std::numeric_limits<int64_t>::max)() - b) ||
        (b < 0 && a < (std::numeric_limits<int64_t>::min)() - b)) {
        return IMPULSE_VM_ERR_ARITHMETIC_OVERFLOW;
    }
    *out = static_cast<int64_t>(static_cast<uint64_t>(a) + static_cast<uint64_t>(b));
    return IMPULSE_VM_OK;
}

inline impulse_vm_status_t dec_sub_chk(int64_t a, int64_t b, int64_t* out) noexcept {
#if defined(__has_builtin)
#  if __has_builtin(__builtin_sub_overflow)
    if (__builtin_sub_overflow(a, b, out)) return IMPULSE_VM_ERR_ARITHMETIC_OVERFLOW;
    return IMPULSE_VM_OK;
#  endif
#elif defined(__GNUC__) && (__GNUC__ >= 5)
    if (__builtin_sub_overflow(a, b, out)) return IMPULSE_VM_ERR_ARITHMETIC_OVERFLOW;
    return IMPULSE_VM_OK;
#endif
    if ((b < 0 && a > (std::numeric_limits<int64_t>::max)() + b) ||
        (b > 0 && a < (std::numeric_limits<int64_t>::min)() + b)) {
        return IMPULSE_VM_ERR_ARITHMETIC_OVERFLOW;
    }
    *out = static_cast<int64_t>(static_cast<uint64_t>(a) - static_cast<uint64_t>(b));
    return IMPULSE_VM_OK;
}

inline impulse_vm_status_t dec_mul_chk(int64_t a, int64_t b, int64_t* out) noexcept {
#if defined(__has_builtin)
#  if __has_builtin(__builtin_mul_overflow)
    if (__builtin_mul_overflow(a, b, out)) return IMPULSE_VM_ERR_ARITHMETIC_OVERFLOW;
    return IMPULSE_VM_OK;
#  endif
#elif defined(__GNUC__) && (__GNUC__ >= 5)
    if (__builtin_mul_overflow(a, b, out)) return IMPULSE_VM_ERR_ARITHMETIC_OVERFLOW;
    return IMPULSE_VM_OK;
#endif
    if (a == 0 || b == 0) { *out = 0; return IMPULSE_VM_OK; }
    const bool neg_res = (a < 0) != (b < 0);
    uint64_t hi = 0ULL, lo = 0ULL;
    dec_mul_u64_to_u128(dec_abs_u64(a), dec_abs_u64(b), &hi, &lo);
    if (hi != 0ULL) return IMPULSE_VM_ERR_ARITHMETIC_OVERFLOW;
    return dec_signed_i64_of_u64(neg_res, lo, out);
}

inline impulse_vm_status_t dec_mul_div_round(int64_t a, int64_t b, int64_t d, int64_t* out) noexcept {
    if (d == 0) return IMPULSE_VM_ERR_DIV_BY_ZERO;
    if (a == 0 || b == 0) { *out = 0; return IMPULSE_VM_OK; }
    const bool neg_res = ((a < 0) != (b < 0)) != (d < 0);
    uint64_t hi = 0ULL, lo = 0ULL;
    dec_mul_u64_to_u128(dec_abs_u64(a), dec_abs_u64(b), &hi, &lo);
    const uint64_t ud = dec_abs_u64(d);
    uint64_t q = 0ULL, r = 0ULL;
    if (!dec_div_rem_u128_by_u64(hi, lo, ud, &q, &r)) {
        return IMPULSE_VM_ERR_ARITHMETIC_OVERFLOW;
    }
    return dec_round_half_up_to_i64(neg_res, q, r, ud, out);
}

inline impulse_vm_status_t dec_div_round(int64_t a, int64_t d, int64_t* out) noexcept {
    if (d == 0) return IMPULSE_VM_ERR_DIV_BY_ZERO;
    if (a == 0) { *out = 0; return IMPULSE_VM_OK; }
    const bool neg_res = (a < 0) != (d < 0);
    const uint64_t ua = dec_abs_u64(a);
    const uint64_t ud = dec_abs_u64(d);
    const uint64_t q = ua / ud;
    const uint64_t r = ua % ud;
    return dec_round_half_up_to_i64(neg_res, q, r, ud, out);
}

inline impulse_vm_status_t dec_div_safe(int64_t a, int64_t d, int64_t* out, bool* out_valid) noexcept {
    if (d == 0) {
        *out = 0;
        *out_valid = false;
        return IMPULSE_VM_OK;
    }
    impulse_vm_status_t st = dec_div_round(a, d, out);
    *out_valid = (st == IMPULSE_VM_OK);
    return st;
}

inline impulse_vm_status_t dec_rescale(int64_t a, uint16_t delta_s, uint16_t dir, int64_t* out) noexcept {
    if (delta_s > 18u || (dir != 0u && dir != 1u)) return IMPULSE_VM_ERR_INVALID_OPERAND;
    if (dir == 0u) return dec_mul_chk(a, kDecPow10[delta_s], out);
    return dec_div_round(a, kDecPow10[delta_s], out);
}

inline impulse_vm_status_t dec_bound_chk(int64_t a, uint16_t prec, int64_t* out) noexcept {
    if (prec < 1u || prec > 18u) return IMPULSE_VM_ERR_INVALID_OPERAND;
    const uint64_t limit = static_cast<uint64_t>(kDecPow10[prec] - 1LL);
    if (dec_abs_u64(a) > limit) return IMPULSE_VM_ERR_ARITHMETIC_OVERFLOW;
    *out = a;
    return IMPULSE_VM_OK;
}

inline impulse_vm_status_t dec_abs_chk(int64_t a, int64_t* out) noexcept {
    if (a == (std::numeric_limits<int64_t>::min)()) return IMPULSE_VM_ERR_ARITHMETIC_OVERFLOW;
    *out = (a < 0) ? -a : a;
    return IMPULSE_VM_OK;
}

inline impulse_vm_status_t dec_neg_chk(int64_t a, int64_t* out) noexcept {
    if (a == (std::numeric_limits<int64_t>::min)()) return IMPULSE_VM_ERR_ARITHMETIC_OVERFLOW;
    *out = -a;
    return IMPULSE_VM_OK;
}

inline impulse_vm_status_t dec_f64_to_dec(
    double x, uint16_t scale, uint16_t prec, int64_t* out, bool* out_valid
) noexcept {
    if (scale > 18u || prec < 1u || prec > 18u) return IMPULSE_VM_ERR_INVALID_OPERAND;
    if (std::isnan(x)) {
        *out = 0;
        *out_valid = false;
        return IMPULSE_VM_OK;
    }
    if (std::isinf(x)) return IMPULSE_VM_ERR_ARITHMETIC_OVERFLOW;
    const double scaled = x * kDecPow10F64[scale];
    if (std::isnan(scaled) || std::isinf(scaled)) return IMPULSE_VM_ERR_ARITHMETIC_OVERFLOW;
    const double r = p1_round_half_away(scaled);
    if (r >= 9223372036854775808.0 || r <= -9223372036854775808.0) {
        return IMPULSE_VM_ERR_ARITHMETIC_OVERFLOW;
    }
    const int64_t v = static_cast<int64_t>(r);
    impulse_vm_status_t st = dec_bound_chk(v, prec, out);
    if (st != IMPULSE_VM_OK) return st;
    *out_valid = true;
    return IMPULSE_VM_OK;
}

inline impulse_vm_status_t dec_to_f64(int64_t a, uint16_t scale, double* out) noexcept {
    if (scale > 18u) return IMPULSE_VM_ERR_INVALID_OPERAND;
    *out = static_cast<double>(a) / kDecPow10F64[scale];
    return IMPULSE_VM_OK;
}

inline impulse_vm_status_t dec_round_scale(int64_t a, uint16_t delta_s, int64_t* out) noexcept {
    if (delta_s > 18u) return IMPULSE_VM_ERR_INVALID_OPERAND;
    return dec_div_round(a, kDecPow10[delta_s], out);
}

inline impulse_vm_status_t dec_trunc_scale(int64_t a, uint16_t delta_s, int64_t* out) noexcept {
    if (delta_s > 18u) return IMPULSE_VM_ERR_INVALID_OPERAND;
    *out = a / kDecPow10[delta_s];
    return IMPULSE_VM_OK;
}

inline impulse_vm_status_t dec_floor_scale(int64_t a, uint16_t delta_s, int64_t* out) noexcept {
    if (delta_s > 18u) return IMPULSE_VM_ERR_INVALID_OPERAND;
    const int64_t d = kDecPow10[delta_s];
    const int64_t q = a / d;
    const int64_t r = a % d;
    *out = (r < 0) ? (q - 1LL) : q;
    return IMPULSE_VM_OK;
}

inline impulse_vm_status_t dec_ceil_scale(int64_t a, uint16_t delta_s, int64_t* out) noexcept {
    if (delta_s > 18u) return IMPULSE_VM_ERR_INVALID_OPERAND;
    const int64_t d = kDecPow10[delta_s];
    const int64_t q = a / d;
    const int64_t r = a % d;
    *out = (r > 0) ? (q + 1LL) : q;
    return IMPULSE_VM_OK;
}

/** Exact signed 128-bit accumulator for VEC_REDUCE.sum_i64_chk (deterministic across max_dop). */
struct VmSum128 {
#if defined(__SIZEOF_INT128__)
    __int128 sum = 0;
#else
    int64_t  hi = 0;
    uint64_t lo = 0;
#endif
    uint64_t cnt = 0;

    inline void add(int64_t x) noexcept {
#if defined(__SIZEOF_INT128__)
        sum += static_cast<__int128>(x);
#else
        const int64_t x_hi = (x < 0) ? -1LL : 0LL;
        const uint64_t ux = static_cast<uint64_t>(x);
        const uint64_t new_lo = lo + ux;
        const int64_t carry = (new_lo < lo) ? 1LL : 0LL;
        hi = static_cast<int64_t>(static_cast<uint64_t>(hi) + static_cast<uint64_t>(x_hi) + static_cast<uint64_t>(carry));
        lo = new_lo;
#endif
        ++cnt;
    }

    inline impulse_vm_status_t finalize(int64_t* out, bool* out_valid) const noexcept {
        if (cnt == 0ULL) {
            *out = 0;
            *out_valid = false;
            return IMPULSE_VM_OK;
        }
#if defined(__SIZEOF_INT128__)
        if (sum < static_cast<__int128>((std::numeric_limits<int64_t>::min)()) ||
            sum > static_cast<__int128>((std::numeric_limits<int64_t>::max)())) {
            return IMPULSE_VM_ERR_ARITHMETIC_OVERFLOW;
        }
        *out = static_cast<int64_t>(sum);
        *out_valid = true;
        return IMPULSE_VM_OK;
#else
        const int64_t slo = static_cast<int64_t>(lo);
        const bool fits = (slo >= 0 && hi == 0) || (slo < 0 && hi == -1LL);
        if (!fits) return IMPULSE_VM_ERR_ARITHMETIC_OVERFLOW;
        *out = slo;
        *out_valid = true;
        return IMPULSE_VM_OK;
#endif
    }
};

} // namespace impulse::vm
