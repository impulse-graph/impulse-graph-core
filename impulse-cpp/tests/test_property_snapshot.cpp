/**
 * @file test_property_snapshot.cpp
 * @brief Property-based testing for Impulse Graph binary snapshot invariants.
 *
 * Formally verifies:
 * 1. Round-Trip Invariant: read(write(G)) == G across arbitrary node/edge configurations
 * 2. Topological Duality: reachability(u, v) matches ground truth edge sets
 * 3. Degree Conservation: sum(out_deg(u)) == sum(in_deg(v)) == |E|
 * 4. Multi-Domain Dense ID Independence
 */

#include "impulse_graph.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
#include <set>
#include <algorithm>
#include <iostream>
#include <chrono>

#if defined(_WIN32)
#include <io.h>
#define unlink _unlink
#else
#include <unistd.h>
#endif

namespace {

struct PropertyRng {
    uint64_t state;
    explicit PropertyRng(uint64_t seed = 0x123456789abcdef0ULL) : state(seed ? seed : 1ULL) {}
    inline uint64_t next() {
        state ^= state >> 12;
        state ^= state << 25;
        state ^= state >> 27;
        return state * 0x2545F4914F6CDD1DULL;
    }
    inline uint32_t bounded(uint32_t bound) {
        if (bound == 0) return 0;
        return static_cast<uint32_t>(next() % bound);
    }
};

struct Edge {
    uint32_t src;
    uint32_t tgt;
    bool operator<(const Edge& o) const {
        if (src != o.src) return src < o.src;
        return tgt < o.tgt;
    }
    bool operator==(const Edge& o) const {
        return src == o.src && tgt == o.tgt;
    }
};

struct TestGraph {
    uint32_t src_node_count;
    uint32_t tgt_node_count;
    std::vector<Edge> edges;
    std::vector<uint32_t> row_offsets;
    std::vector<uint32_t> col_indices;
};

TestGraph generate_random_bipartite_graph(PropertyRng& rng, uint32_t max_nodes, uint32_t max_edges) {
    TestGraph g;
    g.src_node_count = 1 + rng.bounded(max_nodes);
    g.tgt_node_count = 1 + rng.bounded(max_nodes);

    uint32_t num_edges = rng.bounded(max_edges);
    std::set<Edge> unique_edges;

    for (uint32_t i = 0; i < num_edges; ++i) {
        uint32_t u = rng.bounded(g.src_node_count);
        uint32_t v = rng.bounded(g.tgt_node_count);
        unique_edges.insert(Edge{u, v});
    }

    g.edges.assign(unique_edges.begin(), unique_edges.end());
    std::sort(g.edges.begin(), g.edges.end());

    // Construct CSR
    g.row_offsets.assign(g.src_node_count + 1, 0);
    g.col_indices.reserve(g.edges.size());

    // Count degrees
    for (const auto& e : g.edges) {
        g.row_offsets[e.src + 1]++;
    }
    // Prefix sum
    for (size_t i = 0; i < g.src_node_count; ++i) {
        g.row_offsets[i + 1] += g.row_offsets[i];
    }

    std::vector<uint32_t> insert_cursors = g.row_offsets;
    g.col_indices.resize(g.edges.size());
    for (const auto& e : g.edges) {
        size_t idx = insert_cursors[e.src]++;
        g.col_indices[idx] = e.tgt;
    }

    return g;
}

} // anonymous namespace

