/**
 * @file bench_prefetch_distance.cpp
 * @brief Empirical Benchmark for Software Hardware Prefetch Distances & Ablation.
 *
 * Compares execution latencies across prefetch strides:
 *  - Distance = 0  (Disabled, rely solely on CPU hardware prefetcher)
 *  - Distance = 2  (8 bytes / 0.125 cachelines)
 *  - Distance = 4  (16 bytes / 0.25 cachelines)
 *  - Distance = 8  (32 bytes / 0.5 cachelines)
 *  - Distance = 16 (64 bytes / 1.0 cachelines)
 *  - Distance = 32 (128 bytes / 2.0 cachelines)
 */

#include "impulse_vm.h"
#include "impulse_graph.h"

#include <iostream>
#include <vector>
#include <chrono>
#include <numeric>
#include <algorithm>
#include <iomanip>
#include <cstring>
#include <cstdint>

struct BenchmarkGraph {
    std::vector<uint32_t> offsets;
    std::vector<uint32_t> targets;
    uint32_t num_nodes;
    uint32_t num_edges;
};

// Generate power-law graph with random target destinations to stress memory cache
static BenchmarkGraph generate_test_graph(uint32_t num_nodes, uint32_t avg_degree) {
    BenchmarkGraph g;
    g.num_nodes = num_nodes;
    g.offsets.resize(num_nodes + 1, 0);

    // Random non-sequential target patterns simulating real sparse graphs
    std::vector<uint32_t> raw_targets;
    uint32_t cur_offset = 0;
    uint32_t seed = 42;

    auto lcg = [&seed]() {
        seed = seed * 1664525u + 1013904223u;
        return seed;
    };

    for (uint32_t i = 0; i < num_nodes; ++i) {
        g.offsets[i] = cur_offset;
        uint32_t deg = 1 + (lcg() % (avg_degree * 2));
        for (uint32_t d = 0; d < deg; ++d) {
            raw_targets.push_back(lcg() % num_nodes);
        }
        cur_offset += deg;
    }
    g.offsets[num_nodes] = cur_offset;
    g.targets = std::move(raw_targets);
    g.num_edges = static_cast<uint32_t>(g.targets.size());
    return g;
}

template<int DISTANCE, bool PREFETCH_BITSET>
static inline void traverse_csr_prefetch(
    const uint32_t* offsets,
    const uint32_t* targets,
    uint32_t node_count,
    const uint64_t* src_words,
    size_t words_per_bitset,
    uint64_t* dst_words
) {
    for (size_t w = 0; w < words_per_bitset; ++w) {
        uint64_t word = src_words[w];
        while (word) {
            int bit = std::countr_zero(word);
            uint64_t u = w * 64 + bit;
            word &= word - 1;
            if (u < node_count) {
                uint32_t start = offsets[u];
                uint32_t end   = offsets[u + 1];
                for (uint32_t i = start; i < end; ++i) {
                    if constexpr (DISTANCE > 0) {
                        if (i + DISTANCE < end) {
                            __builtin_prefetch(&targets[i + DISTANCE], 0, 1);
                        }
                    }
                    if constexpr (PREFETCH_BITSET && DISTANCE > 0) {
                        if (i + (DISTANCE / 2) < end) {
                            uint32_t tgt_lookahead = targets[i + (DISTANCE / 2)];
                            __builtin_prefetch(&dst_words[tgt_lookahead >> 6], 1, 3);
                        }
                    }
                    uint32_t tgt = targets[i];
                    dst_words[tgt >> 6] |= (1ULL << (tgt & 63));
                }
            }
        }
    }
}

