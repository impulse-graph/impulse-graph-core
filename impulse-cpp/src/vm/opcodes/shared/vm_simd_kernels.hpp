#pragma once

#include <cstddef>
#include <cstdint>

#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
static inline uint32_t imp_vm_ctz64(uint64_t x) noexcept {
    unsigned long idx = 0;
    _BitScanForward64(&idx, x);
    return static_cast<uint32_t>(idx);
}
#else
static inline uint32_t imp_vm_ctz64(uint64_t x) noexcept {
    return static_cast<uint32_t>(__builtin_ctzll(x));
}
#endif

namespace impulse::vm::simd {

// Google Highway HWY_DYNAMIC_DISPATCH kernels (implemented in src/vm/vm_simd_hwy.cpp).
// Automatically dispatches NEON/SVE on ARM64 and SSE4/AVX2/AVX-512 on x86-64 across Clang, GCC, and MSVC.
void f64_arith_vec(uint8_t subop, const double* a, const double* b, double* out, uint32_t n) noexcept;
void f64_arith_scalar(uint8_t subop, const double* a, double b_sc, double* out, uint32_t n) noexcept;
void f64_arith_unary(uint8_t subop, const double* a, double* out, uint32_t n) noexcept;
void f64_arith_vec_valid(uint8_t subop, const double* a, const double* b, const uint64_t* a_valid,
                         const uint64_t* b_valid, double* out, uint64_t* out_valid, uint32_t n) noexcept;
void f64_arith_scalar_valid(uint8_t subop, const double* a, double b_sc, const uint64_t* a_valid,
                            double* out, uint64_t* out_valid, uint32_t n) noexcept;
void f64_arith_unary_valid(uint8_t subop, const double* a, const uint64_t* a_valid,
                           double* out, uint64_t* out_valid, uint32_t n) noexcept;
void f64_pack_valid_words(const double* out, const uint64_t* a_valid, const uint64_t* b_valid,
                          bool has_b, bool b_scalar_ok, uint64_t* out_valid, uint32_t n) noexcept;
void f64_cmp_words(uint8_t subop, const double* a, const double* b, bool scalar, double b_sc, bool b_sc_ok,
                   const uint64_t* a_valid, const uint64_t* b_valid, uint64_t* out_valid, uint64_t* out_data,
                   uint32_t n) noexcept;
void bitset_op_words(uint8_t subop, const uint64_t* a, const uint64_t* b, uint64_t* out, size_t nwords) noexcept;
double f64_sim_metric(uint8_t metric, const double* a, const double* b, uint32_t d_len) noexcept;

// Word-level CTZ extraction of true lanes from a 3VL boolean vector.
static inline uint32_t b2f_extract_words(const uint64_t* valid,
                                         const uint64_t* data,
                                         uint32_t n,
                                         uint32_t* __restrict out_ids) noexcept {
    const uint32_t vwords = (n + 63u) >> 6;
    uint32_t cnt = 0u;
    for (uint32_t w = 0u; w < vwords; ++w) {
        uint64_t m = (valid ? valid[w] : ~0ULL) & data[w];
        uint32_t base = w << 6;
        uint32_t rem = n - base;
        if (rem < 64u) m &= ((1ULL << rem) - 1ULL);
        while (m != 0ULL) {
            uint32_t tz = imp_vm_ctz64(m);
            out_ids[cnt++] = base + tz;
            m &= (m - 1ULL);
        }
    }
    return cnt;
}

} // namespace impulse::vm::simd
