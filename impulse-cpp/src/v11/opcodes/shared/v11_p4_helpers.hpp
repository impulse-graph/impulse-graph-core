/**
 * @file v11_p4_helpers.hpp
 * @brief Shared Pillar 4 (0x40..0x57) zero-malloc helpers, math approximations,
 *        gather/copy utilities, and output slot management.
 */
#pragma once

#include "v11_p3_helpers.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace impulse::vm::v11 {

inline constexpr double kP4Sqrt2 = 1.41421356237309504880168872420969807857;
inline constexpr double kP4Ln2   = 0.69314718055994530941723212145817656808;
inline constexpr double kP4Pi    = 3.14159265358979323846264338327950288420;

inline const V11Frontier* p4_get_mask_frontier(impulse_vm_workspace_v11_t& ws,
                                               uint8_t flags,
                                               uint8_t s3) noexcept {
    if ((flags & FLAG_MASKED_S3) == 0u) return nullptr;
    if (!ws.is_f_init(s3) || ws.f_regs[s3].multi_lane) {
        ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH;
        return nullptr;
    }
    return &ws.f_regs[s3];
}

inline bool p4_lane_active(const V11Frontier* mask, uint32_t i) noexcept {
    return mask == nullptr || mask->contains_1bit(i);
}

inline bool p4_is_numeric_vec(const V11Vector* v) noexcept {
    return v != nullptr && (v->elem_tag == kVecTagF64 || v->elem_tag == kVecTagI64 || v->elem_tag == kVecTagU64);
}

inline double p4_vec_to_f64(const V11Vector& v, uint32_t i) noexcept {
    if (v.elem_tag == kVecTagF64) return v.f64_data[i];
    return static_cast<double>(v.i64_data[i]);
}

inline bool p4_is_scalar_f64_valid(const impulse_vm_workspace_v11_t& ws, uint8_t reg) noexcept {
    if (!ws.is_r_valid(reg)) return false;
    return !std::isnan(p1_bits_to_f64(ws.read_r_raw(reg)));
}

inline double p4_read_scalar_f64(const impulse_vm_workspace_v11_t& ws, uint8_t reg) noexcept {
    return p1_bits_to_f64(ws.read_r_raw(reg));
}

inline uint64_t* p4_alloc_valid_all_true(V11Arena& arena, uint32_t len) noexcept {
    size_t words = (len > 0u) ? ((static_cast<size_t>(len) + 63u) >> 6) : 1u;
    uint64_t* bits = arena.alloc_array<uint64_t>(words, 8);
    if (!bits) return nullptr;
    if (len == 0u) {
        bits[0] = 0ULL;
        return bits;
    }
    std::memset(bits, 0xFF, words * sizeof(uint64_t));
    uint32_t rem = len & 63u;
    if (rem != 0u) {
        bits[words - 1u] = (1ULL << rem) - 1ULL;
    }
    return bits;
}

inline V11Vector* p4_shallow_copy_vec(V11Arena& arena, const V11Vector& col) noexcept {
    auto* ov = static_cast<V11Vector*>(arena.alloc(sizeof(V11Vector), alignof(V11Vector)));
    if (!ov) return nullptr;
    *ov = col;
    return ov;
}

inline bool p4_eval_cmp_op(uint8_t subop, int cmp) noexcept {
    switch (subop) {
        case 0: return cmp == 0;
        case 1: return cmp != 0;
        case 2: return cmp <  0;
        case 3: return cmp <= 0;
        case 4: return cmp >  0;
        case 5: return cmp >= 0;
        default: return false;
    }
}

inline bool p4_eval_cmp_f64(uint8_t subop, double a, double b) noexcept {
    switch (subop) {
        case 0: return a == b;
        case 1: return a != b;
        case 2: return a <  b;
        case 3: return a <= b;
        case 4: return a >  b;
        case 5: return a >= b;
        default: return false;
    }
}