template<int DISTANCE, bool PREFETCH_BITSET>
static double benchmark_config(
    const BenchmarkGraph& g,
    const std::vector<uint64_t>& input_frontier,
    size_t words_per_bitset,
    int iterations,
    const char* label
) {
    std::vector<uint64_t> dst_words(words_per_bitset, 0);

    // Warmup
    for (int i = 0; i < 200; ++i) {
        std::fill(dst_words.begin(), dst_words.end(), 0);
        traverse_csr_prefetch<DISTANCE, PREFETCH_BITSET>(
            g.offsets.data(), g.targets.data(), g.num_nodes,
            input_frontier.data(), words_per_bitset, dst_words.data()
        );
    }

    std::vector<double> latencies;
    latencies.reserve(iterations);

    for (int i = 0; i < iterations; ++i) {
        std::fill(dst_words.begin(), dst_words.end(), 0);
        auto t0 = std::chrono::high_resolution_clock::now();
        traverse_csr_prefetch<DISTANCE, PREFETCH_BITSET>(
            g.offsets.data(), g.targets.data(), g.num_nodes,
            input_frontier.data(), words_per_bitset, dst_words.data()
        );
        auto t1 = std::chrono::high_resolution_clock::now();
        latencies.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
    }

    std::sort(latencies.begin(), latencies.end());
    double mean = std::accumulate(latencies.begin(), latencies.end(), 0.0) / latencies.size();
    double p50 = latencies[latencies.size() / 2];
    double p99 = latencies[static_cast<size_t>(latencies.size() * 0.99)];

    std::cout << std::left << std::setw(38) << label
              << " | Mean: " << std::right << std::setw(7) << std::fixed << std::setprecision(3) << mean << " µs"
              << " | P50: "  << std::right << std::setw(7) << std::fixed << std::setprecision(3) << p50  << " µs"
              << " | P99: "  << std::right << std::setw(7) << std::fixed << std::setprecision(3) << p99  << " µs"
              << " | QPS: "  << std::right << std::setw(8) << static_cast<uint64_t>(1000000.0 / mean)
              << "\n";
    return mean;
}

template<int DISTANCE>
static inline void traverse_2hop_prefetch(
    const uint32_t* offsets1, const uint32_t* targets1, uint32_t node_count1,
    const uint32_t* offsets2, const uint32_t* targets2, uint32_t node_count2,
    const uint64_t* src_words, size_t words_per_bitset,
    uint64_t* dst_words
) {
    for (size_t w = 0; w < words_per_bitset; ++w) {
        uint64_t word = src_words[w];
        while (word) {
            int bit = std::countr_zero(word);
            uint64_t u = w * 64 + bit;
            word &= word - 1;
            if (u < node_count1) {
                uint32_t start1 = offsets1[u];
                uint32_t end1   = offsets1[u + 1];
                for (uint32_t i = start1; i < end1; ++i) {
                    if constexpr (DISTANCE > 0) {
                        if (i + DISTANCE < end1) {
                            uint32_t next_v = targets1[i + DISTANCE];
                            if (next_v < node_count2) {
                                __builtin_prefetch(&offsets2[next_v], 0, 1);
                            }
                        }
                    }
                    uint32_t v = targets1[i];
                    if (v < node_count2) {
                        uint32_t start2 = offsets2[v];
                        uint32_t end2   = offsets2[v + 1];
                        for (uint32_t j = start2; j < end2; ++j) {
                            uint32_t tgt2 = targets2[j];
                            dst_words[tgt2 >> 6] |= (1ULL << (tgt2 & 63));
                        }
                    }
                }
            }
        }
    }
}

template<int DISTANCE>
static double benchmark_2hop_config(
    const BenchmarkGraph& g1,
    const BenchmarkGraph& g2,
    const std::vector<uint64_t>& input_frontier,
    size_t words_per_bitset,
    int iterations,
    const char* label
) {
    std::vector<uint64_t> dst_words(words_per_bitset, 0);

    for (int i = 0; i < 200; ++i) {
        std::fill(dst_words.begin(), dst_words.end(), 0);
        traverse_2hop_prefetch<DISTANCE>(
            g1.offsets.data(), g1.targets.data(), g1.num_nodes,
            g2.offsets.data(), g2.targets.data(), g2.num_nodes,
            input_frontier.data(), words_per_bitset, dst_words.data()
        );
    }

    std::vector<double> latencies;
    latencies.reserve(iterations);

    for (int i = 0; i < iterations; ++i) {
        std::fill(dst_words.begin(), dst_words.end(), 0);
        auto t0 = std::chrono::high_resolution_clock::now();
        traverse_2hop_prefetch<DISTANCE>(
            g1.offsets.data(), g1.targets.data(), g1.num_nodes,
            g2.offsets.data(), g2.targets.data(), g2.num_nodes,
            input_frontier.data(), words_per_bitset, dst_words.data()
        );
        auto t1 = std::chrono::high_resolution_clock::now();
        latencies.push_back(std::chrono::duration<double, std::micro>(t1 - t0).count());
    }

    std::sort(latencies.begin(), latencies.end());
    double mean = std::accumulate(latencies.begin(), latencies.end(), 0.0) / latencies.size();
    double p50 = latencies[latencies.size() / 2];
    double p99 = latencies[static_cast<size_t>(latencies.size() * 0.99)];

    std::cout << std::left << std::setw(38) << label
              << " | Mean: " << std::right << std::setw(7) << std::fixed << std::setprecision(3) << mean << " µs"
              << " | P50: "  << std::right << std::setw(7) << std::fixed << std::setprecision(3) << p50  << " µs"
              << " | P99: "  << std::right << std::setw(7) << std::fixed << std::setprecision(3) << p99  << " µs"
              << " | QPS: "  << std::right << std::setw(8) << static_cast<uint64_t>(1000000.0 / mean)
              << "\n";
    return mean;
}

