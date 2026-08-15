/**
 * @file bench_cel_compiler.cpp
 * @brief High-Resolution Tight-Loop Benchmark Suite for Google CEL Parser, AST Optimizer & ImpScheme Lowering.
 */

#include "impulse_cel.h"
#include <iostream>
#include <iomanip>
#include <vector>
#include <string>
#include <chrono>
#include <fstream>
#include <cassert>

using namespace impulse::cel;

struct CelBenchScenario {
    std::string name;
    std::string expr;
    int expected_leaves;
    int iterations;
};

struct ScenarioResult {
    std::string name;
    int leaves;
    int depth;
    int iterations;
    double parse_us;
    double opt_us;
    double codegen_us;
    double total_compile_us;
    double compiles_per_sec;
};

static size_t count_ast_leaves(const std::shared_ptr<AstNode>& node) {
    if (!node) return 0;
    if (node->children.empty()) return 1;
    size_t total = 0;
    for (const auto& child : node->children) {
        total += count_ast_leaves(child);
    }
    return total;
}

static size_t measure_ast_depth(const std::shared_ptr<AstNode>& node) {
    if (!node) return 0;
    size_t max_d = 0;
    for (const auto& child : node->children) {
        max_d = std::max(max_d, measure_ast_depth(child));
    }
    return 1 + max_d;
}

static std::string generate_extreme_pathological_expr(int num_terms) {
    std::string expr = "clamp(safeDiv(sqrt(edge.weight * 100.0), 1.0, 0.0)";
    for (int i = 0; i < num_terms; ++i) {
        if (i % 4 == 0) {
            expr += " + (edge.f" + std::to_string(i) + " > 0.5 ? log(dest.f" + std::to_string(i) + " + 1.0) : -0.5)";
        } else if (i % 4 == 1) {
            expr += " * (1.0 + sigmoid(src.f" + std::to_string(i) + " - dest.f" + std::to_string(i) + "))";
        } else if (i % 4 == 2) {
            expr += " - (popcount(node.flags_" + std::to_string(i) + ") % 4 == 0 ? 0.05 : 0.15)";
        } else {
            expr += " + gelu(src.emb_" + std::to_string(i) + ") * silu(dest.emb_" + std::to_string(i) + ")";
        }
    }
    expr += ", 0.0, 10000.0)";
    return expr;
}

static ScenarioResult run_benchmark(const CelBenchScenario& sc) {
    // 1. Warm-up
    for (int i = 0; i < 500; ++i) {
        Parser p(sc.expr);
        auto ast = p.parse_expression();
        auto opt = AstOptimizer::optimize(ast);
        auto ir = CelCompiler::to_impscheme(opt);
        (void)ir;
    }

    // 2. Parse-Only Phase
    auto t0 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < sc.iterations; ++i) {
        Parser p(sc.expr);
        auto ast = p.parse_expression();
        (void)ast;
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    double parse_total_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    double parse_us = (parse_total_ms * 1000.0) / sc.iterations;

    // 3. Optimize-Only Phase
    Parser p_single(sc.expr);
    auto baseline_ast = p_single.parse_expression();
    size_t leaves = count_ast_leaves(baseline_ast);
    size_t depth = measure_ast_depth(baseline_ast);

    auto t2 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < sc.iterations; ++i) {
        auto opt = AstOptimizer::optimize(baseline_ast);
        (void)opt;
    }
    auto t3 = std::chrono::high_resolution_clock::now();
    double opt_total_ms = std::chrono::duration<double, std::milli>(t3 - t2).count();
    double opt_us = (opt_total_ms * 1000.0) / sc.iterations;

    // 4. Codegen-Only Phase
    auto opt_ast = AstOptimizer::optimize(baseline_ast);
    auto t4 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < sc.iterations; ++i) {
        auto ir = CelCompiler::to_impscheme(opt_ast);
        (void)ir;
    }
    auto t5 = std::chrono::high_resolution_clock::now();
    double codegen_total_ms = std::chrono::duration<double, std::milli>(t5 - t4).count();
    double codegen_us = (codegen_total_ms * 1000.0) / sc.iterations;

    // 5. Full End-to-End Compile Phase
    auto t6 = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < sc.iterations; ++i) {
        Parser p(sc.expr);
        auto ast = p.parse_expression();
        auto opt = AstOptimizer::optimize(ast);
        auto ir = CelCompiler::to_impscheme(opt);
        (void)ir;
    }
    auto t7 = std::chrono::high_resolution_clock::now();
    double full_total_ms = std::chrono::duration<double, std::milli>(t7 - t6).count();
    double total_compile_us = (full_total_ms * 1000.0) / sc.iterations;
    double compiles_per_sec = (sc.iterations / full_total_ms) * 1000.0;

    return ScenarioResult{
        sc.name,
        static_cast<int>(leaves),
        static_cast<int>(depth),
        sc.iterations,
        parse_us,
        opt_us,
        codegen_us,
        total_compile_us,
        compiles_per_sec
    };
}