inline int p4_str_cmp(const V11StrSlice& a, const V11StrSlice& b) noexcept {
    uint32_t m = (a.len < b.len) ? a.len : b.len;
    if (m > 0u) {
        int c = std::memcmp(a.ptr, b.ptr, m);
        if (c != 0) return c;
    }
    if (a.len < b.len) return -1;
    if (a.len > b.len) return 1;
    return 0;
}

inline bool p4_str_eq(const V11StrSlice& a, const V11StrSlice& b) noexcept {
    return a.len == b.len && (a.len == 0u || std::memcmp(a.ptr, b.ptr, a.len) == 0);
}

inline bool p4_str_starts_with(const V11StrSlice& a, const V11StrSlice& b) noexcept {
    return b.len <= a.len && (b.len == 0u || std::memcmp(a.ptr, b.ptr, b.len) == 0);
}

inline bool p4_str_ends_with(const V11StrSlice& a, const V11StrSlice& b) noexcept {
    return b.len <= a.len && (b.len == 0u || std::memcmp(a.ptr + (a.len - b.len), b.ptr, b.len) == 0);
}

inline bool p4_str_contains(const V11StrSlice& a, const V11StrSlice& b) noexcept {
    if (b.len == 0u) return true;
    if (b.len > a.len) return false;
    for (uint32_t i = 0; i + b.len <= a.len; ++i) {
        if (std::memcmp(a.ptr + i, b.ptr, b.len) == 0) return true;
    }
    return false;
}

inline double p4_round_half_away_f64(double x) noexcept {
    if (!std::isfinite(x) || x == 0.0) return x;
    if (x > 0.0) {
        double f = std::floor(x);
        return (x - f >= 0.5) ? (f + 1.0) : f;
    } else {
        double c = std::ceil(x);
        return (c - x >= 0.5) ? (c - 1.0) : c;
    }
}

inline double p4_erf_approx(double x) noexcept {
    if (std::isnan(x)) return std::numeric_limits<double>::quiet_NaN();
    if (std::isinf(x)) return x > 0.0 ? 1.0 : -1.0;
    if (x == 0.0) return x;
    double sign = x < 0.0 ? -1.0 : 1.0;
    double ax = std::fabs(x);
    double t = 1.0 / (1.0 + 0.3275911 * ax);
    double poly = t * (0.254829592
            + t * (-0.284496736
            + t * (1.421413741
            + t * (-1.453152027
            + t * 1.061405429))));
    return sign * (1.0 - poly * std::exp(-(ax * ax)));
}

inline double p4_erfc_approx(double x) noexcept {
    if (std::isnan(x)) return std::numeric_limits<double>::quiet_NaN();
    return 1.0 - p4_erf_approx(x);
}

inline double p4_lgamma_approx(double x) noexcept {
    if (std::isnan(x)) return std::numeric_limits<double>::quiet_NaN();
    if (x <= 0.0 && x == std::floor(x)) return std::numeric_limits<double>::infinity();
    if (std::isinf(x)) return std::numeric_limits<double>::infinity();
    static constexpr double c[9] = {
        0.99999999999980993, 676.5203681218851, -1259.1392167224028,
        771.32342877765313, -176.61502916214059, 12.507343278686905,
        -0.13857109526572012, 9.9843695780195716e-6, 1.5056327351493116e-7
    };
    if (x < 0.5) {
        return std::log(kP4Pi / std::fabs(std::sin(kP4Pi * x))) - p4_lgamma_approx(1.0 - x);
    }
    double z = x - 1.0;
    double acc = c[0];
    for (int i = 1; i < 9; ++i) acc += c[i] / (z + i);
    double t = z + 7.5;
    return 0.5 * std::log(2.0 * kP4Pi) + (z + 0.5) * std::log(t) - t + std::log(acc);
}

inline void p4_write_f64_scalar(impulse_vm_workspace_v11_t& ws, uint8_t dst, bool valid, double val) noexcept {
    if (!valid || std::isnan(val)) {
        ws.write_r_null(dst);
        ws.set_zf(true);
        ws.set_nf(true);
    } else {
        ws.write_r(dst, static_cast<uint8_t>(ScalarType::SCALAR_F64), p1_f64_to_bits(val));
        ws.set_zf(val == 0.0);
        ws.set_nf(false);
    }
}

