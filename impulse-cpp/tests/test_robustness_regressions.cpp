/**
 * @file test_robustness_regressions.cpp
 * @brief Regression tests for defects found by the sanitizer/fuzz campaign (v0.9.1 P1).
 *
 * Meaningful under IMPULSE_SANITIZE (UBSan findings are fatal); also asserts functional behavior
 * so it is a valid test in plain builds.
 */

#include "impulse_cel.h"
#include "impulse_vm.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>

#undef NDEBUG

static std::shared_ptr<impulse::cel::AstNode> optimize(const char* expr) {
    impulse::cel::Parser p(expr);
    auto ast = p.parse();
    assert(ast);
    return impulse::cel::AstOptimizer::optimize(ast);
}

int main() {
    using impulse::cel::AstKind;

    // CEL constant folding: signed int64 overflow was UB (impulse_cel.h fold_binary); now wraps.
    {
        auto n = optimize("9223372036854775807 * 2");
        assert(n->kind == AstKind::LITERAL_INT && n->int_val == -2);
        n = optimize("9223372036854775807 + 1");
        assert(n->kind == AstKind::LITERAL_INT && n->int_val == INT64_MIN);
        n = optimize("-9223372036854775807 - 2");
        assert(n->kind == AstKind::LITERAL_INT && n->int_val == INT64_MAX);
        // INT64_MIN / -1 and INT64_MIN % -1 trap in hardware; must fold without UB.
        n = optimize("(-9223372036854775807 - 1) / -1");
        assert(n->kind == AstKind::LITERAL_INT && n->int_val == INT64_MIN);
        n = optimize("(-9223372036854775807 - 1) % -1");
        assert(n->kind == AstKind::LITERAL_INT && n->int_val == 0);
        n = optimize("-(-9223372036854775807 - 1)");
        assert(n->kind == AstKind::LITERAL_INT && n->int_val == INT64_MIN);
    }

    // CEL parser: sub-expression parse failures leave nullptr children; parse() must reject
    // the tree instead of letting AstOptimizer/CelCompiler dereference null.
    for (const char* bad : {"sqrt(x * )", "f(1, )", "a.b(c, )", "sqrt(x * ix +", "max(1, 2, )"}) {
        impulse::cel::Parser p(bad);
        assert(p.parse() == nullptr);
    }
    {
        impulse::cel::Parser p("sqrt(x * 2) < 50.0");
        assert(p.parse() != nullptr);
    }

    // C API CEL front end: malformed expressions must produce an error status, not a null ImpScheme
    // step (previously null-dereferenced in ImpulseCompiler::pass_direction_selection).
    {
        impulse_instruction_t instrs[64];
        size_t count = 0;
        impulse_status_t st = impulse_compile_query(nullptr, "age > 21 && score * ", IMPULSE_LANG_CEL, instrs, 64,
                                                    &count);
        assert(st != IMPULSE_OK);
        st = impulse_compile_query(nullptr, "sqrt(x * )", IMPULSE_LANG_CEL, instrs, 64, &count);
        assert(st != IMPULSE_OK);
    }

    std::printf("test_robustness_regressions: PASS\n");
    return 0;
}
