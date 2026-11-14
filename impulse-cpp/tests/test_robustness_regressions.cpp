/**
 * @file test_robustness_regressions.cpp
 * @brief Regression tests for defects found by the sanitizer/fuzz campaign (v0.9.1 P1).
 *
 * Meaningful under IMPULSE_SANITIZE (UBSan findings are fatal); also asserts functional behavior
 * so it is a valid test in plain builds.
 */

#undef NDEBUG
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>

#include "impulse_cel.h"
#include "impulse_compiler.hpp"
#include "impulse_vm.h"

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

        // ImpScheme SExprParser: mismatched closing delimiters (e.g. `]` inside `(...)` or stray `)`)
        // previously failed to advance `pos` and hung in an infinite loop pushing empty SExpr nodes.
        for (const char* bad_scm : {
                 "(pipeline (lane-prune-mask 2 0 0) (lDaG]->(g:Gene)",
                 "(a ] b)",
                 "[a ) b]",
                 ")",
                 "]",
             }) {
            st = impulse_compile_query(nullptr, bad_scm, IMPULSE_LANG_IMPSCM, instrs, 64, &count);
            assert(st != IMPULSE_OK);
        }
    }

    // VM register-type check (fuzz_vm_bytecode crash PT0UPT09PYBD9wlQBAAAAA==):
    // Out-of-range register_types tag (>= 32, e.g. 41) previously triggered UBSan shift-exponent >= 32
    // in IMPULSE_CHECK_REG_TYPE during OP_EWISE_ADD (0x43). Must return IMPULSE_VM_ERR_INVALID_REGISTER.
    {
        impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr);
        assert(ctx != nullptr);
        impulse_vm_state_t vm{};
        vm.query_context = ctx;
        vm.register_types[4] = 41;
        impulse_instruction_t prog[2] = {
            {0x43, 0x09, 0x0050, 0x00000004u},
            {OP_HALT, 0, 0, 0},
        };
        impulse_vm_status_t vms = impulse_vm_execute(prog, 2, &vm, 100);
        assert(vms == IMPULSE_VM_ERR_INVALID_REGISTER);
        impulse_vm_context_destroy(ctx);
    }

    // OP_CSC_WALK unv >= 64 regression (fuzz_vm_bytecode crash in CI):
    // When OP_CSC_WALK (0x18) encodes unv >= 64 (e.g. param 0x00000040), register_types[unv]
    // must not be indexed out of bounds.
    {
        impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr);
        assert(ctx != nullptr);
        impulse_vm_state_t vm{};
        vm.query_context = ctx;
        vm.registers[0] = 0;
        vm.register_types[0] = TYPE_INT64;
        impulse_instruction_t prog[2] = {
            {0x18, 0x01, 0x0009, 0x00000040u},
            {OP_HALT, 0, 0, 0},
        };
        impulse_vm_status_t vms = impulse_vm_execute(prog, 2, &vm, 100);
        assert(vms != IMPULSE_VM_OK);
        impulse_vm_context_destroy(ctx);
    }

    // OP_MAP_KEYS_TO_DENSE (0x05) invalid/misaligned input_param regression:
    // Passing a small scalar like 7 must return IMPULSE_VM_ERR_OUT_OF_BOUNDS instead of dereferencing 0x7.
    {
        impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr);
        assert(ctx != nullptr);
        impulse_vm_state_t vm{};
        vm.query_context = ctx;
        impulse_instruction_t prog[2] = {
            {OP_MAP_KEYS_TO_DENSE, 0x00, 0x0001, 0u},
            {OP_HALT, 0, 0, 0},
        };
        impulse_vm_status_t vms = impulse_vm_execute(prog, 2, &vm, 7ULL);
        assert(vms == IMPULSE_VM_ERR_OUT_OF_BOUNDS);
        impulse_vm_context_destroy(ctx);
    }

    // ImpulseCompiler::compile null guard regression:
    // Passing nullptr program must throw std::invalid_argument cleanly without crashing.
    {
        bool threw = false;
        try {
            impulse::compiler::ImpulseCompiler::compile(nullptr);
        } catch (const std::invalid_argument&) {
            threw = true;
        }
        assert(threw);
    }

    // OP_SET_UNION / OP_SET_INTERSECT / OP_SET_DIFFERENCE invalid source bitset handle regression:
    // Out-of-bounds TYPE_BITSET_HANDLE in src1/src2 must return IMPULSE_VM_ERR_OUT_OF_BOUNDS.
    for (uint8_t op : {uint8_t{OP_SET_UNION}, uint8_t{OP_SET_INTERSECT}, uint8_t{OP_SET_DIFFERENCE}}) {
        impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr);
        assert(ctx != nullptr);
        impulse_vm_state_t vm{};
        vm.query_context = ctx;
        vm.registers[0] = 99999ULL;
        vm.register_types[0] = TYPE_BITSET_HANDLE;
        vm.registers[1] = 1ULL;
        vm.register_types[1] = TYPE_NODE_ID;
        impulse_instruction_t prog[2] = {
            {op, 0x00, 2, static_cast<uint32_t>(0 | (1 << 16))},
            {OP_HALT, 0, 0, 0},
        };
        impulse_vm_status_t vms = impulse_vm_execute(prog, 2, &vm, 100);
        assert(vms == IMPULSE_VM_ERR_OUT_OF_BOUNDS);
        impulse_vm_context_destroy(ctx);
    }

    std::printf("test_robustness_regressions: PASS\n");
    return 0;
}
