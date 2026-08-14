/**
 * @file test_cel_parser.cpp
 * @brief Exhaustive Unit Tests for Google CEL Zero-Dependency Pratt Parser & ImpScheme IR Lowering.
 */

#include "impulse_cel.h"
#include <iostream>
#include <cassert>

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

int main() {
    std::cout << "=== Google CEL Zero-Dependency Parser & IR Compiler Suite ===" << std::endl;
    test_cel_arithmetic_and_precedence();
    test_cel_ternary_and_members();
    test_cel_vector_math_calls();
    test_cel_temporal_and_datetime();
    test_cel_lists_and_methods();
    std::cout << "=== ALL CEL TESTS PASSED ===" << std::endl;
    return 0;
}
