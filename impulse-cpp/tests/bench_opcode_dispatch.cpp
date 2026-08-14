/**
 * @file bench_opcode_dispatch.cpp
 * @brief High-Resolution Opcode Dispatch Stress-Test & Performance Regression Benchmark Suite.
 *
 * Quantifies ImpulseVM indirect dispatch loop latency, MDOPS (Million Dispatched Opcodes/sec),
 * branch prediction efficiency, and subroutine overhead over 1,000,000+ iterations.
 */

#include "impulse_vm.h"
#include <iostream>
#include <vector>
#include <chrono>
#include <cassert>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <string>
#include <numeric>
#include <algorithm>

struct BenchmarkResult {
    std::string name;
    size_t iterations;
    size_t opcodes_per_iteration;
    size_t total_dispatches;
    double avg_latency_ms;
    double min_latency_ms;
    double mdops;              // Million Dispatched Opcodes per Second
    double ns_per_dispatch;    // Nanoseconds per opcode dispatch
    double native_latency_ms;
    double overhead_vs_native;
    bool passed_threshold;
};

// -----------------------------------------------------------------------------
// 1. Tight Loop Minimum Overhead Benchmark
// -----------------------------------------------------------------------------
static BenchmarkResult run_tight_loop_benchmark(size_t iterations) {
    // 0: LOAD R0, N
    // 1: LOOP_DECR R0, 1
    // 2: JNZ -1 (back to 1)
    // 3: HALT
    std::vector<impulse_instruction_t> prog = {
        { OP_LOAD_CONST_INT, 0, 0, static_cast<uint32_t>(iterations) },
        { OP_LOOP_DECR,      0, 0, 1 },
        { OP_JNZ,            0, 0, static_cast<uint32_t>(-1) },
        { OP_HALT,           0, 0, 0 }
    };

    impulse_vm_state_t state{};
    impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr);
    state.query_context = ctx;

    // Warm-up
    prog[0].payload = 10000;
    impulse_vm_execute(prog.data(), prog.size(), &state, 0);

    // Native C++ Baseline
    auto t_native_start = std::chrono::high_resolution_clock::now();
    uint64_t native_r0 = iterations;
    while (native_r0 > 0) {
        native_r0 = native_r0 - 1;
    }
    auto t_native_end = std::chrono::high_resolution_clock::now();
    double native_ms = std::chrono::duration<double, std::milli>(t_native_end - t_native_start).count();

    // Benchmark runs
    prog[0].payload = static_cast<uint32_t>(iterations);
    const int runs = 5;
    std::vector<double> latencies;
    for (int r = 0; r < runs; ++r) {
        state = impulse_vm_state_t{};
        state.query_context = ctx;
        auto t0 = std::chrono::high_resolution_clock::now();
        impulse_vm_status_t st = impulse_vm_execute(prog.data(), prog.size(), &state, 0);
        auto t1 = std::chrono::high_resolution_clock::now();
        assert(st == IMPULSE_VM_OK);
        latencies.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
    }

    impulse_vm_context_destroy(ctx);

    double avg_ms = std::accumulate(latencies.begin(), latencies.end(), 0.0) / runs;
    double min_ms = *std::min_element(latencies.begin(), latencies.end());
    size_t total_ops = 1 + iterations * 2 + 1; // 1 load + (decr + jnz)*N + halt
    double mdops = (total_ops / (avg_ms * 1e-3)) / 1e6;
    double ns_per_op = (avg_ms * 1e6) / total_ops;

    return BenchmarkResult{
        "Tight Loop Baseline (OP_LOOP_DECR + OP_JNZ)",
        iterations,
        2,
        total_ops,
        avg_ms,
        min_ms,
        mdops,
        ns_per_op,
        native_ms,
        avg_ms / (native_ms > 0.0001 ? native_ms : 0.0001),
        mdops >= 150.0 // Regression threshold: >= 150 MDOPS
    };
}