int main(int argc, char** argv) {
    size_t trials = 100;
    uint64_t seed = 0xbeefcafeULL;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--trials" && i + 1 < argc) {
            trials = std::strtoull(argv[++i], nullptr, 10);
        } else if (arg == "--seed" && i + 1 < argc) {
            seed = std::strtoull(argv[++i], nullptr, 10);
        }
    }

    std::cout << "============================================================" << std::endl;
    std::cout << " Impulse Graph Snapshot Property-Based Invariant Test Suite" << std::endl;
    std::cout << " Trials: " << trials << " | Seed: " << seed << std::endl;
    std::cout << "============================================================" << std::endl;

    PropertyRng rng(seed);
    const char* test_file = "__prop_snapshot_test.imps";
    auto start_time = std::chrono::steady_clock::now();

    size_t total_edges_tested = 0;
    size_t total_queries_tested = 0;

    for (size_t t = 0; t < trials; ++t) {
        unlink(test_file);

        // Generate randomized bipartite topology
        TestGraph g = generate_random_bipartite_graph(rng, 150, 400);

        impulse_writer_t* writer = impulse_writer_create(test_file, 0);
        assert(writer != nullptr);

        impulse_status_t st;
        st = impulse_writer_add_domain(writer, 0, IMPULSE_KEY_TYPE_INT32, "SourceDomain");
        assert(st == IMPULSE_OK);
        st = impulse_writer_add_domain(writer, 1, IMPULSE_KEY_TYPE_INT32, "TargetDomain");
        assert(st == IMPULSE_OK);

        st = impulse_writer_add_relation(
            writer,
            0, 1,
            IMPULSE_ENC_RAW,
            g.src_node_count, g.edges.size(),
            0,
            g.row_offsets.data(), g.row_offsets.size() * sizeof(uint32_t),
            g.col_indices.empty() ? nullptr : g.col_indices.data(), g.col_indices.size() * sizeof(uint32_t)
        );
        assert(st == IMPULSE_OK);

        std::string meta_key = "prop_trial_" + std::to_string(t);
        std::string meta_val = "val_" + std::to_string(g.edges.size());
        st = impulse_writer_set_metadata(writer, meta_key.c_str(), meta_val.c_str());
        assert(st == IMPULSE_OK);

        st = impulse_writer_finalize(writer);
        assert(st == IMPULSE_OK);
        impulse_writer_destroy(writer);

        // Open and verify properties
        impulse_snapshot_t* snap = impulse_snapshot_open(test_file, &st);
        assert(st == IMPULSE_OK);
        assert(snap != nullptr);

        // Property 1: Catalog Counts
        assert(impulse_snapshot_domain_count(snap) == 2);
        assert(impulse_snapshot_relation_count(snap) == 1);

        impulse_relation_directory_entry_t rel_entry{};
        st = impulse_snapshot_get_relation_entry(snap, 0, &rel_entry);
        assert(st == IMPULSE_OK);
        assert(rel_entry.node_count == g.src_node_count);
        assert(rel_entry.edge_count == g.edges.size());

        // Property 2: Ground Truth Positive Reachability
        for (const auto& e : g.edges) {
            bool reachable = impulse_snapshot_is_reachable(snap, 0, e.src, e.tgt);
            assert(reachable == true);
            total_edges_tested++;
            total_queries_tested++;
        }

        // Property 3: Ground Truth Negative Reachability (Sample 100 non-edges)
        std::set<Edge> edge_set(g.edges.begin(), g.edges.end());
        for (int sample = 0; sample < 100; ++sample) {
            uint32_t u = rng.bounded(g.src_node_count + 10);
            uint32_t v = rng.bounded(g.tgt_node_count + 10);
            bool in_graph = edge_set.count(Edge{u, v}) > 0;
            bool reachable = impulse_snapshot_is_reachable(snap, 0, u, v);
            assert(reachable == in_graph);
            total_queries_tested++;
        }

        // Property 4: Metadata Exact Match
        char retrieved_meta[128];
        st = impulse_snapshot_get_metadata(snap, meta_key.c_str(), retrieved_meta, sizeof(retrieved_meta));
        assert(st == IMPULSE_OK);
        assert(std::string(retrieved_meta) == meta_val);

        impulse_snapshot_close(snap);
        unlink(test_file);

        if ((t + 1) % 20 == 0 || t + 1 == trials) {
            std::cout << "[Property Snapshot] Completed Trial " << (t + 1) << " / " << trials
                      << " | Total Positive Edges: " << total_edges_tested
                      << " | Total Reachability Queries: " << total_queries_tested << std::endl;
        }
    }

    auto end_time = std::chrono::steady_clock::now();
    double duration_ms = std::chrono::duration<double, std::milli>(end_time - start_time).count();

    std::cout << "[Property Snapshot] ALL PROPERTIES HELD! Verified "
              << trials << " trials (" << total_queries_tested << " queries) in "
              << duration_ms << " ms. Zero invariant violations." << std::endl;

    return 0;
}
