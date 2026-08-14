/**
 * @file bench_cypher_medical.cpp
 * @brief Benchmark running declarative openCypher queries compiled to ImpScheme & impOps in C++.
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
static const char* DRKG_PATH = "/Users/jesse/impulse/datasets/drkg/drkg.v09.imps";

static const int WARMUP_ITERS = 10000;
static const int BENCHMARK_ITERS = 20000;

static void run_cypher_benchmark(
    const std::string& title,
    const std::string& dataset,
    const std::string& cypher_query,
    impulse_snapshot_t* snapshot,
    impulse_vm_context_t* ctx,
    const GraphCatalog& catalog,
    uint32_t seed_node
) {
    auto compilation = CypherCompiler::compile(cypher_query);
    auto compiled = ImpulseCompiler::compile(compilation.ast, &catalog);

    // Warmup
    for (int i = 0; i < WARMUP_ITERS; ++i) {
        impulse_vm_state_t state{};
        state.query_context = ctx;
        impulse_vm_execute(compiled.data(), compiled.instruction_count(), &state, seed_node);
        if (state.register_types[compiled.result_register] == TYPE_BITSET_HANDLE) {
            impulse_vm_context_release_bitset(ctx, state.registers[compiled.result_register]);
        }
    }

    // Benchmark
    std::vector<double> latencies_us(BENCHMARK_ITERS);
    double total_us = 0;
    auto t0_total = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < BENCHMARK_ITERS; ++i) {
        auto t0 = std::chrono::high_resolution_clock::now();
        impulse_vm_state_t state{};
        state.query_context = ctx;
        impulse_vm_execute(compiled.data(), compiled.instruction_count(), &state, seed_node);
        auto t1 = std::chrono::high_resolution_clock::now();

        double dur = std::chrono::duration<double, std::micro>(t1 - t0).count();
        latencies_us[i] = dur;
        total_us += dur;

        if (state.register_types[compiled.result_register] == TYPE_BITSET_HANDLE) {
            impulse_vm_context_release_bitset(ctx, state.registers[compiled.result_register]);
        }
    }
    auto t1_total = std::chrono::high_resolution_clock::now();
    double total_wall_sec = std::chrono::duration<double>(t1_total - t0_total).count();

    std::sort(latencies_us.begin(), latencies_us.end());
    double mean_us = total_us / BENCHMARK_ITERS;
    double p50_us = latencies_us[BENCHMARK_ITERS * 0.50];
    double p90_us = latencies_us[BENCHMARK_ITERS * 0.90];
    double p99_us = latencies_us[BENCHMARK_ITERS * 0.99];
    double min_us = latencies_us.front();
    double max_us = latencies_us.back();
    uint64_t qps = static_cast<uint64_t>(BENCHMARK_ITERS / total_wall_sec);

    std::cout << "---------------------------------------------------------------------------------------------------------\n";
    std::cout << "  " << title << "\n";
    std::cout << "  Dataset: " << dataset << "\n";
    std::cout << "  Raw openCypher Query:\n";
    std::cout << "    " << cypher_query << "\n";
    std::cout << "---------------------------------------------------------------------------------------------------------\n";
    std::cout << "  Mean Latency:          " << std::fixed << std::setprecision(3) << mean_us << " µs\n";
    std::cout << "  P50 (Median) Latency:  " << std::fixed << std::setprecision(3) << p50_us << " µs\n";
    std::cout << "  P90 Latency:           " << std::fixed << std::setprecision(3) << p90_us << " µs\n";
    std::cout << "  P99 Latency:           " << std::fixed << std::setprecision(3) << p99_us << " µs\n";
    std::cout << "  Min / Max Latency:     " << std::fixed << std::setprecision(3) << min_us << " µs / " << max_us << " µs\n";
    std::cout << "  Execution Throughput:  " << qps << " queries / second\n\n";
}

int main() {
    std::cout << "=========================================================================================================\n";
    std::cout << "               DECLARATIVE OPENCYPHER MEDICAL KNOWLEDGE GRAPH BENCHMARK (C++20)                          \n";
    std::cout << "=========================================================================================================\n";
    std::cout << " Frontend: openCypher (MATCH ... WHERE ... RETURN) -> ImpScheme AST -> 7-Stage Compiler -> impOps ISA   \n";
    std::cout << " Target Hardware: Apple Silicon M-Series | C++20 Zero-Copy Native Kernel                                \n";
    std::cout << " Iterations: 20,000 runs per query (10,000 warmup runs)                                                 \n";
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
        catalog.register_relation("CtD", 9, 1.0);
        catalog.register_relation("CrC", 10, 1.0);
        catalog.register_relation("DlA", 14, 1.0);
        catalog.register_relation("AeG", 0, 1.0);

        // Q1
        run_cypher_benchmark(
            "Cypher Q1: 4-Hop Pathway Drug Repurposing (CbGpPWpD)",
            "Hetionet v1.0",
            "MATCH (d:Disease)-[:DaG]->(g1:Gene)-[:GpPW]->(p:Pathway)<-[:GpPW]-(g2:Gene)<-[:CbG]-(c:Compound) WHERE d.id = $diseaseId RETURN c",
            het_snap,
            het_ctx,
            catalog,
            12
        );

        // Q2
        run_cypher_benchmark(
            "Cypher Q2: 2-Hop Expression Counteraction / MoA (CuG<rGaD)",
            "Hetionet v1.0",
            "MATCH (d:Disease)-[:DdG]->(g:Gene)<-[:CuG]-(c:Compound) WHERE d.id = $diseaseId RETURN c",
            het_snap,
            het_ctx,
            catalog,
            12
        );

        // Q3
        run_cypher_benchmark(
            "Cypher Q3: 2-Hop Chemical Resemblance Transitivity (CrCtD)",
            "Hetionet v1.0",
            "MATCH (d:Disease)<-[:CtD]-(c1:Compound)-[:CrC]->(c2:Compound) WHERE d.id = $diseaseId RETURN c2",
            het_snap,
            het_ctx,
            catalog,
            12
        );

        // Q4
        run_cypher_benchmark(
            "Cypher Q4: 3-Hop Shared Anatomy Pathology & Target Discovery (DlAeGbC)",
            "Hetionet v1.0",
            "MATCH (d:Disease)-[:DlA]->(a:Anatomy)-[:AeG]->(g:Gene)<-[:CbG]-(c:Compound) WHERE d.id = $diseaseId RETURN c",
            het_snap,
            het_ctx,
            catalog,
            12
        );

        impulse_vm_context_destroy(het_ctx);
        impulse_snapshot_close(het_snap);
    }

    impulse_status_t st_drkg = IMPULSE_OK;
    impulse_snapshot_t* drkg_snap = impulse_snapshot_open(DRKG_PATH, &st_drkg);
    if (st_drkg == IMPULSE_OK && drkg_snap) {
        auto* drkg_ctx = impulse_vm_context_create(drkg_snap);
        GraphCatalog catalog;
        catalog.register_relation("DISGENET::da", 0, 1.0);
        catalog.register_relation("STRING::interacts_with", 0, 1.0);
        catalog.register_relation("DRUGBANK::target", 0, 1.0);
        catalog.register_relation("DRUGBANK::ddi_interactor_in", 0, 1.0);
        catalog.register_relation("GNBR::C", 0, 1.0);

        // Q5
        run_cypher_benchmark(
            "Cypher Q5: 3-Hop Precision Oncology Cascades (DRKG Multi-Source)",
            "DRKG (DisGeNET + STRING + DrugBank)",
            "MATCH (d:Disease)-[:`DISGENET::da`]->(g1:Gene)-[:`STRING::interacts_with`]->(g2:Gene)<-[:`DRUGBANK::target`]-(c:Compound) WHERE d.id = $diseaseId RETURN c",
            drkg_snap,
            drkg_ctx,
            catalog,
            50
        );

        // Q6
        run_cypher_benchmark(
            "Cypher Q6: 2-Hop Polypharmacology Adverse DDI Warning (DRKG)",
            "DRKG (DrugBank DDI + GNBR Side Effects)",
            "MATCH (c1:Compound)-[:`DRUGBANK::ddi_interactor_in`]->(c2:Compound)-[:`GNBR::C`]->(s:SideEffect) WHERE c1.id = $compoundId RETURN s",
            drkg_snap,
            drkg_ctx,
            catalog,
            100
        );

        impulse_vm_context_destroy(drkg_ctx);
        impulse_snapshot_close(drkg_snap);
    }

    return 0;
}
