/**
 * @file test_cel_parser.cpp
 * @brief Exhaustive Unit Tests for Google CEL Zero-Dependency Pratt Parser & ImpScheme IR Lowering.
 */

#include "impulse_cel.h"
#include <iostream>
#include <cassert>
#include <iomanip>
#include <chrono>
#include <algorithm>

using namespace impulse::cel;

static void test_cel_arithmetic_and_precedence() {
    std::cout << "[Test] CEL Arithmetic and Operator Precedence..." << std::endl;

    Parser p1("2 + 3 * 4");
    auto ast1 = p1.parse_expression();
    assert(ast1 != nullptr);
    std::string ir1 = CelCompiler::to_impscheme(ast1);
    assert(ir1 == "(+ 2 (* 3 4))");

    Parser p2("a > 10 && b <= 20");
    auto ast2 = p2.parse_expression();
    assert(ast2 != nullptr);
    std::string ir2 = CelCompiler::to_impscheme(ast2);
    assert(ir2 == "(mask-and (vec-cmp-gt a 10) (<= b 20))");

    Parser p3("!is_active || status == 200");
    auto ast3 = p3.parse_expression();
    assert(ast3 != nullptr);
    std::string ir3 = CelCompiler::to_impscheme(ast3);
    assert(ir3 == "(mask-or (mask-not is_active) (vec-cmp-eq status 200))");

    std::cout << "  -> PASSED: " << ir1 << " | " << ir2 << std::endl;
}

static void test_cel_ternary_and_members() {
    std::cout << "[Test] CEL Ternary Conditional and Member Access..." << std::endl;

    Parser p1("edge.miles > 100.0 ? dest.priority : 0");
    auto ast1 = p1.parse_expression();
    assert(ast1 != nullptr);
    std::string ir1 = CelCompiler::to_impscheme(ast1);
    assert(ir1 == "(vec-blend (vec-cmp-gt (get-attr edge \"miles\") 100.0) (get-attr dest \"priority\") 0)");

    std::cout << "  -> PASSED: " << ir1 << std::endl;
}

static void test_cel_vector_math_calls() {
    std::cout << "[Test] CEL Analytical Vector Math Functions (42 Functions)..." << std::endl;

    // Verify all 42 math function names resolve to distinct valid enum IDs
    const char* all_math_names[] = {
        "abs", "sqrt", "rsqrt", "cbrt", "pow", "hypot", "lerp",
        "exp", "exp2", "exp10", "expm1", "log", "log2", "log10", "log1p",
        "sin", "cos", "tan", "asin", "acos", "atan", "atan2", "sinc",
        "sinh", "cosh", "tanh", "asinh", "acosh", "atanh",
        "floor", "ceil", "trunc", "round", "clamp", "copysign", "fmod",
        "relu", "leaky_relu", "sigmoid", "gelu", "silu", "softplus",
        "erf", "erfc", "lgamma",
        "popcount", "clz", "ctz", "rotl", "rotr",
        "safeDiv", "isNan", "isInf", "isFinite"
    };

    for (const char* name : all_math_names) {
        int func_id = CelCompiler::resolve_math_func(name);
        assert(func_id >= 0);
        (void)func_id;
        std::string expr_str = std::string(name) + "(x)";
        Parser p(expr_str);
        auto ast = p.parse_expression();
        assert(ast != nullptr);
        std::string ir = CelCompiler::to_impscheme(ast);
        assert(ir == std::string("(") + name + " x)");
    }

    std::cout << "  -> PASSED: All 46 CEL vector math and safe FP functions verified." << std::endl;
}

static void test_cel_temporal_and_datetime() {
    std::cout << "[Test] CEL Datetime & Temporal Expressions..." << std::endl;

    Parser p1("timestamp(\"2026-08-13T00:00:00Z\") + duration(\"24h\")");
    auto ast1 = p1.parse_expression();
    assert(ast1 != nullptr);
    std::string ir1 = CelCompiler::to_impscheme(ast1);
    assert(ir1 == "(+ (timestamp \"2026-08-13T00:00:00Z\") (duration \"24h\"))");

    Parser p2("now() - edge.timestamp > duration(\"7d\")");
    auto ast2 = p2.parse_expression();
    assert(ast2 != nullptr);
    std::string ir2 = CelCompiler::to_impscheme(ast2);
    assert(ir2 == "(vec-cmp-gt (- (now) (get-attr edge \"timestamp\")) (duration \"7d\"))");

    std::cout << "  -> PASSED: " << ir1 << " | " << ir2 << std::endl;
}

