/**
 * @file v11_xxh3.hpp
 * @brief Zero-dependency canonical XXH3-64 (v0.8.2) for ImpulseVM v0.11.0 ICTX v2.
 *
 * Bit-identical to V11_xxh3 (impulse-ocaml), V11Xxh3.java (impulse-graph-java),
 * and xxhash.h v0.8.2 across all length regimes (0, 1..3, 4..8, 9..16, 17..128,
 * 129..240, > 240 bytes). Zero heap allocations.
 */
#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

namespace impulse::vm::v11 {

class V11Xxh3 {
public:
    static inline uint64_t hash64(const uint8_t* input, size_t len) noexcept {
        return hash64_with_seed(input, len, 0ULL);
    }

    static inline uint64_t hash64_with_seed(const uint8_t* input, size_t len, uint64_t seed) noexcept {
        if (len == 0) {
            return xxh64_avalanche(seed ^ (read_le64(kDefaultSecret, 56) ^ read_le64(kDefaultSecret, 64)));
        }
        if (len <= 3) {
            return hash_len_1_to_3(input, len, kDefaultSecret, seed);
        }
        if (len <= 8) {
            return hash_len_4_to_8(input, len, kDefaultSecret, seed);
        }
        if (len <= 16) {
            return hash_len_9_to_16(input, len, kDefaultSecret, seed);
        }
        if (len <= 128) {
            return hash_len_17_to_128(input, len, kDefaultSecret, seed);
        }
        if (len <= 240) {
            return hash_len_129_to_240(input, len, kDefaultSecret, seed);
        }
        if (seed == 0ULL) {
            return hash_long(input, len, kDefaultSecret);
        }
        uint8_t custom_secret[192];
        init_custom_secret(custom_secret, seed);
        return hash_long(input, len, custom_secret);
    }

private:
    static constexpr uint64_t kPrime32_1 = 0x9E3779B1ULL;
    static constexpr uint64_t kPrime32_2 = 0x85EBCA77ULL;
    static constexpr uint64_t kPrime32_3 = 0xC2B2AE3DULL;

    static constexpr uint64_t kPrime64_1 = 0x9E3779B185EBCA87ULL;
    static constexpr uint64_t kPrime64_2 = 0xC2B2AE3D27D4EB4FULL;
    static constexpr uint64_t kPrime64_3 = 0x165667B19E3779F9ULL;
    static constexpr uint64_t kPrime64_4 = 0x85EBCA77C2B2AE63ULL;
    static constexpr uint64_t kPrime64_5 = 0x27D4EB2F165667C5ULL;

    static constexpr uint64_t kPrimeMx1 = 0x165667919E3779F9ULL;
    static constexpr uint64_t kPrimeMx2 = 0x9FB21C651E98DF25ULL;