inline void p4_write_vec_elem_to_scalar(impulse_vm_workspace_v11_t& ws,
                                        uint8_t dst,
                                        const V11Vector& col,
                                        uint32_t idx) noexcept {
    if (!col.is_valid_at(idx)) {
        ws.write_r_null(dst);
        ws.set_zf(true);
        ws.set_nf(true);
        return;
    }
    switch (col.elem_tag) {
        case kVecTagI64: {
            uint64_t v = static_cast<uint64_t>(col.i64_data[idx]);
            ws.write_r(dst, static_cast<uint8_t>(ScalarType::SCALAR_I64), v);
            ws.set_zf(v == 0ULL);
            ws.set_nf(false);
            break;
        }
        case kVecTagU64: {
            uint64_t v = static_cast<uint64_t>(col.i64_data[idx]);
            ws.write_r(dst, static_cast<uint8_t>(ScalarType::SCALAR_U64), v);
            ws.set_zf(v == 0ULL);
            ws.set_nf(false);
            break;
        }
        case kVecTagF64: {
            p4_write_f64_scalar(ws, dst, true, col.f64_data[idx]);
            break;
        }
        case kVecTagBool3vl: {
            uint64_t b = col.bool_data_at(idx) ? 1ULL : 0ULL;
            ws.write_r(dst, static_cast<uint8_t>(ScalarType::SCALAR_BOOL_3VL), b);
            ws.set_zf(b == 0ULL);
            ws.set_nf(false);
            break;
        }
        case kVecTagStr: {
            V11StrSlice s = col.str_data[idx];
            if (!s.valid) {
                ws.write_r_null(dst);
                ws.set_zf(true);
                ws.set_nf(true);
            } else {
                uint64_t h = ws.intern_scalar_str(s.ptr, s.len);
                if (h == 0ULL) {
                    ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW;
                    return;
                }
                ws.write_r(dst, static_cast<uint8_t>(ScalarType::SCALAR_STR_HANDLE), h);
                ws.set_zf(s.len == 0u);
                ws.set_nf(false);
            }
            break;
        }
        default:
            ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH;
            break;
    }
}