// -----------------------------------------------------------------------------
// 2. Mixed Scalar ALU Pipeline Benchmark (16-Way Unrolled)
// -----------------------------------------------------------------------------
static BenchmarkResult run_mixed_alu_pipeline_benchmark(size_t iterations) {
    std::vector<impulse_instruction_t> prog;
    prog.push_back({ OP_LOAD_CONST_INT, 0, 0, static_cast<uint32_t>(iterations) });
    // Unroll 14 register loads inside loop body (PC 1..14)
    for (uint16_t r = 1; r <= 14; ++r) {
        prog.push_back({ OP_LOAD_CONST_INT, 0, r, static_cast<uint32_t>(r * 7 + 3) });
    }
    prog.push_back({ OP_LOOP_DECR, 0, 0, 1 });                          // PC 15
    prog.push_back({ OP_JNZ,       0, 0, static_cast<uint32_t>(-15) }); // PC 16 (back to 1)
    prog.push_back({ OP_HALT,      0, 0, 0 });                          // PC 17

    impulse_vm_state_t state{};
    impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr);
    state.query_context = ctx;

    // Warm-up
    prog[0].payload = 10000;
    impulse_vm_execute(prog.data(), prog.size(), &state, 0);

    // Native C++ Baseline
    auto t_native_start = std::chrono::high_resolution_clock::now();
    uint64_t native_r[16] = {0};
    native_r[0] = iterations;
    while (native_r[0] > 0) {
        for (uint16_t r = 1; r <= 14; ++r) {
            native_r[r] = r * 7 + 3;
        }
        native_r[0] = native_r[0] - 1;
    }
    auto t_native_end = std::chrono::high_resolution_clock::now();
    double native_ms = std::chrono::duration<double, std::milli>(t_native_end - t_native_start).count();

    // Benchmark runs
    prog[0].payload = static_cast<uint32_t>(iterations);
    const int runs = 5;
    std::vector<double> latencies;
    for (int r = 0; r < runs; ++r) {
        state = impulse_vm_state_t{};
        state.query_context = ctx;
        auto t0 = std::chrono::high_resolution_clock::now();
        impulse_vm_status_t st = impulse_vm_execute(prog.data(), prog.size(), &state, 0);
        auto t1 = std::chrono::high_resolution_clock::now();
        assert(st == IMPULSE_VM_OK);
        latencies.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
    }

    impulse_vm_context_destroy(ctx);

    double avg_ms = std::accumulate(latencies.begin(), latencies.end(), 0.0) / runs;
    double min_ms = *std::min_element(latencies.begin(), latencies.end());
    size_t total_ops = 1 + iterations * 16 + 1; // 1 load + (14 loads + decr + jnz)*N + halt
    double mdops = (total_ops / (avg_ms * 1e-3)) / 1e6;
    double ns_per_op = (avg_ms * 1e6) / total_ops;

    return BenchmarkResult{
        "Mixed Scalar ALU Pipeline (16-Op Unrolled)",
        iterations,
        16,
        total_ops,
        avg_ms,
        min_ms,
        mdops,
        ns_per_op,
        native_ms,
        avg_ms / (native_ms > 0.0001 ? native_ms : 0.0001),
        mdops >= 200.0 // Regression threshold: >= 200 MDOPS
    };
}

// -----------------------------------------------------------------------------
// 3. Branch Predictor & Jump Stress Benchmark
// -----------------------------------------------------------------------------
static BenchmarkResult run_branch_predict_benchmark(size_t iterations) {
    // 0: LOAD R0, N
    // 1: LOAD R1, 1
    // 2: LOOP_DECR R1, 1  (R1=0, sets ZF=1)
    // 3: JZ +2            (ZF=1 -> jump to 5)
    // 4: NOP              (skipped)
    // 5: LOAD R2, 2
    // 6: LOOP_DECR R2, 1  (R2=1, sets ZF=0)
    // 7: JNZ +2           (ZF=0 -> jump to 9)
    // 8: NOP              (skipped)
    // 9: LOOP_DECR R0, 1
    // 10: JNZ -9          (back to 1)
    // 11: HALT
    std::vector<impulse_instruction_t> prog = {
        { OP_LOAD_CONST_INT, 0, 0, static_cast<uint32_t>(iterations) }, // 0
        { OP_LOAD_CONST_INT, 0, 1, 1 },                                  // 1
        { OP_LOOP_DECR,      0, 1, 1 },                                  // 2 (sets ZF=1)
        { OP_JZ,             0, 0, 2 },                                  // 3 (jump to 5)
        { OP_NOP,            0, 0, 0 },                                  // 4 (skipped)
        { OP_LOAD_CONST_INT, 0, 2, 2 },                                  // 5
        { OP_LOOP_DECR,      0, 2, 1 },                                  // 6 (sets ZF=0)
        { OP_JNZ,            0, 0, 2 },                                  // 7 (jump to 9)
        { OP_NOP,            0, 0, 0 },                                  // 8 (skipped)
        { OP_LOOP_DECR,      0, 0, 1 },                                  // 9
        { OP_JNZ,            0, 0, static_cast<uint32_t>(-9) },          // 10 (back to 1)
        { OP_HALT,           0, 0, 0 }                                   // 11
    };

    impulse_vm_state_t state{};
    impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr);
    state.query_context = ctx;

    // Warm-up
    prog[0].payload = 10000;
    impulse_vm_execute(prog.data(), prog.size(), &state, 0);

    // Native C++ Baseline
    auto t_native_start = std::chrono::high_resolution_clock::now();
    uint64_t native_r0 = iterations;
    uint64_t native_r1 = 0, native_r2 = 0;
    while (native_r0 > 0) {
        native_r1 = 1;
        native_r1 = native_r1 - 1;
        if (native_r1 == 0) {
            native_r2 = 2;
            native_r2 = native_r2 - 1;
            if (native_r2 != 0) {
                native_r0 = native_r0 - 1;
            }
        }
    }
    auto t_native_end = std::chrono::high_resolution_clock::now();
    double native_ms = std::chrono::duration<double, std::milli>(t_native_end - t_native_start).count();

    // Benchmark runs
    prog[0].payload = static_cast<uint32_t>(iterations);
    const int runs = 5;
    std::vector<double> latencies;
    for (int r = 0; r < runs; ++r) {
        state = impulse_vm_state_t{};
        state.query_context = ctx;
        auto t0 = std::chrono::high_resolution_clock::now();
        impulse_vm_status_t st = impulse_vm_execute(prog.data(), prog.size(), &state, 0);
        auto t1 = std::chrono::high_resolution_clock::now();
        assert(st == IMPULSE_VM_OK);
        latencies.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
    }

    impulse_vm_context_destroy(ctx);

    double avg_ms = std::accumulate(latencies.begin(), latencies.end(), 0.0) / runs;
    double min_ms = *std::min_element(latencies.begin(), latencies.end());
    size_t total_ops = 1 + iterations * 7 + 1; // 7 executed ops per iter
    double mdops = (total_ops / (avg_ms * 1e-3)) / 1e6;
    double ns_per_op = (avg_ms * 1e6) / total_ops;

    return BenchmarkResult{
        "Branch Predictor & Conditional Jumps (OP_JZ / OP_JNZ)",
        iterations,
        7,
        total_ops,
        avg_ms,
        min_ms,
        mdops,
        ns_per_op,
        native_ms,
        avg_ms / (native_ms > 0.0001 ? native_ms : 0.0001),
        mdops >= 150.0 // Regression threshold: >= 150 MDOPS
    };
}

