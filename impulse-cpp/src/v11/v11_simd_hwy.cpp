#include "v11/opcodes/shared/v11_simd_kernels.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

#undef HWY_TARGET_INCLUDE
#define HWY_TARGET_INCLUDE "v11/v11_simd_hwy.cpp"
#include "hwy/foreach_target.h"
#include "hwy/highway.h"

HWY_BEFORE_NAMESPACE();
namespace impulse::vm::v11::simd {
namespace HWY_NAMESPACE {

namespace hn = hwy::HWY_NAMESPACE;

void F64ArithVec(uint8_t subop,
                 const double* HWY_RESTRICT a,
                 const double* HWY_RESTRICT b,
                 double* HWY_RESTRICT out,
                 uint32_t n) {
    const hn::ScalableTag<double> d;
    const size_t N = hn::Lanes(d);
    size_t i = 0;
    const size_t len = static_cast<size_t>(n);
    const double qnan = std::numeric_limits<double>::quiet_NaN();
    switch (subop) {
        case 0u:
            for (; i + 4 * N <= len; i += 4 * N) {
                hn::StoreU(hn::Add(hn::LoadU(d, a + i + 0 * N), hn::LoadU(d, b + i + 0 * N)), d, out + i + 0 * N);
                hn::StoreU(hn::Add(hn::LoadU(d, a + i + 1 * N), hn::LoadU(d, b + i + 1 * N)), d, out + i + 1 * N);
                hn::StoreU(hn::Add(hn::LoadU(d, a + i + 2 * N), hn::LoadU(d, b + i + 2 * N)), d, out + i + 2 * N);
                hn::StoreU(hn::Add(hn::LoadU(d, a + i + 3 * N), hn::LoadU(d, b + i + 3 * N)), d, out + i + 3 * N);
            }
            for (; i + N <= len; i += N) hn::StoreU(hn::Add(hn::LoadU(d, a + i), hn::LoadU(d, b + i)), d, out + i);
            for (; i < len; ++i) out[i] = a[i] + b[i];
            return;
        case 1u:
            for (; i + 4 * N <= len; i += 4 * N) {
                hn::StoreU(hn::Sub(hn::LoadU(d, a + i + 0 * N), hn::LoadU(d, b + i + 0 * N)), d, out + i + 0 * N);
                hn::StoreU(hn::Sub(hn::LoadU(d, a + i + 1 * N), hn::LoadU(d, b + i + 1 * N)), d, out + i + 1 * N);
                hn::StoreU(hn::Sub(hn::LoadU(d, a + i + 2 * N), hn::LoadU(d, b + i + 2 * N)), d, out + i + 2 * N);
                hn::StoreU(hn::Sub(hn::LoadU(d, a + i + 3 * N), hn::LoadU(d, b + i + 3 * N)), d, out + i + 3 * N);
            }
            for (; i + N <= len; i += N) hn::StoreU(hn::Sub(hn::LoadU(d, a + i), hn::LoadU(d, b + i)), d, out + i);
            for (; i < len; ++i) out[i] = a[i] - b[i];
            return;
        case 2u:
            for (; i + 4 * N <= len; i += 4 * N) {
                hn::StoreU(hn::Mul(hn::LoadU(d, a + i + 0 * N), hn::LoadU(d, b + i + 0 * N)), d, out + i + 0 * N);
                hn::StoreU(hn::Mul(hn::LoadU(d, a + i + 1 * N), hn::LoadU(d, b + i + 1 * N)), d, out + i + 1 * N);
                hn::StoreU(hn::Mul(hn::LoadU(d, a + i + 2 * N), hn::LoadU(d, b + i + 2 * N)), d, out + i + 2 * N);
                hn::StoreU(hn::Mul(hn::LoadU(d, a + i + 3 * N), hn::LoadU(d, b + i + 3 * N)), d, out + i + 3 * N);
            }
            for (; i + N <= len; i += N) hn::StoreU(hn::Mul(hn::LoadU(d, a + i), hn::LoadU(d, b + i)), d, out + i);
            for (; i < len; ++i) out[i] = a[i] * b[i];
            return;
        case 3u: {
            const auto vzero = hn::Zero(d);
            const auto vnan  = hn::Set(d, qnan);
            for (; i + N <= len; i += N) {
                const auto va = hn::LoadU(d, a + i);
                const auto vb = hn::LoadU(d, b + i);
                const auto is_z = hn::Eq(vb, vzero);
                hn::StoreU(hn::IfThenElse(is_z, vnan, hn::Div(va, vb)), d, out + i);
            }
            for (; i < len; ++i) out[i] = (b[i] == 0.0) ? qnan : (a[i] / b[i]);
            return;
        }
        default: break;
    }
}

void F64ArithScalar(uint8_t subop,
                    const double* HWY_RESTRICT a,
                    double b_sc,
                    double* HWY_RESTRICT out,
                    uint32_t n) {
    const hn::ScalableTag<double> d;
    const size_t N = hn::Lanes(d);
    const auto vb = hn::Set(d, b_sc);
    size_t i = 0;
    const size_t len = static_cast<size_t>(n);
    const double qnan = std::numeric_limits<double>::quiet_NaN();
    switch (subop) {
        case 0u:
            for (; i + 4 * N <= len; i += 4 * N) {
                hn::StoreU(hn::Add(hn::LoadU(d, a + i + 0 * N), vb), d, out + i + 0 * N);
                hn::StoreU(hn::Add(hn::LoadU(d, a + i + 1 * N), vb), d, out + i + 1 * N);
                hn::StoreU(hn::Add(hn::LoadU(d, a + i + 2 * N), vb), d, out + i + 2 * N);
                hn::StoreU(hn::Add(hn::LoadU(d, a + i + 3 * N), vb), d, out + i + 3 * N);
            }
            for (; i + N <= len; i += N) hn::StoreU(hn::Add(hn::LoadU(d, a + i), vb), d, out + i);
            for (; i < len; ++i) out[i] = a[i] + b_sc;
            return;
        case 1u:
            for (; i + 4 * N <= len; i += 4 * N) {
                hn::StoreU(hn::Sub(hn::LoadU(d, a + i + 0 * N), vb), d, out + i + 0 * N);
                hn::StoreU(hn::Sub(hn::LoadU(d, a + i + 1 * N), vb), d, out + i + 1 * N);
                hn::StoreU(hn::Sub(hn::LoadU(d, a + i + 2 * N), vb), d, out + i + 2 * N);
                hn::StoreU(hn::Sub(hn::LoadU(d, a + i + 3 * N), vb), d, out + i + 3 * N);
            }
            for (; i + N <= len; i += N) hn::StoreU(hn::Sub(hn::LoadU(d, a + i), vb), d, out + i);
            for (; i < len; ++i) out[i] = a[i] - b_sc;
            return;
        case 2u:
            for (; i + 4 * N <= len; i += 4 * N) {
                hn::StoreU(hn::Mul(hn::LoadU(d, a + i + 0 * N), vb), d, out + i + 0 * N);
                hn::StoreU(hn::Mul(hn::LoadU(d, a + i + 1 * N), vb), d, out + i + 1 * N);
                hn::StoreU(hn::Mul(hn::LoadU(d, a + i + 2 * N), vb), d, out + i + 2 * N);
                hn::StoreU(hn::Mul(hn::LoadU(d, a + i + 3 * N), vb), d, out + i + 3 * N);
            }
            for (; i + N <= len; i += N) hn::StoreU(hn::Mul(hn::LoadU(d, a + i), vb), d, out + i);
            for (; i < len; ++i) out[i] = a[i] * b_sc;
            return;
        case 3u:
            if (b_sc == 0.0) {
                const auto vnan = hn::Set(d, qnan);
                for (; i + N <= len; i += N) hn::StoreU(vnan, d, out + i);
                for (; i < len; ++i) out[i] = qnan;
            } else {
                for (; i + N <= len; i += N) hn::StoreU(hn::Div(hn::LoadU(d, a + i), vb), d, out + i);
                for (; i < len; ++i) out[i] = a[i] / b_sc;
            }
            return;
        default: break;
    }
}

void F64ArithUnary(uint8_t subop,
                   const double* HWY_RESTRICT a,
                   double* HWY_RESTRICT out,
                   uint32_t n) {
    const hn::ScalableTag<double> d;
    const size_t N = hn::Lanes(d);
    size_t i = 0;
    const size_t len = static_cast<size_t>(n);
    switch (subop) {
        case 8u:
            for (; i + N <= len; i += N) hn::StoreU(hn::Neg(hn::LoadU(d, a + i)), d, out + i);
            for (; i < len; ++i) out[i] = -a[i];
            return;
        case 9u:
            for (; i + N <= len; i += N) hn::StoreU(hn::Abs(hn::LoadU(d, a + i)), d, out + i);
            for (; i < len; ++i) out[i] = std::fabs(a[i]);
            return;
        case 10u:
            for (; i + N <= len; i += N) hn::StoreU(hn::Sqrt(hn::LoadU(d, a + i)), d, out + i);
            for (; i < len; ++i) out[i] = (a[i] < 0.0) ? std::numeric_limits<double>::quiet_NaN() : std::sqrt(a[i]);
            return;
        case 12u:
            for (; i + N <= len; i += N) hn::StoreU(hn::Floor(hn::LoadU(d, a + i)), d, out + i);
            for (; i < len; ++i) out[i] = std::floor(a[i]);
            return;
        case 13u:
            for (; i + N <= len; i += N) hn::StoreU(hn::Ceil(hn::LoadU(d, a + i)), d, out + i);
            for (; i < len; ++i) out[i] = std::ceil(a[i]);
            return;
        default: break;
    }
}

void F64PackValidWords(const double* HWY_RESTRICT out,
                       const uint64_t* a_valid,
                       const uint64_t* b_valid,
                       bool has_b,
                       bool b_scalar_ok,
                       uint64_t* HWY_RESTRICT out_valid,
                       uint32_t n) {
    const hn::ScalableTag<double> d;
    const size_t N = hn::Lanes(d);
    const uint32_t vwords = (n + 63u) >> 6;
    for (uint32_t w = 0u; w < vwords; ++w) {
        uint64_t in_m = a_valid ? a_valid[w] : ~0ULL;
        if (has_b) in_m &= b_valid ? b_valid[w] : (b_scalar_ok ? ~0ULL : 0ULL);
        uint32_t base = w << 6;
        uint32_t rem = n - base;
        uint32_t cnt = (rem < 64u) ? rem : 64u;
        if (cnt < 64u) in_m &= ((1ULL << cnt) - 1ULL);
        if (in_m == 0ULL) {
            out_valid[w] = 0ULL;
            continue;
        }
        if (cnt == 64u && (64u % N == 0u)) {
            auto all_ok = hn::FirstN(d, N);
            for (size_t j = 0; j < 64u; j += N) {
                const auto v = hn::LoadU(d, out + base + j);
                all_ok = hn::And(all_ok, hn::Eq(v, v));
            }
            if (hn::AllTrue(d, all_ok)) {
                out_valid[w] = in_m;
                continue;
            }
            uint64_t non_nan = 0ULL;
            uint8_t mbuf[8] = {};
            for (size_t j = 0; j < 64u; j += N) {
                const auto v = hn::LoadU(d, out + base + j);
                hn::StoreMaskBits(d, hn::Eq(v, v), mbuf);
                uint64_t bits = static_cast<uint64_t>(mbuf[0]) & ((N == 8) ? 0xFFULL : ((1ULL << N) - 1ULL));
                non_nan |= (bits << j);
            }
            out_valid[w] = in_m & non_nan;
            continue;
        }
        uint64_t non_nan = 0ULL;
        for (uint32_t j = 0u; j < cnt; ++j) {
            double v = out[base + j];
            if (v == v) non_nan |= (1ULL << j);
        }
        out_valid[w] = in_m & non_nan;
    }
}

void F64CmpWords(uint8_t subop,
                 const double* HWY_RESTRICT a,
                 const double* HWY_RESTRICT b,
                 bool scalar,
                 double b_sc,
                 bool b_sc_ok,
                 const uint64_t* a_valid,
                 const uint64_t* b_valid,
                 uint64_t* HWY_RESTRICT out_valid,
                 uint64_t* HWY_RESTRICT out_data,
                 uint32_t n) {
    const hn::ScalableTag<double> d;
    const size_t N = hn::Lanes(d);
    const uint32_t vwords = (n + 63u) >> 6;
    if (scalar && (!b_sc_ok || std::isnan(b_sc))) {
        std::memset(out_valid, 0, static_cast<size_t>(vwords) * sizeof(uint64_t));
        std::memset(out_data, 0, static_cast<size_t>(vwords) * sizeof(uint64_t));
        return;
    }
    const auto vbs = hn::Set(d, b_sc);
    const uint64_t lane_mask = (N >= 64) ? ~0ULL : ((1ULL << N) - 1ULL);
    for (uint32_t w = 0u; w < vwords; ++w) {
        uint64_t in_m = a_valid ? a_valid[w] : ~0ULL;
        if (!scalar) in_m &= b_valid ? b_valid[w] : ~0ULL;
        uint32_t base = w << 6;
        uint32_t rem = n - base;
        uint32_t cnt = (rem < 64u) ? rem : 64u;
        if (cnt < 64u) in_m &= ((1ULL << cnt) - 1ULL);
        if (in_m == 0ULL) {
            out_valid[w] = 0ULL;
            out_data[w] = 0ULL;
            continue;
        }
        if (cnt == 64u && (64u % N == 0u)) {
            uint64_t vm = 0ULL;
            uint64_t dm = 0ULL;
            uint8_t mbuf[8] = {};
            for (size_t j = 0; j < 64u; j += N) {
                const auto va = hn::LoadU(d, a + base + j);
                const auto vy = scalar ? vbs : hn::LoadU(d, b + base + j);
                const auto ok = scalar ? hn::Eq(va, va) : hn::And(hn::Eq(va, va), hn::Eq(vy, vy));
                hn::Mask<decltype(d)> c;
                switch (subop) {
                    case 0u: c = hn::Eq(va, vy); break;
                    case 1u: c = hn::Not(hn::Eq(va, vy)); break;
                    case 2u: c = hn::Lt(va, vy); break;
                    case 3u: c = hn::Le(va, vy); break;
                    case 4u: c = hn::Gt(va, vy); break;
                    case 5u: c = hn::Ge(va, vy); break;
                    default: c = hn::MaskFalse(d); break;
                }
                hn::StoreMaskBits(d, ok, mbuf);
                vm |= ((static_cast<uint64_t>(mbuf[0]) & lane_mask) << j);
                hn::StoreMaskBits(d, hn::And(c, ok), mbuf);
                dm |= ((static_cast<uint64_t>(mbuf[0]) & lane_mask) << j);
            }
            out_valid[w] = in_m & vm;
            out_data[w]  = in_m & dm;
            continue;
        }
        uint64_t vm = 0ULL;
        uint64_t dm = 0ULL;
        for (uint32_t j = 0u; j < cnt; ++j) {
            if (((in_m >> j) & 1ULL) == 0ULL) continue;
            double x = a[base + j];
            double y = scalar ? b_sc : b[base + j];
            if (std::isnan(x) || std::isnan(y)) continue;
            bool res = false;
            if (subop == 0u) res = (x == y);
            else if (subop == 1u) res = (x != y);
            else if (subop == 2u) res = (x <  y);
            else if (subop == 3u) res = (x <= y);
            else if (subop == 4u) res = (x >  y);
            else if (subop == 5u) res = (x >= y);
            vm |= (1ULL << j);
            if (res) dm |= (1ULL << j);
        }
        out_valid[w] = vm;
        out_data[w]  = dm;
    }
}

void BitsetOpWords(uint8_t subop,
                   const uint64_t* HWY_RESTRICT a,
                   const uint64_t* HWY_RESTRICT b,
                   uint64_t* HWY_RESTRICT out,
                   size_t nwords) {
    const hn::ScalableTag<uint64_t> d;
    const size_t N = hn::Lanes(d);
    size_t i = 0;
    switch (subop) {
        case 0u:
            for (; i + N <= nwords; i += N) hn::StoreU(hn::Or(hn::LoadU(d, a + i), hn::LoadU(d, b + i)), d, out + i);
            for (; i < nwords; ++i) out[i] = a[i] | b[i];
            return;
        case 1u:
            for (; i + N <= nwords; i += N) hn::StoreU(hn::And(hn::LoadU(d, a + i), hn::LoadU(d, b + i)), d, out + i);
            for (; i < nwords; ++i) out[i] = a[i] & b[i];
            return;
        case 2u:
            for (; i + N <= nwords; i += N) hn::StoreU(hn::AndNot(hn::LoadU(d, b + i), hn::LoadU(d, a + i)), d, out + i);
            for (; i < nwords; ++i) out[i] = a[i] & ~b[i];
            return;
        case 3u:
            for (; i + N <= nwords; i += N) hn::StoreU(hn::Xor(hn::LoadU(d, a + i), hn::LoadU(d, b + i)), d, out + i);
            for (; i < nwords; ++i) out[i] = a[i] ^ b[i];
            return;
        default: break;
    }
}

double F64SimMetric(uint8_t metric,
                    const double* HWY_RESTRICT a,
                    const double* HWY_RESTRICT b,
                    uint32_t d_len) {
    const hn::ScalableTag<double> d;
    const size_t N = hn::Lanes(d);
    const size_t len = static_cast<size_t>(d_len);
    size_t i = 0;
    if (metric == 0u) {
        auto acc = hn::Zero(d);
        for (; i + N <= len; i += N) {
            acc = hn::Add(acc, hn::Mul(hn::LoadU(d, a + i), hn::LoadU(d, b + i)));
        }
        double dot = hn::ReduceSum(d, acc);
        for (; i < len; ++i) dot += a[i] * b[i];
        return dot;
    } else if (metric == 1u) {
        auto vdot = hn::Zero(d), vna = hn::Zero(d), vnb = hn::Zero(d);
        for (; i + N <= len; i += N) {
            const auto vx = hn::LoadU(d, a + i);
            const auto vy = hn::LoadU(d, b + i);
            vdot = hn::Add(vdot, hn::Mul(vx, vy));
            vna  = hn::Add(vna,  hn::Mul(vx, vx));
            vnb  = hn::Add(vnb,  hn::Mul(vy, vy));
        }
        double dot = hn::ReduceSum(d, vdot);
        double na  = hn::ReduceSum(d, vna);
        double nb  = hn::ReduceSum(d, vnb);
        for (; i < len; ++i) {
            double x = a[i], y = b[i];
            dot += x * y; na += x * x; nb += y * y;
        }
        double denom = std::sqrt(na) * std::sqrt(nb);
        return (denom == 0.0) ? std::numeric_limits<double>::quiet_NaN() : (dot / denom);
    } else if (metric == 2u || metric == 3u) {
        auto vl2 = hn::Zero(d);
        for (; i + N <= len; i += N) {
            const auto diff = hn::Sub(hn::LoadU(d, a + i), hn::LoadU(d, b + i));
            vl2 = hn::Add(vl2, hn::Mul(diff, diff));
        }
        double l2 = hn::ReduceSum(d, vl2);
        for (; i < len; ++i) {
            double diff = a[i] - b[i];
            l2 += diff * diff;
        }
        return (metric == 2u) ? -l2 : l2;
    }
    return std::numeric_limits<double>::quiet_NaN();
}

} // namespace HWY_NAMESPACE
} // namespace impulse::vm::v11::simd
HWY_AFTER_NAMESPACE();