IMPULSE_V11_NOINLINE inline V11Vector* p4_gather_from_vec(
    V11Arena& arena,
    const V11Vector& col,
    const uint32_t* indices,
    const uint8_t* idx_valid,
    uint32_t n,
    const V11Frontier* mask
) noexcept {
    size_t words = (n > 0u) ? ((static_cast<size_t>(n) + 63u) >> 6) : 1u;
    auto* ov = static_cast<V11Vector*>(arena.alloc(sizeof(V11Vector), alignof(V11Vector)));
    if (!ov) return nullptr;
    new (ov) V11Vector();
    ov->elem_tag = col.elem_tag;
    ov->length = n;

    switch (col.elem_tag) {
        case kVecTagI64:
        case kVecTagU64: {
            int64_t* out = (n > 0u) ? arena.alloc_array<int64_t>(n, 8) : nullptr;
            uint64_t* valid = arena.alloc_array<uint64_t>(words, 8);
            if ((n > 0u && !out) || !valid) return nullptr;
            if (n > 0u) std::memset(out, 0, static_cast<size_t>(n) * sizeof(int64_t));
            std::memset(valid, 0, words * sizeof(uint64_t));
            for (uint32_t i = 0; i < n; ++i) {
                if (!p4_lane_active(mask, i)) continue;
                if (idx_valid && !idx_valid[i]) continue;
                uint32_t idx = indices[i];
                if (idx < col.length && col.is_valid_at(idx)) {
                    out[i] = col.i64_data[idx];
                    valid[i >> 6] |= (1ULL << (i & 63u));
                }
            }
            ov->i64_data = out;
            ov->valid_bits = valid;
            return ov;
        }
        case kVecTagF64: {
            double* out = (n > 0u) ? arena.alloc_array<double>(n, 8) : nullptr;
            uint64_t* valid = arena.alloc_array<uint64_t>(words, 8);
            if ((n > 0u && !out) || !valid) return nullptr;
            if (n > 0u) std::memset(out, 0, static_cast<size_t>(n) * sizeof(double));
            std::memset(valid, 0, words * sizeof(uint64_t));
            for (uint32_t i = 0; i < n; ++i) {
                if (!p4_lane_active(mask, i)) continue;
                if (idx_valid && !idx_valid[i]) continue;
                uint32_t idx = indices[i];
                if (idx < col.length && col.is_valid_at(idx)) {
                    out[i] = col.f64_data[idx];
                    valid[i >> 6] |= (1ULL << (i & 63u));
                }
            }
            ov->f64_data = out;
            ov->valid_bits = valid;
            return ov;
        }
        case kVecTagBool3vl: {
            uint64_t* valid = arena.alloc_array<uint64_t>(words, 8);
            uint64_t* data  = arena.alloc_array<uint64_t>(words, 8);
            if (!valid || !data) return nullptr;
            std::memset(valid, 0, words * sizeof(uint64_t));
            std::memset(data, 0, words * sizeof(uint64_t));
            for (uint32_t i = 0; i < n; ++i) {
                if (!p4_lane_active(mask, i)) continue;
                if (idx_valid && !idx_valid[i]) continue;
                uint32_t idx = indices[i];
                if (idx < col.length && col.is_valid_at(idx)) {
                    uint64_t bit = 1ULL << (i & 63u);
                    valid[i >> 6] |= bit;
                    if (col.bool_data_at(idx)) data[i >> 6] |= bit;
                }
            }
            ov->valid_bits = valid;
            ov->bool_data_bits = data;
            return ov;
        }
        case kVecTagStr: {
            V11StrSlice* out = (n > 0u) ? arena.alloc_array<V11StrSlice>(n, 8) : nullptr;
            if (n > 0u && !out) return nullptr;
            for (uint32_t i = 0; i < n; ++i) {
                out[i] = {nullptr, 0u, 0u};
                if (!p4_lane_active(mask, i)) continue;
                if (idx_valid && !idx_valid[i]) continue;
                uint32_t idx = indices[i];
                if (idx < col.length && col.is_valid_at(idx)) {
                    out[i] = col.str_data[idx];
                }
            }
            ov->str_data = out;
            return ov;
        }
        case kVecTagEmb: {
            double** out = (n > 0u) ? arena.alloc_array<double*>(n, 8) : nullptr;
            if (n > 0u && !out) return nullptr;
            for (uint32_t i = 0; i < n; ++i) {
                out[i] = nullptr;
                if (!p4_lane_active(mask, i)) continue;
                if (idx_valid && !idx_valid[i]) continue;
                uint32_t idx = indices[i];
                if (idx < col.length && col.is_valid_at(idx)) {
                    out[i] = col.emb_rows[idx];
                }
            }
            ov->emb_dim = col.emb_dim;
            ov->emb_rows = out;
            return ov;
        }
        default: {
            ov->elem_tag = kVecTagI64;
            ov->length = 0u;
            ov->valid_bits = arena.alloc_array<uint64_t>(1, 8);
            if (!ov->valid_bits) return nullptr;
            ov->valid_bits[0] = 0ULL;
            return ov;
        }
    }
}

inline const V11KeyCatalog* p4_find_key_catalog(const V11SnapshotView* snap, uint16_t dom_id) noexcept {
    if (!snap) return nullptr;
    for (uint32_t i = 0; i < snap->key_catalog_count; ++i) {
        if (snap->key_catalogs[i].dom_id == dom_id) return &snap->key_catalogs[i];
    }
    return nullptr;
}