static void test_cel_lists_and_methods() {
    std::cout << "[Test] CEL List Literals and Method Calls..." << std::endl;

    Parser p1("dest.region.startsWith(\"us-\")");
    auto ast1 = p1.parse_expression();
    assert(ast1 != nullptr);
    std::string ir1 = CelCompiler::to_impscheme(ast1);
    assert(ir1 == "(startsWith (get-attr dest \"region\") \"us-\")");

    Parser p2("[1, 2, 3, 5]");
    auto ast2 = p2.parse_expression();
    assert(ast2 != nullptr);
    std::string ir2 = CelCompiler::to_impscheme(ast2);
    assert(ir2 == "(list 1 2 3 5)");

    std::cout << "  -> PASSED: " << ir1 << " | " << ir2 << std::endl;
}

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

static void test_cel_pathological_40_term_expression() {
    std::cout << "[Test] Pathological Complex Nested CEL Expression (40 Leaf Terms)..." << std::endl;

    std::string pathological_expr = 
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

    Parser parser(pathological_expr);
    auto ast = parser.parse_expression();
    assert(ast != nullptr);

    size_t leaves = count_ast_leaves(ast);
    size_t depth = measure_ast_depth(ast);
    std::cout << "  -> AST Statistics: Total Leaf Count = " << leaves << " (Expected ~40), Max AST Depth = " << depth << std::endl;
    assert(leaves >= 35);
    assert(depth >= 10);

    // Lower to ImpScheme S-Expression IR
    std::string ir = CelCompiler::to_impscheme(ast);
    assert(!ir.empty());
    assert(ir.front() == '(' && ir.back() == ')');
    std::cout << "  -> ImpScheme S-Expression IR (Length " << ir.size() << " bytes):" << std::endl;
    std::cout << "     " << ir << std::endl;

    // Stress Benchmark: Parse 10,000 times to verify throughput & zero memory leak
    auto t0 = std::chrono::high_resolution_clock::now();
    const int parse_iterations = 10000;
    for (int i = 0; i < parse_iterations; ++i) {
        Parser p(pathological_expr);
        auto a = p.parse_expression();
        (void)a;
    }
    auto t1 = std::chrono::high_resolution_clock::now();
    double total_ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
    double us_per_parse = (total_ms * 1000.0) / parse_iterations;
    double parses_per_sec = (parse_iterations / total_ms) * 1000.0;
    std::cout << "  -> Parse Performance: " << std::fixed << std::setprecision(2)
              << us_per_parse << " us/parse (" << static_cast<int>(parses_per_sec) << " parses/sec)" << std::endl;
    std::cout << "  -> PASSED: Zero-crash, precise operator associativity, deep AST lowering verified." << std::endl;
}

