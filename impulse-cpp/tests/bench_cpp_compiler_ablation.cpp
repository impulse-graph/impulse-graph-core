/**
 * @file bench_cpp_compiler_ablation.cpp
 * @brief Macro Benchmark for C++ Optimizing Compiler & Execution Ablation.
 */

#include "impulse_compiler.hpp"
#include "impulse_vm.h"
#include "impulse_graph.h"

#include <iostream>
#include <chrono>
#include <vector>
#include <numeric>
#include <algorithm>
#include <iomanip>
#include <cstdio>
#include <cassert>

using namespace impulse::compiler;

static const char* BENCH_SNAP_PATH = "bench_ablation_mock.imps";

static void build_benchmark_snapshot(size_t num_nodes, size_t degree) {
    impulse_writer_t* writer = impulse_writer_create(BENCH_SNAP_PATH, 0);
    assert(writer != nullptr);

    impulse_status_t st = impulse_writer_add_domain(writer, 0, IMPULSE_KEY_TYPE_INT32, "Default");
    assert(st == IMPULSE_OK);

    // Rel 0 (RelA): 1-to-1 linear chains
    std::vector<uint32_t> offsetsA(num_nodes + 1);
    std::vector<uint32_t> targetsA(num_nodes * degree);
    for (size_t i = 0; i <= num_nodes; ++i) offsetsA[i] = static_cast<uint32_t>(i * degree);
    for (size_t i = 0; i < num_nodes * degree; ++i) targetsA[i] = static_cast<uint32_t>((i + 1) % num_nodes);

    st = impulse_writer_add_relation(writer, 0, 0, IMPULSE_ENC_RAW, num_nodes, targetsA.size(), 0,
                                     offsetsA.data(), offsetsA.size() * sizeof(uint32_t),
                                     targetsA.data(), targetsA.size() * sizeof(uint32_t));
    assert(st == IMPULSE_OK);

    // Rel 1 (RelB)
    std::vector<uint32_t> offsetsB(num_nodes + 1);
    std::vector<uint32_t> targetsB(num_nodes * degree);
    for (size_t i = 0; i <= num_nodes; ++i) offsetsB[i] = static_cast<uint32_t>(i * degree);
    for (size_t i = 0; i < num_nodes * degree; ++i) targetsB[i] = static_cast<uint32_t>((i + 2) % num_nodes);

    st = impulse_writer_add_relation(writer, 0, 0, IMPULSE_ENC_RAW, num_nodes, targetsB.size(), 0,
                                     offsetsB.data(), offsetsB.size() * sizeof(uint32_t),
                                     targetsB.data(), targetsB.size() * sizeof(uint32_t));
    assert(st == IMPULSE_OK);

    st = impulse_writer_finalize(writer);
    assert(st == IMPULSE_OK);
    impulse_writer_destroy(writer);
}