#if HWY_ONCE
namespace impulse::vm::v11::simd {

HWY_EXPORT(F64ArithVec);
HWY_EXPORT(F64ArithScalar);
HWY_EXPORT(F64ArithUnary);
HWY_EXPORT(F64PackValidWords);
HWY_EXPORT(F64CmpWords);
HWY_EXPORT(BitsetOpWords);
HWY_EXPORT(F64SimMetric);

void f64_arith_vec(uint8_t subop, const double* a, const double* b, double* out, uint32_t n) noexcept {
    HWY_DYNAMIC_DISPATCH(F64ArithVec)(subop, a, b, out, n);
}

void f64_arith_scalar(uint8_t subop, const double* a, double b_sc, double* out, uint32_t n) noexcept {
    HWY_DYNAMIC_DISPATCH(F64ArithScalar)(subop, a, b_sc, out, n);
}

void f64_arith_unary(uint8_t subop, const double* a, double* out, uint32_t n) noexcept {
    HWY_DYNAMIC_DISPATCH(F64ArithUnary)(subop, a, out, n);
}

void f64_pack_valid_words(const double* out, const uint64_t* a_valid, const uint64_t* b_valid,
                          bool has_b, bool b_scalar_ok, uint64_t* out_valid, uint32_t n) noexcept {
    HWY_DYNAMIC_DISPATCH(F64PackValidWords)(out, a_valid, b_valid, has_b, b_scalar_ok, out_valid, n);
}

void f64_cmp_words(uint8_t subop, const double* a, const double* b, bool scalar, double b_sc, bool b_sc_ok,
                   const uint64_t* a_valid, const uint64_t* b_valid, uint64_t* out_valid, uint64_t* out_data,
                   uint32_t n) noexcept {
    HWY_DYNAMIC_DISPATCH(F64CmpWords)(subop, a, b, scalar, b_sc, b_sc_ok, a_valid, b_valid, out_valid, out_data, n);
}

void bitset_op_words(uint8_t subop, const uint64_t* a, const uint64_t* b, uint64_t* out, size_t nwords) noexcept {
    HWY_DYNAMIC_DISPATCH(BitsetOpWords)(subop, a, b, out, nwords);
}

double f64_sim_metric(uint8_t metric, const double* a, const double* b, uint32_t d_len) noexcept {
    return HWY_DYNAMIC_DISPATCH(F64SimMetric)(metric, a, b, d_len);
}

} // namespace impulse::vm::v11::simd
#endif