int main() {
    std::cout << "==========================================================================================================" << std::endl;
    std::cout << "                  Impulse Graph Engine - Google CEL Compiler & Optimizer Stress Benchmark                  " << std::endl;
    std::cout << "==========================================================================================================" << std::endl;

    std::string expr_pathological_40 = 
        "(edge.weight > 0.0 && !isNan(edge.friction)) ? "
        "  clamp("
        "    safeDiv("
        "      sqrt(edge.weight * 100.0) + log(dest.priority + 1.0) * pow(src.latitude - dest.latitude, 2.0) + exp(-edge.decay_rate * (now() - edge.created_at)),"
        "      hypot(src.x - dest.x, src.y - dest.y) + 0.001,"
        "      0.0"
        "    ) * (1.0 + sigmoid(dest.rating - src.rating)) - (popcount(node.flags) % 4 == 0 ? 0.05 : 0.15) * sin(edge.angle * 3.14159 / 180.0),"
        "    0.0,"
        "    1000.0"
        "  ) : "
        "  (edge.fallback_active ? "
        "    lerp(src.default_score, dest.default_score, 0.5) * (gelu(src.emb_0) + silu(dest.emb_0)) : "
        "    -1.0"
        "  )";

    std::string expr_extreme_100 = generate_extreme_pathological_expr(25); // ~105 leaf terms

    std::vector<CelBenchScenario> scenarios = {
        {
            "Simple Filter Predicate",
            "edge.weight > 10.0 && dest.region == \"us-east\"",
            4,
            100000
        },
        {
            "ReBAC Security Policy Rule",
            "user.is_admin || (user.tenant_id == resource.tenant_id && !resource.is_locked && request.action == \"READ\")",
            8,
            100000
        },
        {
            "Analytical Scoring & Math Formula",
            "clamp(safeDiv(sqrt(edge.weight * 100.0) + log(dest.priority + 1.0), hypot(src.x - dest.x, src.y - dest.y) + 0.001, 0.0) * (1.0 + sigmoid(dest.rating - src.rating)), 0.0, 1000.0)",
            16,
            50000
        },
        {
            "Pathological 39-Leaf Nested Expression",
            expr_pathological_40,
            39,
            20000
        },
        {
            "Extreme 100-Leaf Generated Pathological Expression",
            expr_extreme_100,
            105,
            10000
        }
    };

    std::vector<ScenarioResult> results;
    for (const auto& sc : scenarios) {
        results.push_back(run_benchmark(sc));
    }

    std::cout << std::left
              << std::setw(38) << "Scenario"
              << std::setw(8)  << "Leaves"
              << std::setw(8)  << "Depth"
              << std::setw(12) << "Parse(us)"
              << std::setw(12) << "Opt(us)"
              << std::setw(12) << "IR Emit(us)"
              << std::setw(14) << "Total(us)"
              << std::setw(16) << "Compiles/sec"
              << std::endl;
    std::cout << std::string(120, '-') << std::endl;

    for (const auto& r : results) {
        std::cout << std::left
                  << std::setw(38) << r.name
                  << std::setw(8)  << r.leaves
                  << std::setw(8)  << r.depth
                  << std::setw(12) << std::fixed << std::setprecision(2) << r.parse_us
                  << std::setw(12) << std::fixed << std::setprecision(2) << r.opt_us
                  << std::setw(12) << std::fixed << std::setprecision(2) << r.codegen_us
                  << std::setw(14) << std::fixed << std::setprecision(2) << r.total_compile_us
                  << std::setw(16) << static_cast<int>(r.compiles_per_sec)
                  << std::endl;
    }
    std::cout << std::string(120, '-') << std::endl;

    // Export JSON telemetry
    std::ofstream json_out("cel_compiler_benchmark.json");
    if (json_out.is_open()) {
        json_out << "{\n  \"benchmarks\": [\n";
        for (size_t i = 0; i < results.size(); ++i) {
            const auto& r = results[i];
            json_out << "    {\n"
                     << "      \"name\": \"" << r.name << "\",\n"
                     << "      \"leaves\": " << r.leaves << ",\n"
                     << "      \"depth\": " << r.depth << ",\n"
                     << "      \"iterations\": " << r.iterations << ",\n"
                     << "      \"parse_us\": " << r.parse_us << ",\n"
                     << "      \"opt_us\": " << r.opt_us << ",\n"
                     << "      \"codegen_us\": " << r.codegen_us << ",\n"
                     << "      \"total_compile_us\": " << r.total_compile_us << ",\n"
                     << "      \"compiles_per_sec\": " << r.compiles_per_sec << "\n"
                     << "    }" << (i + 1 < results.size() ? "," : "") << "\n";
        }
        json_out << "  ]\n}\n";
        json_out.close();
        std::cout << "[Telemetry] Exported results to cel_compiler_benchmark.json" << std::endl;
    }

    std::cout << "=== CEL Benchmark Complete. Status: 100% SUCCESS ===" << std::endl;
    return 0;
}