int main() {
    std::cout << "=========================================================================================================\n";
    std::cout << "               IMPULSE GRAPH HARDWARE PREFETCH DISTANCE & ABLATION BENCHMARK                            \n";
    std::cout << "=========================================================================================================\n";
    std::cout << " Testing hardware cacheline stride tuning (0 to 32 elements / 0B to 128B lookahead)                     \n";
    std::cout << " Target Graph: 100,000 Nodes | 3,000,000 Edges (Non-sequential memory stress pattern)                   \n";
    std::cout << "=========================================================================================================\n\n";

    uint32_t num_nodes = 100000;
    uint32_t avg_deg = 30;
    auto g1 = generate_test_graph(num_nodes, avg_deg);
    auto g2 = generate_test_graph(num_nodes, 2); // 2-hop low multiplicity

    size_t words_per_bitset = (num_nodes + 63) / 64;
    std::vector<uint64_t> input_frontier(words_per_bitset, 0);

    // Populate active frontier (5% random active nodes = 5,000 nodes)
    for (uint32_t i = 0; i < num_nodes; i += 20) {
        input_frontier[i >> 6] |= (1ULL << (i & 63));
    }

    int iters = 1000;

    std::cout << "--- 1. Single-Hop CSR Graph Traversal (Contiguous Target Reads) ---\n";
    benchmark_config<0, false>(g1, input_frontier, words_per_bitset, iters, "Distance = 0 (Pure HW Prefetcher)");
    benchmark_config<2, false>(g1, input_frontier, words_per_bitset, iters, "Distance = 2 (8B lookahead, targets)");
    benchmark_config<4, false>(g1, input_frontier, words_per_bitset, iters, "Distance = 4 (16B lookahead, targets)");
    benchmark_config<8, false>(g1, input_frontier, words_per_bitset, iters, "Distance = 8 (32B lookahead, targets)");
    benchmark_config<16, false>(g1, input_frontier, words_per_bitset, iters, "Distance = 16 (64B / 1 cacheline)");
    benchmark_config<32, false>(g1, input_frontier, words_per_bitset, iters, "Distance = 32 (128B / 2 cachelines)");
    
    std::cout << "\n--- 2. Dual Prefetch (Targets Array Read + BitSet Word Write) ---\n";
    benchmark_config<4, true>(g1, input_frontier, words_per_bitset, iters, "Distance = 4 (Targets + BitSet Write)");
    benchmark_config<8, true>(g1, input_frontier, words_per_bitset, iters, "Distance = 8 (Targets + BitSet Write)");
    benchmark_config<16, true>(g1, input_frontier, words_per_bitset, iters, "Distance = 16 (Targets + BitSet Write)");
    benchmark_config<32, true>(g1, input_frontier, words_per_bitset, iters, "Distance = 32 (Targets + BitSet Write)");

    std::cout << "\n--- 3. Two-Hop Indirect Traversal (Indirect Offsets Table Lookahead) ---\n";
    double hop_base = benchmark_2hop_config<0>(g1, g2, input_frontier, words_per_bitset, iters, "Distance = 0 (No Software Prefetch)");
    benchmark_2hop_config<2>(g1, g2, input_frontier, words_per_bitset, iters, "Distance = 2 (8B Lookahead Hop2)");
    double hop_opt = benchmark_2hop_config<4>(g1, g2, input_frontier, words_per_bitset, iters, "Distance = 4 (16B Lookahead Hop2)");
    benchmark_2hop_config<8>(g1, g2, input_frontier, words_per_bitset, iters, "Distance = 8 (32B Lookahead Hop2)");
    benchmark_2hop_config<16>(g1, g2, input_frontier, words_per_bitset, iters, "Distance = 16 (64B Lookahead Hop2)");

    std::cout << "=========================================================================================================\n";
    std::cout << " 2-Hop Indirect Speedup with Distance=4: " << std::fixed << std::setprecision(2) << (hop_base / hop_opt) << "x\n";
    std::cout << "=========================================================================================================\n";

    return 0;
}
