/**
 * @file bench_cypher_all_diseases.cpp
 * @brief C++ Exhaustive All-Diseases & All-Compounds Batch Screening Benchmark from raw openCypher queries.
 */

#include "impulse_cypher.hpp"
#include "impulse_vm.h"
#include "impulse_graph.h"

#include <iostream>
#include <vector>
#include <chrono>
#include <numeric>
#include <algorithm>
#include <iomanip>
#include <cstdio>
#include <cassert>

using namespace impulse::compiler;

static const char* HETIONET_PATH = "/Users/jesse/impulse/datasets/hetionet/hetionet.v09.imps";

static void run_all_seeds_cypher_benchmark(
    const std::string& title,
    const std::string& dataset,
    const std::string& cypher_query,
    const impulse_snapshot_t* snapshot,
    impulse_vm_context_t* ctx,
    const GraphCatalog& catalog,
    uint16_t seed_rel_idx,
    bool is_reverse
) {
    const uint32_t* offsets = nullptr;
    const uint32_t* targets = nullptr;
    uint64_t node_count = 0;
    uint64_t edge_count = 0;
    if (is_reverse) {
        impulse_snapshot_get_relation_csc_buffers(snapshot, seed_rel_idx, &offsets, &targets, &node_count, &edge_count);
    } else {
        impulse_snapshot_get_relation_buffers(snapshot, seed_rel_idx, &offsets, &targets, &node_count, &edge_count);
    }
    if (!offsets) return;

    std::vector<uint32_t> active_seeds;
    for (uint32_t i = 0; i < node_count; ++i) {
        if (offsets[i + 1] > offsets[i]) {
            active_seeds.push_back(i);
        }
    }

    auto compilation = CypherCompiler::compile(cypher_query);
    auto compiled = ImpulseCompiler::compile(compilation.ast, &catalog);

    // Warmup
    for (size_t i = 0; i < std::min<size_t>(100, active_seeds.size()); ++i) {
        impulse_vm_state_t state{};
        state.query_context = ctx;
        impulse_vm_execute(compiled.data(), compiled.instruction_count(), &state, active_seeds[i]);
        if (state.register_types[compiled.result_register] == TYPE_BITSET_HANDLE) {
            impulse_vm_context_release_bitset(ctx, state.registers[compiled.result_register]);
        }
    }

    // Benchmark across all active seeds
    std::vector<double> latencies_us(active_seeds.size());
    double total_us = 0;
    auto t0_total = std::chrono::high_resolution_clock::now();

    for (size_t idx = 0; idx < active_seeds.size(); ++idx) {
        uint32_t seed = active_seeds[idx];
        auto t0 = std::chrono::high_resolution_clock::now();
        impulse_vm_state_t state{};
        state.query_context = ctx;
        impulse_vm_execute(compiled.data(), compiled.instruction_count(), &state, seed);
        auto t1 = std::chrono::high_resolution_clock::now();

        double dur = std::chrono::duration<double, std::micro>(t1 - t0).count();
        latencies_us[idx] = dur;
        total_us += dur;

        if (state.register_types[compiled.result_register] == TYPE_BITSET_HANDLE) {
            impulse_vm_context_release_bitset(ctx, state.registers[compiled.result_register]);
        }
    }
    auto t1_total = std::chrono::high_resolution_clock::now();
    double total_wall_ms = std::chrono::duration<double, std::milli>(t1_total - t0_total).count();

    std::sort(latencies_us.begin(), latencies_us.end());
    size_t N = active_seeds.size();
    double mean_us = total_us / N;
    double p50_us = latencies_us[N * 0.50];
    double p90_us = latencies_us[N * 0.90];
    double p99_us = latencies_us[N * 0.99];
    double min_us = latencies_us.front();
    double max_us = latencies_us.back();
    uint64_t screens_per_sec = static_cast<uint64_t>(N / (total_wall_ms / 1000.0));

    std::cout << "---------------------------------------------------------------------------------------------------------\n";
    std::cout << "  " << title << "\n";
    std::cout << "  Dataset: " << dataset << "\n";
    std::cout << "  Total Cohort Size Screened: " << N << " entities\n";
    std::cout << "---------------------------------------------------------------------------------------------------------\n";
    std::cout << "  Total Whole-Dataset Time:   " << std::fixed << std::setprecision(3) << total_wall_ms << " ms\n";
    std::cout << "  Mean Latency / Entity:      " << std::fixed << std::setprecision(3) << mean_us << " µs\n";
    std::cout << "  P50 (Median) Latency:       " << std::fixed << std::setprecision(3) << p50_us << " µs\n";
    std::cout << "  P90 Latency:                " << std::fixed << std::setprecision(3) << p90_us << " µs\n";
    std::cout << "  P99 (Hub Nodes) Latency:    " << std::fixed << std::setprecision(3) << p99_us << " µs\n";
    std::cout << "  Min / Max (Worst Hub):      " << std::fixed << std::setprecision(3) << min_us << " µs / " << max_us << " µs\n";
    std::cout << "  Screening Throughput:       " << screens_per_sec << " complete cohort screens / second\n\n";
}

int main() {
    std::cout << "=========================================================================================================\n";
    std::cout << "      EXHAUSTIVE ALL-DISEASES OPENCYPHER BATCH SCREENING (C++20 ZERO-COPY NATIVE KERNEL)                 \n";
    std::cout << "=========================================================================================================\n\n";

    impulse_status_t st_het = IMPULSE_OK;
    impulse_snapshot_t* het_snap = impulse_snapshot_open(HETIONET_PATH, &st_het);
    if (st_het == IMPULSE_OK && het_snap) {
        auto* het_ctx = impulse_vm_context_create(het_snap);
        GraphCatalog catalog;
        catalog.register_relation("DaG", 7, 1.0);
        catalog.register_relation("GpPW", 20, 1.0);
        catalog.register_relation("CbG", 19, 1.0);
        catalog.register_relation("DdG", 17, 1.0);
        catalog.register_relation("CuG", 22, 1.0);

        // Q1 All Diseases
        run_all_seeds_cypher_benchmark(
            "Cypher Q1: 4-Hop All-Diseases Drug Repurposing",
            "Hetionet v1.0",
            "MATCH (d:Disease)-[:DaG]->(g1:Gene)-[:GpPW]->(p:Pathway)<-[:GpPW]-(g2:Gene)<-[:CbG]-(c:Compound) WHERE d.id = $diseaseId RETURN c",
            het_snap,
            het_ctx,
            catalog,
            7, // DaG
            false
        );

        // Q2 All Diseases
        run_all_seeds_cypher_benchmark(
            "Cypher Q2: 2-Hop All-Diseases Expression Counteraction (MoA)",
            "Hetionet v1.0",
            "MATCH (d:Disease)-[:DdG]->(g:Gene)<-[:CuG]-(c:Compound) WHERE d.id = $diseaseId RETURN c",
            het_snap,
            het_ctx,
            catalog,
            17, // DdG
            false
        );

        impulse_vm_context_destroy(het_ctx);
        impulse_snapshot_close(het_snap);
    }

    return 0;
}