inline int64_t p4_lookup_key(const V11KeyCatalog* kc, const V11StrSlice& key) noexcept {
    if (!kc || !key.valid) return -1;
    for (uint32_t i = 0; i < kc->count; ++i) {
        if (kc->keys[i].valid && p4_str_eq(kc->keys[i], key)) return static_cast<int64_t>(i);
    }
    return -1;
}

inline bool p4_put_output(impulse_vm_workspace_v11_t& ws, uint16_t slot, V11Vector* vec) noexcept {
    for (uint32_t i = 0; i < ws.output_count; ++i) {
        if (ws.outputs[i].slot == slot) {
            ws.outputs[i].vec = vec;
            return true;
        }
    }
    if (ws.output_count >= kMaxOutputs) return false;
    ws.outputs[ws.output_count++] = {slot, vec};
    return true;
}

inline bool p4_i128_mul_div(int64_t a, int64_t b, int64_t d, int64_t* out) noexcept {
    if (d == 0) return false;
#if defined(__SIZEOF_INT128__)
    __int128 prod = static_cast<__int128>(a) * static_cast<__int128>(b);
    __int128 q = prod / static_cast<__int128>(d);
    if (q < static_cast<__int128>(INT64_MIN) || q > static_cast<__int128>(INT64_MAX)) return false;
    *out = static_cast<int64_t>(q);
    return true;
#else
    bool neg = (a < 0) ^ (b < 0) ^ (d < 0);
    uint64_t ua = (a < 0) ? (0ULL - static_cast<uint64_t>(a)) : static_cast<uint64_t>(a);
    uint64_t ub = (b < 0) ? (0ULL - static_cast<uint64_t>(b)) : static_cast<uint64_t>(b);
    uint64_t ud = (d < 0) ? (0ULL - static_cast<uint64_t>(d)) : static_cast<uint64_t>(d);
    // Exact 64x64 -> 128-bit product (hi:lo)
    uint64_t a_lo = ua & 0xFFFFFFFFULL, a_hi = ua >> 32;
    uint64_t b_lo = ub & 0xFFFFFFFFULL, b_hi = ub >> 32;
    uint64_t p0 = a_lo * b_lo;
    uint64_t p1 = a_lo * b_hi;
    uint64_t p2 = a_hi * b_lo;
    uint64_t p3 = a_hi * b_hi;
    uint64_t mid = (p0 >> 32) + (p1 & 0xFFFFFFFFULL) + (p2 & 0xFFFFFFFFULL);
    uint64_t hi = p3 + (p1 >> 32) + (p2 >> 32) + (mid >> 32);
    uint64_t lo = (p0 & 0xFFFFFFFFULL) | (mid << 32);
    if (hi >= ud) return false; // quotient >= 2^64
    // Bitwise 128 / 64 unsigned division
    uint64_t q = 0ULL, rem = hi;
    for (int bit = 63; bit >= 0; --bit) {
        uint64_t carry = rem >> 63;
        rem = (rem << 1) | ((lo >> bit) & 1ULL);
        if (carry || rem >= ud) {
            rem -= ud;
            q |= (1ULL << bit);
        }
    }
    if (neg) {
        if (q > (1ULL << 63)) return false;
        *out = static_cast<int64_t>(0ULL - q);
    } else {
        if (q > static_cast<uint64_t>(INT64_MAX)) return false;
        *out = static_cast<int64_t>(q);
    }
    return true;
#endif
}

inline uint64_t p4_bswap64(uint64_t x) noexcept {
#if defined(__GNUC__) || defined(__clang__)
    return __builtin_bswap64(x);
#else
    return ((x & 0x00000000000000FFULL) << 56) |
           ((x & 0x000000000000FF00ULL) << 40) |
           ((x & 0x0000000000FF0000ULL) << 24) |
           ((x & 0x00000000FF000000ULL) << 8)  |
           ((x & 0x000000FF00000000ULL) >> 8)  |
           ((x & 0x0000FF0000000000ULL) >> 24) |
           ((x & 0x00FF000000000000ULL) >> 40) |
           ((x & 0xFF00000000000000ULL) >> 56);
#endif
}

} // namespace impulse::vm::v11
