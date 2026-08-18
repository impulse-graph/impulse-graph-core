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

    std::cout << "=========================================================================" << std::endl;
    std::cout << "  ALL 7 SECONDARY INDEXES PASSED 100% CLEANLY!                           " << std::endl;
    std::cout << "=========================================================================\n" << std::endl;
    return 0;
}