// -----------------------------------------------------------------------------
// 4. Subroutine Call Trampoline Benchmark
// -----------------------------------------------------------------------------
static BenchmarkResult run_subroutine_trampoline_benchmark(size_t iterations) {
    // 0: LOAD R8, N (Keep counter in R8 to preserve across R0..R3 register window)
    // 1: CALL 5
    // 2: LOOP_DECR R8, 1
    // 3: JNZ -2 (back to 1)
    // 4: HALT
    // Subroutine:
    // 5: ENTER_FRAME 0
    // 6: LOAD R4, 42
    // 7: LEAVE_FRAME 0
    // 8: RET
    std::vector<impulse_instruction_t> prog = {
        { OP_LOAD_CONST_INT, 0, 8, static_cast<uint32_t>(iterations) }, // 0
        { OP_CALL,           0, 0, 5 },                                  // 1 (call subroutine at 5)
        { OP_LOOP_DECR,      0, 8, 1 },                                  // 2
        { OP_JNZ,            0, 0, static_cast<uint32_t>(-2) },          // 3 (loop back to 1)
        { OP_HALT,           0, 0, 0 },                                  // 4
        // Subroutine body
        { OP_ENTER_FRAME,    0, 0, 0 },                                  // 5
        { OP_LOAD_CONST_INT, 0, 4, 42 },                                 // 6
        { OP_LEAVE_FRAME,    0, 0, 0 },                                  // 7
        { OP_RET,            0, 0, 0 }                                   // 8
    };

    impulse_vm_state_t state{};
    impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr);
    state.query_context = ctx;

    // Warm-up
    prog[0].payload = 10000;
    impulse_vm_execute(prog.data(), prog.size(), &state, 0);

    // Native C++ Baseline
    auto native_subroutine = [](uint64_t& r4) __attribute__((noinline)) {
        r4 = 42;
    };
    auto t_native_start = std::chrono::high_resolution_clock::now();
    uint64_t native_r8 = iterations;
    uint64_t native_r4 = 0;
    while (native_r8 > 0) {
        native_subroutine(native_r4);
        native_r8 = native_r8 - 1;
    }
    auto t_native_end = std::chrono::high_resolution_clock::now();
    double native_ms = std::chrono::duration<double, std::milli>(t_native_end - t_native_start).count();

    // Benchmark runs
    prog[0].payload = static_cast<uint32_t>(iterations);
    const int runs = 5;
    std::vector<double> latencies;
    for (int r = 0; r < runs; ++r) {
        state = impulse_vm_state_t{};
        state.query_context = ctx;
        auto t0 = std::chrono::high_resolution_clock::now();
        impulse_vm_status_t st = impulse_vm_execute(prog.data(), prog.size(), &state, 0);
        auto t1 = std::chrono::high_resolution_clock::now();
        assert(st == IMPULSE_VM_OK);
        latencies.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
    }

    impulse_vm_context_destroy(ctx);

    double avg_ms = std::accumulate(latencies.begin(), latencies.end(), 0.0) / runs;
    double min_ms = *std::min_element(latencies.begin(), latencies.end());
    size_t total_ops = 1 + iterations * 6 + 1; // call + enter + load + leave + ret + decr/jnz
    double mdops = (total_ops / (avg_ms * 1e-3)) / 1e6;
    double ns_per_op = (avg_ms * 1e6) / total_ops;

    return BenchmarkResult{
        "Subroutine Call Trampoline (OP_CALL / OP_RET)",
        iterations,
        6,
        total_ops,
        avg_ms,
        min_ms,
        mdops,
        ns_per_op,
        native_ms,
        avg_ms / (native_ms > 0.0001 ? native_ms : 0.0001),
        mdops >= 100.0 // Regression threshold: >= 100 MDOPS
    };
}