    static constexpr uint8_t kDefaultSecret[192] = {
        0xb8, 0xfe, 0x6c, 0x39, 0x23, 0xa4, 0x4b, 0xbe,
        0x7c, 0x01, 0x81, 0x2c, 0xf7, 0x21, 0xad, 0x1c,
        0xde, 0xd4, 0x6d, 0xe9, 0x83, 0x90, 0x97, 0xdb,
        0x72, 0x40, 0xa4, 0xa4, 0xb7, 0xb3, 0x67, 0x1f,
        0xcb, 0x79, 0xe6, 0x4e, 0xcc, 0xc0, 0xe5, 0x78,
        0x82, 0x5a, 0xd0, 0x7d, 0xcc, 0xff, 0x72, 0x21,
        0xb8, 0x08, 0x46, 0x74, 0xf7, 0x43, 0x24, 0x8e,
        0xe0, 0x35, 0x90, 0xe6, 0x81, 0x3a, 0x26, 0x4c,
        0x3c, 0x28, 0x52, 0xbb, 0x91, 0xc3, 0x00, 0xcb,
        0x88, 0xd0, 0x65, 0x8b, 0x1b, 0x53, 0x2e, 0xa3,
        0x71, 0x64, 0x48, 0x97, 0xa2, 0x0d, 0xf9, 0x4e,
        0x38, 0x19, 0xef, 0x46, 0xa9, 0xde, 0xac, 0xd8,
        0xa8, 0xfa, 0x76, 0x3f, 0xe3, 0x9c, 0x34, 0x3f,
        0xf9, 0xdc, 0xbb, 0xc7, 0xc7, 0x0b, 0x4f, 0x1d,
        0x8a, 0x51, 0xe0, 0x4b, 0xcd, 0xb4, 0x59, 0x31,
        0xc8, 0x9f, 0x7e, 0xc9, 0xd9, 0x78, 0x73, 0x64,
        0xea, 0xc5, 0xac, 0x83, 0x34, 0xd3, 0xeb, 0xc3,
        0xc5, 0x81, 0xa0, 0xff, 0xfa, 0x13, 0x63, 0xeb,
        0x17, 0x0d, 0xdd, 0x51, 0xb7, 0xf0, 0xda, 0x49,
        0xd3, 0x16, 0x55, 0x26, 0x29, 0xd4, 0x68, 0x9e,
        0x2b, 0x16, 0xbe, 0x58, 0x7d, 0x47, 0xa1, 0xfc,
        0x8f, 0xf8, 0xb8, 0xd1, 0x7a, 0xd0, 0x31, 0xce,
        0x45, 0xcb, 0x3a, 0x8f, 0x95, 0x16, 0x04, 0x28,
        0xaf, 0xd7, 0xfb, 0xca, 0xbb, 0x4b, 0x40, 0x7e
    };

    static inline uint32_t read_le32(const uint8_t* p, size_t off) noexcept {
        uint32_t v;
        std::memcpy(&v, p + off, 4);
        return v;
    }

    static inline uint64_t read_le64(const uint8_t* p, size_t off) noexcept {
        uint64_t v;
        std::memcpy(&v, p + off, 8);
        return v;
    }

    static inline void write_le64(uint8_t* p, size_t off, uint64_t v) noexcept {
        std::memcpy(p + off, &v, 8);
    }

    static inline uint32_t bswap32(uint32_t x) noexcept {
#if defined(_MSC_VER)
        return _byteswap_ulong(x);
#else
        return __builtin_bswap32(x);
#endif
    }

    static inline uint64_t bswap64(uint64_t x) noexcept {
#if defined(_MSC_VER)
        return _byteswap_uint64(x);
#else
        return __builtin_bswap64(x);
#endif
    }

    static inline uint64_t rotl64(uint64_t x, int r) noexcept {
        return (x << r) | (x >> (64 - r));
    }

    static inline uint64_t xorshift64(uint64_t v, int shift) noexcept {
        return v ^ (v >> shift);
    }

    static inline uint64_t mult32_to_64(uint64_t x, uint64_t y) noexcept {
        return static_cast<uint64_t>(static_cast<uint32_t>(x)) *
               static_cast<uint64_t>(static_cast<uint32_t>(y));
    }

    static inline uint64_t mul128_fold64(uint64_t lhs, uint64_t rhs) noexcept {
#if defined(__SIZEOF_INT128__)
        __uint128_t prod = static_cast<__uint128_t>(lhs) * static_cast<__uint128_t>(rhs);
        return static_cast<uint64_t>(prod) ^ static_cast<uint64_t>(prod >> 64);
#else
        uint64_t lo_lo = mult32_to_64(lhs, rhs);
        uint64_t hi_lo = mult32_to_64(lhs >> 32, rhs);
        uint64_t lo_hi = mult32_to_64(lhs, rhs >> 32);
        uint64_t hi_hi = mult32_to_64(lhs >> 32, rhs >> 32);
        uint64_t cross = (lo_lo >> 32) + (hi_lo & 0xFFFFFFFFULL) + lo_hi;
        uint64_t upper = (hi_lo >> 32) + (cross >> 32) + hi_hi;
        uint64_t lower = (cross << 32) | (lo_lo & 0xFFFFFFFFULL);
        return lower ^ upper;
#endif
    }

    static inline uint64_t xxh64_avalanche(uint64_t h) noexcept {
        h = xorshift64(h, 33);
        h *= kPrime64_2;
        h = xorshift64(h, 29);
        h *= kPrime64_3;
        return xorshift64(h, 32);
    }

