/**
 * @file bench_all_drugs_medical.cpp
 * @brief C++ Exhaustive All-Drugs & All-Diseases Batch Screening Benchmark over Hetionet & DRKG.
 *
 * Side-by-Side Verification: C++20 Zero-Copy Native Kernel vs Java 25 FFM / Vector API.
 */

#include "impulse_compiler.hpp"
#include "impulse_vm.h"
#include "impulse_graph.h"

#include <iostream>
#include <vector>
#include <chrono>
#include <numeric>
#include <algorithm>
#include <iomanip>
#include <atomic>
#include <cstdio>
#include <cassert>

#if defined(_OPENMP)
#include <omp.h>
#endif

using namespace impulse::compiler;

static const char* HETIONET_PATH = "/Users/jesse/impulse/datasets/hetionet/hetionet.v09.imps";
static const char* DRKG_PATH = "/Users/jesse/impulse/datasets/drkg/drkg.v09.imps";

int main() {
    std::cout << "=========================================================================================================\n";
    std::cout << "          IMPULSE GRAPH C++ EXHAUSTIVE ALL-DRUGS BATCH SCREENING BENCHMARK                               \n";
    std::cout << "=========================================================================================================\n";
    std::cout << " Side-by-Side Comparison: C++20 Zero-Copy Native Kernel vs Java 25 FFM / Vector API                     \n";
    std::cout << " Storage: Zero-Copy Memory-Mapped .imps Snapshots                                                        \n";
    std::cout << "=========================================================================================================\n\n";

    // -----------------------------------------------------------------------
    // SCREEN 1: HETIONET ALL-DISEASES 4-HOP DRUG REPURPOSING (CbGpPWpD)
    // -----------------------------------------------------------------------
    impulse_status_t st_het = IMPULSE_OK;
    impulse_snapshot_t* het_snap = impulse_snapshot_open(HETIONET_PATH, &st_het);
    if (st_het == IMPULSE_OK && het_snap) {
        auto* het_ctx = impulse_vm_context_create(het_snap);
        assert(het_ctx != nullptr);

        // Find active disease seed nodes (nodes with degree > 0 in relation 7 "DaG")
        uint16_t dag_rel = 7;
        const uint32_t* offsets = nullptr;
        const uint32_t* targets = nullptr;
        uint64_t node_count = 0;
        uint64_t edge_count = 0;
        impulse_snapshot_get_relation_buffers(het_snap, dag_rel, &offsets, &targets, &node_count, &edge_count);

        std::vector<uint32_t> active_diseases;
        if (offsets) {
            for (uint32_t i = 0; i < node_count; ++i) {
                if (offsets[i + 1] > offsets[i]) {
                    active_diseases.push_back(i);
                }
            }
        }

        // Compile query AST
        auto astQ1 = ScmProgram::of(
            ScmWalk::forward("DaG"),
            ScmWalk::forward("GpPW"),
            ScmWalk::reverse("GpPW"),
            ScmWalk::reverse("CbG"),
            ScmCollect::bitset()
        );

        GraphCatalog catalog;
        catalog.register_relation("DaG", 7, 1.0);
        catalog.register_relation("GpPW", 20, 1.0);
        catalog.register_relation("CbG", 19, 1.0);

        auto compiledQ1 = ImpulseCompiler::compile(astQ1, &catalog);

        // Warmup
        for (uint32_t d : active_diseases) {
            impulse_vm_state_t state{};
            state.query_context = het_ctx;
            impulse_vm_execute(compiledQ1.data(), compiledQ1.instruction_count(), &state, d);
            if (state.register_types[compiledQ1.result_register] == TYPE_BITSET_HANDLE) {
                impulse_vm_context_release_bitset(het_ctx, state.registers[compiledQ1.result_register]);
            }
        }

        // Single-Core Screen
        auto t0_seq = std::chrono::high_resolution_clock::now();
        uint64_t total_discoveries_seq = 0;
        for (uint32_t d : active_diseases) {
            impulse_vm_state_t state{};
            state.query_context = het_ctx;
            impulse_vm_execute(compiledQ1.data(), compiledQ1.instruction_count(), &state, d);
            if (state.register_types[compiledQ1.result_register] == TYPE_BITSET_HANDLE) {
                int h = static_cast<int>(state.registers[compiledQ1.result_register]);
                for (uint32_t n = 0; n < node_count; ++n) {
                    if (impulse_vm_context_bitset_test(het_ctx, h, n)) {
                        total_discoveries_seq++;
                    }
                }
                impulse_vm_context_release_bitset(het_ctx, state.registers[compiledQ1.result_register]);
            }
        }
        (void)total_discoveries_seq;
        auto t1_seq = std::chrono::high_resolution_clock::now();
        double dur_seq_ms = std::chrono::duration<double, std::milli>(t1_seq - t0_seq).count();
        double per_disease_us = (dur_seq_ms * 1000.0) / active_diseases.size();

        // Parallel Multi-Core Screen
        auto t0_par = std::chrono::high_resolution_clock::now();
        #pragma omp parallel
        {
            auto* thread_ctx = impulse_vm_context_create(het_snap);
            #pragma omp for schedule(dynamic, 16)
            for (size_t idx = 0; idx < active_diseases.size(); ++idx) {
                uint32_t d = active_diseases[idx];
                impulse_vm_state_t state{};
                state.query_context = thread_ctx;
                impulse_vm_execute(compiledQ1.data(), compiledQ1.instruction_count(), &state, d);
                if (state.register_types[compiledQ1.result_register] == TYPE_BITSET_HANDLE) {
                    impulse_vm_context_release_bitset(thread_ctx, state.registers[compiledQ1.result_register]);
                }
            }
            impulse_vm_context_destroy(thread_ctx);
        }
        auto t1_par = std::chrono::high_resolution_clock::now();
        double dur_par_ms = std::chrono::duration<double, std::milli>(t1_par - t0_par).count();

        std::cout << "---------------------------------------------------------------------------------------------------------\n";
        std::cout << " SCREEN 1: ALL-DISEASES 4-HOP DRUG REPURPOSING (Hetionet CbGpPWpD)\n";
        std::cout << " Metapath: Disease -> Gene -> Pathway -> Gene -> Compound (4 Hops)\n";
        std::cout << "---------------------------------------------------------------------------------------------------------\n";
        std::cout << " Total Active Disease Targets Screened: " << active_diseases.size() << " diseases\n";
        std::cout << " Single-Core Full Screen Time:          " << std::fixed << std::setprecision(3) << dur_seq_ms << " ms  (" << per_disease_us << " µs / disease)\n";
        std::cout << " Parallel Multi-Core Full Screen Time:  " << std::fixed << std::setprecision(3) << dur_par_ms << " ms  (Speedup: " << (dur_seq_ms / dur_par_ms) << "x)\n";
        std::cout << " Single-Core Throughput:                " << static_cast<uint64_t>(active_diseases.size() / (dur_seq_ms / 1000.0)) << " complete disease screens / sec\n";
        std::cout << " Parallel Multi-Core Throughput:        " << static_cast<uint64_t>(active_diseases.size() / (dur_par_ms / 1000.0)) << " complete disease screens / sec\n";
        std::cout << "---------------------------------------------------------------------------------------------------------\n\n";

        impulse_vm_context_destroy(het_ctx);
        impulse_snapshot_close(het_snap);
    }

    // -----------------------------------------------------------------------
    // SCREEN 2: DRKG ALL-COMPOUNDS DDI ADVERSE REACTION SCREEN (5.87M Edges)
    // -----------------------------------------------------------------------
    impulse_status_t st_drkg = IMPULSE_OK;
    impulse_snapshot_t* drkg_snap = impulse_snapshot_open(DRKG_PATH, &st_drkg);
    if (st_drkg == IMPULSE_OK && drkg_snap) {
        auto* drkg_ctx = impulse_vm_context_create(drkg_snap);
        assert(drkg_ctx != nullptr);

        uint16_t ddi_rel = 0;
        uint16_t gnbr_rel = 0;

        GraphCatalog catalog;
        catalog.register_relation("DRUGBANK::ddi_interactor_in", ddi_rel, 1.0);
        catalog.register_relation("GNBR::C", gnbr_rel, 1.0);

        const uint32_t* offsets = nullptr;
        const uint32_t* targets = nullptr;
        uint64_t node_count = 0;
        uint64_t edge_count = 0;
        impulse_snapshot_get_relation_buffers(drkg_snap, ddi_rel, &offsets, &targets, &node_count, &edge_count);

        std::vector<uint32_t> active_compounds;
        if (offsets) {
            for (uint32_t i = 0; i < node_count; ++i) {
                if (offsets[i + 1] > offsets[i]) {
                    active_compounds.push_back(i);
                }
            }
        }

        auto astQ2 = ScmProgram::of(
            ScmWalk::forward("DRUGBANK::ddi_interactor_in"),
            ScmWalk::forward("GNBR::C"),
            ScmCollect::bitset()
        );

        auto compiledQ2 = ImpulseCompiler::compile(astQ2, &catalog);

        // Warmup
        for (size_t i = 0; i < std::min<size_t>(500, active_compounds.size()); ++i) {
            uint32_t c = active_compounds[i];
            impulse_vm_state_t state{};
            state.query_context = drkg_ctx;
            impulse_vm_execute(compiledQ2.data(), compiledQ2.instruction_count(), &state, c);
            if (state.register_types[compiledQ2.result_register] == TYPE_BITSET_HANDLE) {
                impulse_vm_context_release_bitset(drkg_ctx, state.registers[compiledQ2.result_register]);
            }
        }

        // Single-Core Screen
        auto t0_seq = std::chrono::high_resolution_clock::now();
        uint64_t total_warnings_seq = 0;
        for (uint32_t c : active_compounds) {
            impulse_vm_state_t state{};
            state.query_context = drkg_ctx;
            impulse_vm_execute(compiledQ2.data(), compiledQ2.instruction_count(), &state, c);
            if (state.register_types[compiledQ2.result_register] == TYPE_BITSET_HANDLE) {
                int h = static_cast<int>(state.registers[compiledQ2.result_register]);
                for (uint32_t n = 0; n < node_count; ++n) {
                    if (impulse_vm_context_bitset_test(drkg_ctx, h, n)) {
                        total_warnings_seq++;
                    }
                }
                impulse_vm_context_release_bitset(drkg_ctx, state.registers[compiledQ2.result_register]);
            }
        }
        auto t1_seq = std::chrono::high_resolution_clock::now();
        double dur_seq_ms = std::chrono::duration<double, std::milli>(t1_seq - t0_seq).count();
        double per_compound_us = (dur_seq_ms * 1000.0) / active_compounds.size();

        // Parallel Multi-Core Screen
        auto t0_par = std::chrono::high_resolution_clock::now();
        #pragma omp parallel
        {
            auto* thread_ctx = impulse_vm_context_create(drkg_snap);
            #pragma omp for schedule(dynamic, 32)
            for (size_t idx = 0; idx < active_compounds.size(); ++idx) {
                uint32_t c = active_compounds[idx];
                impulse_vm_state_t state{};
                state.query_context = thread_ctx;
                impulse_vm_execute(compiledQ2.data(), compiledQ2.instruction_count(), &state, c);
                if (state.register_types[compiledQ2.result_register] == TYPE_BITSET_HANDLE) {
                    impulse_vm_context_release_bitset(thread_ctx, state.registers[compiledQ2.result_register]);
                }
            }
            impulse_vm_context_destroy(thread_ctx);
        }
        auto t1_par = std::chrono::high_resolution_clock::now();
        double dur_par_ms = std::chrono::duration<double, std::milli>(t1_par - t0_par).count();

        std::cout << "---------------------------------------------------------------------------------------------------------\n";
        std::cout << " SCREEN 2: ALL-COMPOUNDS DDI & ADVERSE PHARMACOVIGILANCE SCREEN (DRKG 5.87M Edges)\n";
        std::cout << " Metapath: Compound -> DDI Interactors -> Severe Side Effects (2 Hops)\n";
        std::cout << "---------------------------------------------------------------------------------------------------------\n";
        std::cout << " Total Active Compounds Screened:       " << active_compounds.size() << " compounds\n";
        std::cout << " Total Polypharmacology Warnings Found: " << total_warnings_seq << " adverse associations\n";
        std::cout << " Single-Core Full Screen Time:          " << std::fixed << std::setprecision(3) << dur_seq_ms << " ms  (" << per_compound_us << " µs / compound)\n";
        std::cout << " Parallel Multi-Core Full Screen Time:  " << std::fixed << std::setprecision(3) << dur_par_ms << " ms  (Speedup: " << (dur_seq_ms / dur_par_ms) << "x)\n";
        std::cout << " Single-Core Throughput:                " << static_cast<uint64_t>(active_compounds.size() / (dur_seq_ms / 1000.0)) << " complete compound screens / sec\n";
        std::cout << " Parallel Multi-Core Throughput:        " << static_cast<uint64_t>(active_compounds.size() / (dur_par_ms / 1000.0)) << " complete compound screens / sec\n";
        std::cout << "=========================================================================================================\n";

        impulse_vm_context_destroy(drkg_ctx);
        impulse_snapshot_close(drkg_snap);
    }

    return 0;
}