int main(int argc, char** argv) {
    std::cout << "================================================================================" << std::endl;
    std::cout << "             ImpulseVM Opcode Dispatch Stress-Test Benchmark                    " << std::endl;
    std::cout << "================================================================================" << std::endl;

    std::vector<BenchmarkResult> results;
    results.push_back(run_tight_loop_benchmark(1000000));
    results.push_back(run_mixed_alu_pipeline_benchmark(1000000));
    results.push_back(run_branch_predict_benchmark(1000000));
    results.push_back(run_subroutine_trampoline_benchmark(500000));

    std::cout << "\n" << std::left 
              << std::setw(52) << "Benchmark Scenario"
              << std::setw(14) << "Dispatches"
              << std::setw(12) << "Latency(ms)"
              << std::setw(12) << "MDOPS"
              << std::setw(12) << "ns/Opcode"
              << std::setw(10) << "Overhead"
              << std::setw(8)  << "Status"
              << std::endl;
    std::cout << std::string(120, '-') << std::endl;

    bool all_passed = true;
    for (const auto& res : results) {
        std::cout << std::left 
                  << std::setw(52) << res.name
                  << std::setw(14) << res.total_dispatches
                  << std::setw(12) << std::fixed << std::setprecision(2) << res.avg_latency_ms
                  << std::setw(12) << std::fixed << std::setprecision(1) << res.mdops
                  << std::setw(12) << std::fixed << std::setprecision(2) << res.ns_per_dispatch
                  << std::setw(10) << std::fixed << std::setprecision(1) << (std::to_string(static_cast<int>(res.overhead_vs_native)) + "x")
                  << std::setw(8)  << (res.passed_threshold ? "PASS" : "WARN")
                  << std::endl;
        if (!res.passed_threshold) all_passed = false;
    }
    std::cout << std::string(120, '-') << std::endl;

    // Export JSON telemetry file
    std::string json_path = "opcode_dispatch_benchmark.json";
    if (argc > 1) {
        json_path = argv[1];
    }
    std::ofstream ofs(json_path);
    if (ofs.is_open()) {
        ofs << "{\n  \"timestamp\": " << std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()) << ",\n";
        ofs << "  \"benchmarks\": [\n";
        for (size_t i = 0; i < results.size(); ++i) {
            const auto& r = results[i];
            ofs << "    {\n";
            ofs << "      \"name\": \"" << r.name << "\",\n";
            ofs << "      \"iterations\": " << r.iterations << ",\n";
            ofs << "      \"total_dispatches\": " << r.total_dispatches << ",\n";
            ofs << "      \"avg_latency_ms\": " << r.avg_latency_ms << ",\n";
            ofs << "      \"min_latency_ms\": " << r.min_latency_ms << ",\n";
            ofs << "      \"mdops\": " << r.mdops << ",\n";
            ofs << "      \"ns_per_dispatch\": " << r.ns_per_dispatch << ",\n";
            ofs << "      \"overhead_vs_native\": " << r.overhead_vs_native << ",\n";
            ofs << "      \"passed_threshold\": " << (r.passed_threshold ? "true" : "false") << "\n";
            ofs << "    }" << (i + 1 < results.size() ? "," : "") << "\n";
        }
        ofs << "  ]\n}\n";
        std::cout << "\n[Telemetry] Wrote JSON metrics to " << json_path << std::endl;
    }

    std::cout << "\n=== Opcode Dispatch Benchmark Complete. Result: " 
              << (all_passed ? "100% REGRESSION THRESHOLDS SATISFIED" : "REGRESSION DETECTED") 
              << " ===" << std::endl;

    return all_passed ? 0 : 1;
}