    static inline uint64_t xxh3_avalanche(uint64_t h) noexcept {
        h = xorshift64(h, 37);
        h *= kPrimeMx1;
        return xorshift64(h, 32);
    }

    static inline uint64_t rrmxmx(uint64_t h, uint64_t len) noexcept {
        h ^= rotl64(h, 49) ^ rotl64(h, 24);
        h *= kPrimeMx2;
        h ^= (h >> 35) + len;
        h *= kPrimeMx2;
        return xorshift64(h, 28);
    }

    static inline uint64_t mix16b(const uint8_t* input, size_t in_off,
                                  const uint8_t* secret, size_t sec_off,
                                  uint64_t seed) noexcept {
        uint64_t input_lo = read_le64(input, in_off);
        uint64_t input_hi = read_le64(input, in_off + 8);
        uint64_t key_lo = read_le64(secret, sec_off) + seed;
        uint64_t key_hi = read_le64(secret, sec_off + 8) - seed;
        return mul128_fold64(input_lo ^ key_lo, input_hi ^ key_hi);
    }

    static inline uint64_t hash_len_1_to_3(const uint8_t* input, size_t len,
                                           const uint8_t* secret, uint64_t seed) noexcept {
        uint64_t c1 = input[0];
        uint64_t c2 = input[len >> 1];
        uint64_t c3 = input[len - 1];
        uint64_t combined = (c1 << 16) | (c2 << 24) | c3 | (static_cast<uint64_t>(len) << 8);
        uint64_t bitflip = static_cast<uint64_t>(read_le32(secret, 0) ^ read_le32(secret, 4)) + seed;
        return xxh64_avalanche(combined ^ bitflip);
    }

    static inline uint64_t hash_len_4_to_8(const uint8_t* input, size_t len,
                                           const uint8_t* secret, uint64_t seed) noexcept {
        seed ^= static_cast<uint64_t>(bswap32(static_cast<uint32_t>(seed))) << 32;
        uint64_t input1 = read_le32(input, 0);
        uint64_t input2 = read_le32(input, len - 4);
        uint64_t bitflip = (read_le64(secret, 8) ^ read_le64(secret, 16)) - seed;
        uint64_t input64 = input2 + (input1 << 32);
        return rrmxmx(input64 ^ bitflip, static_cast<uint64_t>(len));
    }

    static inline uint64_t hash_len_9_to_16(const uint8_t* input, size_t len,
                                            const uint8_t* secret, uint64_t seed) noexcept {
        uint64_t bitflip1 = (read_le64(secret, 24) ^ read_le64(secret, 32)) + seed;
        uint64_t bitflip2 = (read_le64(secret, 40) ^ read_le64(secret, 48)) - seed;
        uint64_t input_lo = read_le64(input, 0) ^ bitflip1;
        uint64_t input_hi = read_le64(input, len - 8) ^ bitflip2;
        uint64_t acc = static_cast<uint64_t>(len) + bswap64(input_lo) + input_hi +
                       mul128_fold64(input_lo, input_hi);
        return xxh3_avalanche(acc);
    }

    static inline uint64_t hash_len_17_to_128(const uint8_t* input, size_t len,
                                              const uint8_t* secret, uint64_t seed) noexcept {
        uint64_t acc = static_cast<uint64_t>(len) * kPrime64_1;
        if (len > 32) {
            if (len > 64) {
                if (len > 96) {
                    acc += mix16b(input, 48, secret, 96, seed);
                    acc += mix16b(input, len - 64, secret, 112, seed);
                }
                acc += mix16b(input, 32, secret, 64, seed);
                acc += mix16b(input, len - 48, secret, 80, seed);
            }
            acc += mix16b(input, 16, secret, 32, seed);
            acc += mix16b(input, len - 32, secret, 48, seed);
        }
        acc += mix16b(input, 0, secret, 0, seed);
        acc += mix16b(input, len - 16, secret, 16, seed);
        return xxh3_avalanche(acc);
    }