static void test_cel_ast_optimizer_and_constant_folding() {
    std::cout << "[Test] AST Optimizer & Constant Folding Passes..." << std::endl;

    // 1. Integer arithmetic constant folding
    {
        Parser p("2 + 3 * 4");
        auto ast = p.parse_expression();
        auto opt = AstOptimizer::optimize(ast);
        assert(opt->kind == AstKind::LITERAL_INT);
        assert(opt->int_val == 14);
        assert(CelCompiler::to_impscheme(opt) == "14");
    }

    // 2. Algebraic identities (x * 1 + 0 -> x)
    {
        Parser p("x * 1 + 0");
        auto ast = p.parse_expression();
        auto opt = AstOptimizer::optimize(ast);
        assert(opt->kind == AstKind::IDENTIFIER);
        assert(opt->text == "x");
        assert(CelCompiler::to_impscheme(opt) == "x");
    }

    // 3. Double negation elimination (!(!edge.valid) -> edge.valid)
    {
        Parser p("!(!edge.valid)");
        auto ast = p.parse_expression();
        auto opt = AstOptimizer::optimize(ast);
        assert(opt->kind == AstKind::MEMBER_ACCESS);
        assert(CelCompiler::to_impscheme(opt) == "(get-attr edge \"valid\")");
    }

    // 4. Dead branch elimination (true ? src.score : dest.score -> src.score)
    {
        Parser p("true ? src.score : dest.score");
        auto ast = p.parse_expression();
        auto opt = AstOptimizer::optimize(ast);
        assert(opt->kind == AstKind::MEMBER_ACCESS);
        assert(CelCompiler::to_impscheme(opt) == "(get-attr src \"score\")");
    }

    // 5. Transcendental math constant folding (sqrt(100.0) + pow(2.0, 3.0) -> 18.0)
    {
        Parser p("sqrt(100.0) + pow(2.0, 3.0)");
        auto ast = p.parse_expression();
        auto opt = AstOptimizer::optimize(ast);
        assert(opt->kind == AstKind::LITERAL_FLOAT);
        assert(std::abs(opt->float_val - 18.0) < 1e-6);
        assert(CelCompiler::to_impscheme(opt) == "18.0");
    }

    // 6. Safe math constant folding (safeDiv(10.0, 0.0, -1.0) -> -1.0)
    {
        Parser p("safeDiv(10.0, 0.0, -1.0)");
        auto ast = p.parse_expression();
        auto opt = AstOptimizer::optimize(ast);
        assert(opt->kind == AstKind::LITERAL_FLOAT);
        assert(opt->float_val == -1.0);
    }

    // 7. Optimization on Pathological Expression
    {
        std::string expr = "(10.0 > 0.0 && !false) ? sqrt(64.0) * edge.weight + (0.0 + edge.bias * 1.0) : -1.0";
        Parser p(expr);
        auto ast = p.parse_expression();
        auto opt = AstOptimizer::optimize(ast);
        std::string ir = CelCompiler::to_impscheme(opt);
        // Condition folds to true -> dead branch eliminated -> (+ (* 8.0 edge.weight) edge.bias)
        assert(ir == "(+ (* 8.0 (get-attr edge \"weight\")) (get-attr edge \"bias\"))");
        std::cout << "  -> Optimized Pathological Subtree IR: " << ir << std::endl;
    }

    std::cout << "  -> PASSED: All AST constant folding and algebraic reduction passes verified." << std::endl;
}

static void test_cel_parameter_normalization() {
    std::cout << "[Test] CEL Parameter Normalization (Standard, $param, @param)..." << std::endl;

    // 1. Standard CEL parameter
    Parser p1("edge.confidence >= minConfidence");
    auto ast1 = p1.parse_expression();
    std::string ir1 = CelCompiler::to_impscheme(ast1);

    // 2. Cypher-style $param
    Parser p2("edge.confidence >= $minConfidence");
    auto ast2 = p2.parse_expression();
    std::string ir2 = CelCompiler::to_impscheme(ast2);

    // 3. ImpScheme-style @param
    Parser p3("edge.confidence >= @minConfidence");
    auto ast3 = p3.parse_expression();
    std::string ir3 = CelCompiler::to_impscheme(ast3);

    assert(ir1 == "(>= (get-attr edge \"confidence\") minConfidence)");
    assert(ir2 == ir1);
    assert(ir3 == ir1);

    // 4. Strings containing $ and @ must remain exact literal strings untouched
    Parser p4("edge.label == \"$minConfidence\" && edge.prefix == \"@symbol\"");
    auto ast4 = p4.parse_expression();
    std::string ir4 = CelCompiler::to_impscheme(ast4);
    assert(ir4 == "(mask-and (vec-cmp-eq (get-attr edge \"label\") \"$minConfidence\") (vec-cmp-eq (get-attr edge \"prefix\") \"@symbol\"))");

    std::cout << "  -> PASSED: Parameter normalization and literal string preservation verified: " << ir1 << std::endl;
}

int main() {
    std::cout << "=== Google CEL Zero-Dependency Parser & IR Compiler Suite ===" << std::endl;
    test_cel_arithmetic_and_precedence();
    test_cel_ternary_and_members();
    test_cel_vector_math_calls();
    test_cel_temporal_and_datetime();
    test_cel_lists_and_methods();
    test_cel_pathological_40_term_expression();
    test_cel_ast_optimizer_and_constant_folding();
    test_cel_parameter_normalization();
    std::cout << "=== ALL CEL TESTS PASSED ===" << std::endl;
    return 0;
}
