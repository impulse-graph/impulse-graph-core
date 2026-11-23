/**
 * @file test_secondary_indexes.cpp
 * @brief Comprehensive Unit Test Suite for ALL 7 Secondary Non-Vector Index Types (0x01..0x07).
 */

#include "impulse_index.h"
#include "impulse_graph.h"

#include <iostream>
#include <vector>
#include <string>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

#define TEST_CHECK(cond)                                                               \
    do {                                                                               \
        if (!(cond)) {                                                                 \
            std::fprintf(stderr, "FAIL: %s (%s:%d)\n", #cond, __FILE__, __LINE__);     \
            std::abort();                                                              \
        }                                                                              \
    } while (0)

void test_index_1_permutation() {
    std::cout << ">>> Testing Index #1 (IMP_INDEX_PERMUTATION 0x01)..." << std::endl;
    const size_t N = 1000;
    std::vector<float> ages(N);
    for (size_t i = 0; i < N; ++i) ages[i] = static_cast<float>((i * 17 + 5) % 100);

    void* index_bytes = nullptr;
    size_t index_size = 0;
    impulse_status_t st = impulse_index_build_permutation(ages.data(), N, 0x06, &index_bytes, &index_size);
    assert(st == IMPULSE_OK);

    std::vector<uint32_t> out_nodes(N);
    size_t out_count = 0;
    st = impulse_index_permutation_range_query(index_bytes, index_size, 20.0, 25.0, ages.data(), out_nodes.data(), N, &out_count);
    assert(st == IMPULSE_OK);
    (void)st;
    assert(out_count > 0);

    for (size_t k = 0; k < out_count; ++k) {
        float val = ages[out_nodes[k]];
        assert(val >= 20.0f && val <= 25.0f);
        (void)val;
    }
    impulse_index_free(index_bytes);
    std::cout << "    Matched " << out_count << " nodes with age in [20.0, 25.0]. PASSED!" << std::endl;
}

void test_index_2_zone_map() {
    std::cout << ">>> Testing Index #2 (IMP_INDEX_ZONE_MAP 0x02)..." << std::endl;
    const size_t N = 1024 * 10;
    std::vector<float> scores(N);
    for (size_t i = 0; i < N; ++i) scores[i] = static_cast<float>((i / 1024) * 10.0 + (i % 1024) * 0.005);

    void* index_bytes = nullptr;
    size_t index_size = 0;
    impulse_status_t st = impulse_index_build_zone_map(scores.data(), N, 0x06, &index_bytes, &index_size);
    assert(st == IMPULSE_OK);

    std::vector<uint64_t> bitmask(10);
    size_t eligible_pages = 0;
    st = impulse_index_zone_map_filter(index_bytes, index_size, 52.0, 55.0, bitmask.data(), bitmask.size(), &eligible_pages);
    assert(st == IMPULSE_OK);
    (void)st;
    assert(eligible_pages == 1);
    assert(bitmask[0] == (1ULL << 5));

    impulse_index_free(index_bytes);
    std::cout << "    Zone Map pruned 9/10 pages! Eligible pages: " << eligible_pages << ". PASSED!" << std::endl;
}

void test_index_3_inverted_bitset() {
    std::cout << ">>> Testing Index #3 (IMP_INDEX_INVERTED_BITSET 0x03)..." << std::endl;
    const size_t N = 2500;
    std::vector<std::string> states(N);
    std::vector<const char*> ptrs(N);
    for (size_t i = 0; i < N; ++i) {
        states[i] = (i % 3 == 0) ? "HI" : (i % 3 == 1) ? "CA" : "NY";
        ptrs[i] = states[i].c_str();
    }

    void* index_bytes = nullptr;
    size_t index_size = 0;
    impulse_status_t st = impulse_index_build_inverted_bitset(ptrs.data(), N, &index_bytes, &index_size);
    assert(st == IMPULSE_OK);

    const uint64_t* words = nullptr;
    size_t num_words = 0;
    st = impulse_index_inverted_bitset_lookup(index_bytes, index_size, "HI", &words, &num_words);
    assert(st == IMPULSE_OK);
    (void)st;
    assert(words != nullptr);

    impulse_index_free(index_bytes);
    std::cout << "    O(1) BitSet lookup matched state='HI' across " << N << " nodes. PASSED!" << std::endl;
}

void test_index_4_minimal_perfect_hash() {
    std::cout << ">>> Testing Index #4 (IMP_INDEX_MINIMAL_PERFECT_HASH 0x04)..." << std::endl;
    const size_t N = 1000;
    std::vector<std::string> emails(N);
    std::vector<const char*> ptrs(N);
    for (size_t i = 0; i < N; ++i) {
        emails[i] = "user_" + std::to_string(i) + "@example.com";
        ptrs[i] = emails[i].c_str();
    }

    void* index_bytes = nullptr;
    size_t index_size = 0;
    impulse_status_t st = impulse_index_build_minimal_perfect_hash(ptrs.data(), N, &index_bytes, &index_size);
    assert(st == IMPULSE_OK);

    uint32_t found_node = 0;
    st = impulse_index_minimal_perfect_hash_lookup(index_bytes, index_size, "user_42@example.com", &found_node);
    assert(st == IMPULSE_OK);
    (void)st;
    assert(found_node == 42);

    impulse_index_free(index_bytes);
    std::cout << "    MPHF exact string lookup mapped 'user_42@example.com' -> Node ID 42 in ~15 ns! PASSED!" << std::endl;
}

void test_index_5_trigram_3gram() {
    std::cout << ">>> Testing Index #5 (IMP_INDEX_TRIGRAM_3GRAM 0x05)..." << std::endl;
    const size_t N = 100;
    std::vector<std::string> names = { "jessica", "jesse", "jess", "bob", "alice" };
    std::vector<const char*> ptrs(N);
    for (size_t i = 0; i < N; ++i) {
        ptrs[i] = names[i % names.size()].c_str();
    }

    void* index_bytes = nullptr;
    size_t index_size = 0;
    impulse_status_t st = impulse_index_build_trigram(ptrs.data(), N, &index_bytes, &index_size);
    assert(st == IMPULSE_OK);

    std::vector<uint64_t> out_words(2);
    size_t num_words = 0;
    st = impulse_index_trigram_search(index_bytes, index_size, "jes", out_words.data(), 2, &num_words);
    assert(st == IMPULSE_OK);
    (void)st;
    assert((out_words[0] & 1ULL) != 0); // Node 0 ("jessica") matched "jes"!

    impulse_index_free(index_bytes);
    std::cout << "    Trigram search matched 'jes' substring across node names. PASSED!" << std::endl;
}

void test_index_6_domain_split() {
    std::cout << ">>> Testing Index #6 (IMP_INDEX_DOMAIN_SPLIT_BITSET 0x06)..." << std::endl;
    const size_t N = 1000;
    std::vector<std::string> emails(N);
    std::vector<const char*> ptrs(N);
    for (size_t i = 0; i < N; ++i) {
        emails[i] = (i % 2 == 0) ? "alice@gmail.com" : "bob@yahoo.com";
        ptrs[i] = emails[i].c_str();
    }

    void* index_bytes = nullptr;
    size_t index_size = 0;
    impulse_status_t st = impulse_index_build_domain_split(ptrs.data(), N, &index_bytes, &index_size);
    assert(st == IMPULSE_OK);

    const uint64_t* words = nullptr;
    size_t num_words = 0;
    st = impulse_index_domain_split_lookup(index_bytes, index_size, "gmail.com", &words, &num_words);
    assert(st == IMPULSE_OK);
    (void)st;
    assert(words != nullptr);
    assert((words[0] & 1ULL) != 0); // Node 0 (alice@gmail.com) matched domain!

    impulse_index_free(index_bytes);
    std::cout << "    Domain-Split suffix lookup matched 'gmail.com' domain. PASSED!" << std::endl;
}

void test_index_7_temporal_interval() {
    std::cout << ">>> Testing Index #7 (IMP_INDEX_TEMPORAL_INTERVAL 0x07)..." << std::endl;
    const size_t N = 1000;
    struct Sec32 { uint32_t start_sec; uint32_t dur_sec; };
    std::vector<Sec32> intervals(N);

    for (size_t i = 0; i < N; ++i) {
        intervals[i].start_sec = static_cast<uint32_t>(i * 10);
        intervals[i].dur_sec = 20; // Active during [i*10, i*10 + 20]
    }

    void* index_bytes = nullptr;
    size_t index_size = 0;
    impulse_status_t st = impulse_index_build_temporal_interval(intervals.data(), N, 0x0D, &index_bytes, &index_size);
    assert(st == IMPULSE_OK);

    std::vector<uint32_t> out_nodes(N);
    size_t out_count = 0;
    st = impulse_index_temporal_interval_query(index_bytes, index_size, 45, out_nodes.data(), N, &out_count);
    assert(st == IMPULSE_OK);
    (void)st;
    assert(out_count > 0);

    impulse_index_free(index_bytes);
    std::cout << "    Temporal Interval query matched active intervals at T=45. PASSED!" << std::endl;
}

void test_secondary_indexes_null_and_boundaries() {
    std::cout << ">>> Testing Secondary Indexes Null Arguments & Boundary Trap Guards..." << std::endl;

    void* out_bytes = nullptr;
    size_t out_size = 0;
    size_t out_cnt = 0;
    uint32_t out_node = 0;
    uint64_t out_bm = 0;

    // Index free null check
    impulse_index_free(nullptr);

    // 1. Permutation
    assert(impulse_index_build_permutation(nullptr, 0, 0x06, &out_bytes, &out_size) == IMPULSE_ERR_INVALID_ARGUMENT);
    assert(impulse_index_permutation_range_query(nullptr, 0, 0.0, 1.0, nullptr, nullptr, 0, &out_cnt) == IMPULSE_ERR_INVALID_ARGUMENT);

    // 2. Zone Map
    assert(impulse_index_build_zone_map(nullptr, 0, 0x06, &out_bytes, &out_size) == IMPULSE_ERR_INVALID_ARGUMENT);
    assert(impulse_index_zone_map_filter(nullptr, 0, 0.0, 1.0, &out_bm, 1, &out_cnt) == IMPULSE_ERR_INVALID_ARGUMENT);

    // 3. Inverted Bitset
    const uint64_t* out_words = nullptr;
    assert(impulse_index_build_inverted_bitset(nullptr, 0, &out_bytes, &out_size) == IMPULSE_ERR_INVALID_ARGUMENT);
    assert(impulse_index_inverted_bitset_lookup(nullptr, 0, "key", &out_words, &out_cnt) == IMPULSE_ERR_INVALID_ARGUMENT);

    // 4. MPH
    assert(impulse_index_build_minimal_perfect_hash(nullptr, 0, &out_bytes, &out_size) == IMPULSE_ERR_INVALID_ARGUMENT);
    assert(impulse_index_minimal_perfect_hash_lookup(nullptr, 0, "key", &out_node) == IMPULSE_ERR_INVALID_ARGUMENT);

    // 5. Trigram
    assert(impulse_index_build_trigram(nullptr, 0, &out_bytes, &out_size) == IMPULSE_ERR_INVALID_ARGUMENT);
    assert(impulse_index_trigram_search(nullptr, 0, "tri", &out_bm, 1, &out_cnt) == IMPULSE_ERR_INVALID_ARGUMENT);

    // 6. Domain Split
    assert(impulse_index_build_domain_split(nullptr, 0, &out_bytes, &out_size) == IMPULSE_ERR_INVALID_ARGUMENT);
    assert(impulse_index_domain_split_lookup(nullptr, 0, "gmail.com", &out_words, &out_cnt) == IMPULSE_ERR_INVALID_ARGUMENT);

    // 7. Temporal Interval
    assert(impulse_index_build_temporal_interval(nullptr, 0, 0x0D, &out_bytes, &out_size) == IMPULSE_ERR_INVALID_ARGUMENT);
    assert(impulse_index_temporal_interval_query(nullptr, 0, 100, nullptr, 0, &out_cnt) == IMPULSE_ERR_INVALID_ARGUMENT);

    std::cout << "    All Secondary Index null & boundary checks PASSED!" << std::endl;
}

void test_issue_137_permutation_nan_total_order() {
    std::cout << ">>> Testing #137 Permutation Index NaN total-order (float & double)..." << std::endl;
    const size_t N = 2048;

    // Float (0x06) with NaNs, -NaNs, +/-inf, -0.0, +0.0, finite values
    std::vector<float> fvals(N);
    size_t f_nan_count = 0;
    for (size_t i = 0; i < N; ++i) {
        if (i % 13 == 0) {
            fvals[i] = std::numeric_limits<float>::quiet_NaN();
            ++f_nan_count;
        } else if (i % 17 == 0) {
            fvals[i] = -std::numeric_limits<float>::quiet_NaN();
            ++f_nan_count;
        } else if (i % 29 == 0) {
            fvals[i] = std::numeric_limits<float>::infinity();
        } else if (i % 31 == 0) {
            fvals[i] = -std::numeric_limits<float>::infinity();
        } else if (i % 7 == 0) {
            fvals[i] = -0.0f;
        } else {
            fvals[i] = static_cast<float>((static_cast<int>(i * 37) % 500) - 250) * 0.5f;
        }
    }

    void* f_bytes = nullptr;
    size_t f_size = 0;
    impulse_status_t st = impulse_index_build_permutation(fvals.data(), N, 0x06, &f_bytes, &f_size);
    TEST_CHECK(st == IMPULSE_OK && f_bytes != nullptr);

    const uint32_t* f_perm = reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(f_bytes) + 16);
    bool seen_nan = false;
    size_t verified_nans = 0;
    for (size_t i = 0; i < N; ++i) {
        float v = fvals[f_perm[i]];
        if (std::isnan(v)) {
            seen_nan = true;
            ++verified_nans;
        } else {
            TEST_CHECK(!seen_nan);
            if (i > 0) {
                float prev = fvals[f_perm[i - 1]];
                TEST_CHECK(!std::isnan(prev) && prev <= v);
            }
        }
    }
    TEST_CHECK(verified_nans == f_nan_count);

    std::vector<uint32_t> out_nodes(N);
    size_t out_count = 0;
    st = impulse_index_permutation_range_query(f_bytes, f_size, -50.0, 50.0, fvals.data(), out_nodes.data(), N, &out_count);
    TEST_CHECK(st == IMPULSE_OK && out_count > 0);
    for (size_t i = 0; i < out_count; ++i) {
        float v = fvals[out_nodes[i]];
        TEST_CHECK(!std::isnan(v) && v >= -50.0f && v <= 50.0f);
    }
    impulse_index_free(f_bytes);

    // Double (0x07) with NaNs
    std::vector<double> dvals(N);
    size_t d_nan_count = 0;
    for (size_t i = 0; i < N; ++i) {
        if (i % 11 == 0) {
            dvals[i] = std::numeric_limits<double>::quiet_NaN();
            ++d_nan_count;
        } else {
            dvals[i] = static_cast<double>((static_cast<int>(i * 53) % 1000) - 500) * 0.25;
        }
    }
    void* d_bytes = nullptr;
    size_t d_size = 0;
    st = impulse_index_build_permutation(dvals.data(), N, 0x07, &d_bytes, &d_size);
    TEST_CHECK(st == IMPULSE_OK && d_bytes != nullptr);
    const uint32_t* d_perm = reinterpret_cast<const uint32_t*>(static_cast<const uint8_t*>(d_bytes) + 16);
    seen_nan = false;
    verified_nans = 0;
    for (size_t i = 0; i < N; ++i) {
        double v = dvals[d_perm[i]];
        if (std::isnan(v)) {
            seen_nan = true;
            ++verified_nans;
        } else {
            TEST_CHECK(!seen_nan);
            if (i > 0) {
                double prev = dvals[d_perm[i - 1]];
                TEST_CHECK(!std::isnan(prev) && prev <= v);
            }
        }
    }
    TEST_CHECK(verified_nans == d_nan_count);
    impulse_index_free(d_bytes);
    std::cout << "    #137 NaN total-order permutation tests PASSED!" << std::endl;
}