    static inline uint64_t hash_len_129_to_240(const uint8_t* input, size_t len,
                                               const uint8_t* secret, uint64_t seed) noexcept {
        uint64_t acc = static_cast<uint64_t>(len) * kPrime64_1;
        size_t nb_rounds = len / 16;
        for (size_t i = 0; i < 8; ++i) {
            acc += mix16b(input, 16 * i, secret, 16 * i, seed);
        }
        acc = xxh3_avalanche(acc);
        for (size_t i = 8; i < nb_rounds; ++i) {
            acc += mix16b(input, 16 * i, secret, 16 * (i - 8) + 3, seed);
        }
        acc += mix16b(input, len - 16, secret, 119, seed);
        return xxh3_avalanche(acc);
    }

    static inline void init_custom_secret(uint8_t* dst, uint64_t seed) noexcept {
        for (size_t i = 0; i < 12; ++i) {
            size_t off = i * 16;
            write_le64(dst, off, read_le64(kDefaultSecret, off) + seed);
            write_le64(dst, off + 8, read_le64(kDefaultSecret, off + 8) - seed);
        }
    }

    static inline void accumulate512(uint64_t acc[8], const uint8_t* input, size_t in_off,
                                     const uint8_t* secret, size_t sec_off) noexcept {
        for (size_t i = 0; i < 8; ++i) {
            uint64_t data_val = read_le64(input, in_off + i * 8);
            uint64_t data_key = data_val ^ read_le64(secret, sec_off + i * 8);
            acc[i ^ 1] += data_val;
            acc[i] += mult32_to_64(data_key, data_key >> 32);
        }
    }

    static inline void scramble_acc(uint64_t acc[8], const uint8_t* secret, size_t sec_off) noexcept {
        for (size_t i = 0; i < 8; ++i) {
            uint64_t v = xorshift64(acc[i], 47);
            v ^= read_le64(secret, sec_off + i * 8);
            acc[i] = v * kPrime32_1;
        }
    }

    static inline uint64_t mix2_accs(const uint64_t acc[8], size_t idx,
                                     const uint8_t* secret, size_t sec_off) noexcept {
        return mul128_fold64(
            acc[idx] ^ read_le64(secret, sec_off),
            acc[idx + 1] ^ read_le64(secret, sec_off + 8));
    }

    static inline uint64_t merge_accs(const uint64_t acc[8], const uint8_t* secret,
                                      size_t sec_off, uint64_t start) noexcept {
        uint64_t r = start;
        r += mix2_accs(acc, 0, secret, sec_off);
        r += mix2_accs(acc, 2, secret, sec_off + 16);
        r += mix2_accs(acc, 4, secret, sec_off + 32);
        r += mix2_accs(acc, 6, secret, sec_off + 48);
        return xxh3_avalanche(r);
    }

    static inline uint64_t hash_long(const uint8_t* input, size_t len, const uint8_t* secret) noexcept {
        uint64_t acc[8] = {
            kPrime32_3, kPrime64_1, kPrime64_2, kPrime64_3,
            kPrime64_4, kPrime32_2, kPrime64_5, kPrime32_1
        };
        constexpr size_t kSecretLen = 192;
        constexpr size_t kStripeLen = 64;
        constexpr size_t kNbStripesPerBlock = (kSecretLen - kStripeLen) / 8; // 16
        constexpr size_t kBlockLen = kStripeLen * kNbStripesPerBlock;        // 1024
        size_t nb_blocks = (len - 1) / kBlockLen;
        for (size_t n = 0; n < nb_blocks; ++n) {
            for (size_t s = 0; s < kNbStripesPerBlock; ++s) {
                accumulate512(acc, input, n * kBlockLen + s * kStripeLen, secret, s * 8);
            }
            scramble_acc(acc, secret, kSecretLen - kStripeLen);
        }
        size_t nb_stripes = ((len - 1) - (kBlockLen * nb_blocks)) / kStripeLen;
        for (size_t s = 0; s < nb_stripes; ++s) {
            accumulate512(acc, input, nb_blocks * kBlockLen + s * kStripeLen, secret, s * 8);
        }
        accumulate512(acc, input, len - kStripeLen, secret, kSecretLen - kStripeLen - 7);
        return merge_accs(acc, secret, 11, static_cast<uint64_t>(len) * kPrime64_1);
    }
};

} // namespace impulse::vm::v11
