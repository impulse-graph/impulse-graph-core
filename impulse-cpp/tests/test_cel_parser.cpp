/**
 * @file test_cel_parser.cpp
 * @brief Unit tests for Google CEL Zero-Dependency Pratt Parser & ImpScheme IR Lowering.
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
    std::cout << "[Test] CEL Analytical Vector Math Functions..." << std::endl;

    Parser p1("sqrt(x) + exp(y) * gelu(h)");
    auto ast1 = p1.parse_expression();
    assert(ast1 != nullptr);
    std::string ir1 = CelCompiler::to_impscheme(ast1);
    assert(ir1 == "(+ (sqrt x) (* (exp y) (gelu h)))");

    assert(CelCompiler::resolve_math_func("exp") == MATH_FUNC_EXP);
    assert(CelCompiler::resolve_math_func("gelu") == MATH_FUNC_GELU);
    assert(CelCompiler::resolve_math_func("clamp") == MATH_FUNC_CLAMP);
    assert(CelCompiler::resolve_math_func("sigmoid") == MATH_FUNC_SIGMOID);

    std::cout << "  -> PASSED: " << ir1 << std::endl;
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
    test_cel_lists_and_methods();
    std::cout << "=== ALL CEL TESTS PASSED ===" << std::endl;
    return 0;
}