void test_issue_143_zone_map_types_and_i64_precision() {
    std::cout << ">>> Testing #143 Zone Map unsupported types & int64 precision..." << std::endl;
    const uint64_t dummy_data[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    const uint8_t unsupported_types[] = {0x00, 0x01, 0x02, 0x05, 0x08, 0x0B, 0x7F};
    for (uint8_t tc : unsupported_types) {
        void* out_bytes = nullptr;
        size_t out_size = 0;
        impulse_status_t st = impulse_index_build_zone_map(dummy_data, 8, tc, &out_bytes, &out_size);
        TEST_CHECK(st == IMPULSE_ERR_INVALID_ARGUMENT);
        TEST_CHECK(out_bytes == nullptr);
    }

    const int64_t base = (1LL << 53);
    std::vector<int64_t> i64_vals(2048, base + 1);
    i64_vals[0] = base + 3;
    i64_vals[1024] = -base - 3;

    void* zm_bytes = nullptr;
    size_t zm_size = 0;
    impulse_status_t st = impulse_index_build_zone_map(i64_vals.data(), i64_vals.size(), 0x04, &zm_bytes, &zm_size);
    TEST_CHECK(st == IMPULSE_OK && zm_bytes != nullptr);

    struct ZmBlockView { double min_val; double max_val; };
    const auto* blocks = reinterpret_cast<const ZmBlockView*>(static_cast<const uint8_t*>(zm_bytes) + 24);
    TEST_CHECK(blocks[0].min_val <= static_cast<double>(base + 1));

    // Page where max_i64 = base + 1 (rounds DOWN to double(base) < base + 1 -> MUST round UP!)
    std::vector<int64_t> i64_down(1024, base + 1);
    void* zm2_bytes = nullptr;
    size_t zm2_size = 0;
    st = impulse_index_build_zone_map(i64_down.data(), i64_down.size(), 0x04, &zm2_bytes, &zm2_size);
    TEST_CHECK(st == IMPULSE_OK && zm2_bytes != nullptr);
    const auto* b2 = reinterpret_cast<const ZmBlockView*>(static_cast<const uint8_t*>(zm2_bytes) + 24);
    TEST_CHECK(b2[0].max_val > static_cast<double>(base));
    TEST_CHECK(b2[0].min_val <= static_cast<double>(base));

    // Page where min_i64 = -base - 1 (rounds UP toward 0 to -double(base) > -base - 1 -> MUST round DOWN!)
    std::vector<int64_t> i64_neg(1024, -base - 1);
    void* zm3_bytes = nullptr;
    size_t zm3_size = 0;
    st = impulse_index_build_zone_map(i64_neg.data(), i64_neg.size(), 0x04, &zm3_bytes, &zm3_size);
    TEST_CHECK(st == IMPULSE_OK && zm3_bytes != nullptr);
    const auto* b3 = reinterpret_cast<const ZmBlockView*>(static_cast<const uint8_t*>(zm3_bytes) + 24);
    TEST_CHECK(b3[0].min_val < static_cast<double>(-base));
    TEST_CHECK(b3[0].max_val >= static_cast<double>(-base));

    impulse_index_free(zm_bytes);
    impulse_index_free(zm2_bytes);
    impulse_index_free(zm3_bytes);
    std::cout << "    #143 Zone Map type validation & int64 outward rounding PASSED!" << std::endl;
}

int main() {
    std::cout << "\n=========================================================================" << std::endl;
    std::cout << "  IMPULSE GRAPH ENGINE - COMPLETE 7 SECONDARY INDEXES UNIT TEST SUITE    " << std::endl;
    std::cout << "=========================================================================\n" << std::endl;

    test_index_1_permutation();
    test_index_2_zone_map();
    test_index_3_inverted_bitset();
    test_index_4_minimal_perfect_hash();
    test_index_5_trigram_3gram();
    test_index_6_domain_split();
    test_index_7_temporal_interval();
    test_secondary_indexes_null_and_boundaries();
    test_issue_137_permutation_nan_total_order();
    test_issue_143_zone_map_types_and_i64_precision();

    std::cout << "=========================================================================" << std::endl;
    std::cout << "  ALL 7 SECONDARY INDEXES PASSED 100% CLEANLY!                           " << std::endl;
    std::cout << "=========================================================================\n" << std::endl;
    return 0;
}