int main() {
    std::cout << "=========================================================================\n";
    std::cout << "        IMPULSE GRAPH C++ COMPILER & EXECUTION ABLATION BENCHMARK        \n";
    std::cout << "=========================================================================\n\n";

    const size_t num_nodes = 50000;
    const size_t degree = 1; // linear chain for clean ablation
    build_benchmark_snapshot(num_nodes, degree);

    impulse_status_t st_snap = IMPULSE_OK;
    auto* snap = impulse_snapshot_open(BENCH_SNAP_PATH, &st_snap);
    assert(snap != nullptr && st_snap == IMPULSE_OK);
    auto* ctx = impulse_vm_context_create(snap);
    assert(ctx != nullptr);

    auto prog = ScmProgram::of(
        ScmWalk::forward("RelA"),
        ScmWalk::forward("RelB"),
        ScmCollect::bitset()
    );

    GraphCatalog catalog;
    catalog.register_relation("RelA", 0, 1.0);
    catalog.register_relation("RelB", 1, 1.0);

    // -----------------------------------------------------------------------
    // BENCHMARK 1: COMPILATION SPEED & THROUGHPUT
    // -----------------------------------------------------------------------
    const int compile_warmup = 10000;
    const int compile_runs = 50000;

    for (int i = 0; i < compile_warmup; ++i) {
        auto c = ImpulseCompiler::compile(prog, &catalog);
        (void)c;
    }

    std::vector<double> compile_times_ns;
    compile_times_ns.reserve(compile_runs);

    auto t0 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < compile_runs; ++i) {
        auto c_start = std::chrono::high_resolution_clock::now();
        auto c = ImpulseCompiler::compile(prog, &catalog);
        auto c_end = std::chrono::high_resolution_clock::now();
        compile_times_ns.push_back(std::chrono::duration<double, std::nano>(c_end - c_start).count());
        (void)c;
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    double total_compile_sec = std::chrono::duration<double>(t1 - t0).count();

    std::sort(compile_times_ns.begin(), compile_times_ns.end());
    double mean_compile_us = (std::accumulate(compile_times_ns.begin(), compile_times_ns.end(), 0.0) / compile_runs) / 1000.0;
    double p50_compile_us = compile_times_ns[compile_runs / 2] / 1000.0;
    double p90_compile_us = compile_times_ns[static_cast<size_t>(compile_runs * 0.90)] / 1000.0;
    double p99_compile_us = compile_times_ns[static_cast<size_t>(compile_runs * 0.99)] / 1000.0;
    double compile_qps = compile_runs / total_compile_sec;

    std::cout << "-------------------------------------------------------------------------\n";
    std::cout << " [1] C++ COMPILER PIPELINE THROUGHPUT (AST -> ImpOps Bytecode)\n";
    std::cout << "-------------------------------------------------------------------------\n";
    std::cout << "  Runs Measured:           " << compile_runs << "\n";
    std::cout << "  Mean Compilation Time:   " << std::fixed << std::setprecision(3) << mean_compile_us << " µs\n";
    std::cout << "  P50 (Median):            " << p50_compile_us << " µs\n";
    std::cout << "  P90:                     " << p90_compile_us << " µs\n";
    std::cout << "  P99:                     " << p99_compile_us << " µs\n";
    std::cout << "  Compilation Throughput:  " << std::setprecision(0) << compile_qps << " compilations / second\n";
    std::cout << "-------------------------------------------------------------------------\n\n";

    // -----------------------------------------------------------------------
    // BENCHMARK 2: EXECUTION ABLATION (Hop-Hop vs 2-Hop Fused)
    // -----------------------------------------------------------------------
    CompilerOptions opt_nofuse = CompilerOptions::default_options();
    opt_nofuse.enable_kernel_fusion = false;
    auto prog_nofuse = ImpulseCompiler::compile(prog, &catalog, {}, opt_nofuse);

    CompilerOptions opt_fused = CompilerOptions::default_options();
    opt_fused.enable_kernel_fusion = true;
    opt_fused.fused_2hop_max_multiplicity_threshold = 2.0;
    auto prog_fused = ImpulseCompiler::compile(prog, &catalog, {}, opt_fused);

    const int exec_runs = 50000;
    impulse_vm_state_t state{};
    state.query_context = ctx;

    // Warmup
    for (int i = 0; i < 10000; ++i) {
        impulse_vm_execute(prog_nofuse.data(), prog_nofuse.instruction_count(), &state, i % num_nodes);
        impulse_vm_execute(prog_fused.data(), prog_fused.instruction_count(), &state, i % num_nodes);
    }

    // Measure Hop-Hop
    auto t0_nofuse = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < exec_runs; ++i) {
        impulse_vm_execute(prog_nofuse.data(), prog_nofuse.instruction_count(), &state, i % num_nodes);
    }
    auto t1_nofuse = std::chrono::high_resolution_clock::now();
    double nofuse_total_us = std::chrono::duration<double, std::micro>(t1_nofuse - t0_nofuse).count();
    double nofuse_avg_us = nofuse_total_us / exec_runs;

    // Measure 2-Hop Fused
    auto t0_fused = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < exec_runs; ++i) {
        impulse_vm_execute(prog_fused.data(), prog_fused.instruction_count(), &state, i % num_nodes);
    }
    auto t1_fused = std::chrono::high_resolution_clock::now();
    double fused_total_us = std::chrono::duration<double, std::micro>(t1_fused - t0_fused).count();
    double fused_avg_us = fused_total_us / exec_runs;

    std::cout << "-------------------------------------------------------------------------\n";
    std::cout << " [2] VM EXECUTION ABLATION (50,000 runs each)\n";
    std::cout << "-------------------------------------------------------------------------\n";
    std::cout << "  Unfused (Hop-Hop):       " << std::fixed << std::setprecision(3) << nofuse_avg_us << " µs / query  (" << static_cast<long>(exec_runs / (nofuse_total_us / 1e6)) << " QPS)\n";
    std::cout << "  Fused (OP_CSR_WALK_2HOP):" << std::fixed << std::setprecision(3) << fused_avg_us << " µs / query  (" << static_cast<long>(exec_runs / (fused_total_us / 1e6)) << " QPS)\n";
    std::cout << "  Kernel Fusion Speedup:   " << std::fixed << std::setprecision(2) << (nofuse_avg_us / fused_avg_us) << "x FASTER\n";
    std::cout << "=========================================================================\n";

    impulse_vm_context_destroy(ctx);
    impulse_snapshot_close(snap);
    std::remove(BENCH_SNAP_PATH);
    return 0;
}
