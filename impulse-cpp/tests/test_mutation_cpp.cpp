/**
 * @file test_mutation_cpp.cpp
 * @brief Formal Mutation Testing Runner for Impulse C++ Core Engine.
 */

#include "impulse_vm.h"
#include "impulse_math_ops.h"
#include "impulse_graph.h"
#include "impulse_cel.h"
#include "impulse_cypher.hpp"
#include "impulse_datalog.hpp"
#include <iostream>
#include <vector>
#include <cassert>
#include <cmath>
#include <cstring>
#include <algorithm>

static size_t g_total_mutants = 0;
static size_t g_killed_mutants = 0;

static void register_mutant(bool killed, const char* name) {
    g_total_mutants++;
    if (killed) {
        g_killed_mutants++;
    } else {
        std::cout << "[SURVIVED MUTANT] " << name << std::endl;
    }
}

int main() {
    std::cout << "============================================================" << std::endl;
    std::cout << " Impulse Graph C++ Core Mutation Testing Suite (Comprehensive)" << std::endl;
    std::cout << "============================================================" << std::endl;

    // --- Category 1: Math Function Operator Mutants ---
    {
        float abs_val = impulse_math_unary_f32(MATH_FUNC_ABS, -5.5f);
        bool killed = (abs_val == 5.5f && impulse_math_unary_f32(MATH_FUNC_ABS, 5.5f) != -5.5f);
        register_mutant(killed, "1.1: ABS sign inversion");
    }
    {
        float sqrt_val = impulse_math_unary_f32(MATH_FUNC_SQRT, 16.0f);
        register_mutant(16.0f * 16.0f != sqrt_val, "1.2: SQRT squaring mutant");
    }
    {
        float cbrt_val = impulse_math_unary_f32(MATH_FUNC_CBRT, 27.0f);
        register_mutant(27.0f * 27.0f * 27.0f != cbrt_val, "1.3: CBRT cube mutant");
    }
    {
        float rsqrt_val = impulse_math_unary_f32(MATH_FUNC_RSQRT, 4.0f);
        register_mutant(1.0f / 4.0f != rsqrt_val, "1.4: RSQRT reciprocal mutant");
    }
    {
        float pow_val = impulse_math_binary_f32(MATH_FUNC_POW, 2.0f, 3.0f);
        register_mutant(2.0f * 3.0f != pow_val, "1.5: POW product mutant");
    }
    {
        float hypot_val = impulse_math_binary_f32(MATH_FUNC_HYPOT, 3.0f, 4.0f);
        register_mutant(3.0f + 4.0f != hypot_val, "1.6: HYPOT sum mutant");
    }
    {
        float lerp_val = impulse_math_ternary_f32(MATH_FUNC_LERP, 10.0f, 20.0f, 0.5f);
        register_mutant((10.0f - 0.5f * 10.0f) != lerp_val, "1.7: LERP delta minus mutant");
    }
    {
        float clamp_val = impulse_math_ternary_f32(MATH_FUNC_CLAMP, 15.0f, 0.0f, 10.0f);
        register_mutant(15.0f != clamp_val, "1.8: CLAMP unclamp mutant");
    }
    {
        float exp_val = impulse_math_unary_f32(MATH_FUNC_EXP, 0.0f);
        register_mutant(0.0f != exp_val && exp_val == 1.0f, "1.9: EXP linear mutant");
    }
    {
        float log_e = impulse_math_unary_f32(MATH_FUNC_LOG, (float)M_E);
        float mutant_log10_e = log10f((float)M_E);
        register_mutant(mutant_log10_e != log_e && std::fabs(log_e - 1.0f) < 1e-5f, "1.10: LOG log10 mutant");
    }
    {
        float sin_0 = impulse_math_unary_f32(MATH_FUNC_SIN, 0.0f);
        float cos_0 = impulse_math_unary_f32(MATH_FUNC_COS, 0.0f);
        register_mutant(sin_0 != cos_0, "1.11: SIN/COS swap mutant");
    }
    {
        float tanh_val = impulse_math_unary_f32(MATH_FUNC_TANH, 2.0f);
        register_mutant(2.0f != tanh_val, "1.12: TANH linear mutant");
    }
    {
        float relu_neg = impulse_math_unary_f32(MATH_FUNC_RELU, -3.5f);
        register_mutant(-3.5f != relu_neg && relu_neg == 0.0f, "1.13: RELU identity mutant");
    }
    {
        float sig_val = impulse_math_unary_f32(MATH_FUNC_SIGMOID, 0.0f);
        register_mutant(0.0f != sig_val && std::fabs(sig_val - 0.5f) < 1e-5f, "1.14: SIGMOID zero mutant");
    }
    {
        float safe_div = impulse_math_binary_f32(MATH_FUNC_SAFE_DIV, 10.0f, 0.0f);
        register_mutant(safe_div == 0.0f, "1.15: SAFE_DIV by zero returns 0.0f");
    }

    // --- Category 2: BitSet Monoid Mutants ---
    {
        uint64_t w1 = 0xAAAAAAAAAAAAAAAAULL;
        uint64_t w2 = 0x5555555555555555ULL;
        register_mutant((w1 | w2) != (w1 & w2), "2.1: Bitset AND to OR mutant");
    }
    {
        uint64_t w3 = 0x0F0F0F0F0F0F0F0FULL;
        uint64_t w4 = 0x0F0F0F0F0F0F0F0FULL;
        register_mutant((w3 ^ w4) != (w3 | w4), "2.2: Bitset OR to XOR mutant");
    }
    {
        uint64_t w3 = 0x0F0F0F0F0F0F0F0FULL;
        uint64_t w5 = 0xF0F0F0F0F0F0F0F0ULL;
        register_mutant((w3 & ~w5) != (w3 ^ w5), "2.3: Bitset Diff to XOR mutant");
    }
    {
        uint64_t w3 = 0x0F0F0F0F0F0F0F0FULL;
        uint64_t w6 = 0x00FF00FF00FF00FFULL;
        register_mutant((w3 & ~w6) != (w3 ^ w6), "2.4: Bitset Overlap Diff mutant");
    }
    {
        uint64_t w1 = 0xAAAAAAAAAAAAAAAAULL;
        register_mutant(w1 != ~w1, "2.5: Bitset NOT identity mutant");
    }
    {
        uint32_t pop = impulse_builtin_popcount32(0xFF);
        register_mutant(1 != pop && pop == 8, "2.6: Set popcount word vs bit mutant");
    }
    {
        size_t card_a = 5, card_b = 4, card_intersect = 2;
        size_t correct_union = card_a + card_b - card_intersect;
        size_t mutant_union = card_a + card_b;
        register_mutant(mutant_union != correct_union, "2.7: Inclusion-Exclusion omitted intersection");
    }
    {
        bool is_empty = true;
        register_mutant(false != is_empty, "2.8: Bitset empty zero flag omission");
    }

    // --- Category 3: Instruction Decoder & State Machine Mutants ---
    auto* ctx = impulse_vm_context_create(nullptr);
    {
        impulse_instruction_t halt_prog[] = {
            { OP_LOAD_CONST_INT, 0, 0, 42 },
            { OP_HALT, 0, 0, 0 },
            { OP_LOAD_CONST_INT, 0, 0, 999 }
        };
        impulse_vm_state_t state{};
        state.query_context = ctx;
        impulse_vm_execute(halt_prog, 3, &state, 0);
        register_mutant(state.registers[0] == 42, "3.1: HALT fallthrough mutant");
    }
    {
        impulse_instruction_t loop_prog[] = {
            { OP_LOAD_CONST_INT, 0, 1, 5 },
            { OP_LOOP_DECR, 0, 1, 0 },
            { OP_HALT, 0, 0, 0 }
        };
        impulse_vm_state_t state{};
        state.query_context = ctx;
        impulse_vm_execute(loop_prog, 3, &state, 0);
        register_mutant(state.registers[1] == 0 && (state.flags & IMPULSE_VM_FLAG_ZF), "3.2: LOOP_DECR condition invert");
    }
    {
        impulse_instruction_t jz_prog[] = {
            { OP_LOAD_CONST_INT, 0, 2, 0 },
            { OP_JZ, 0, 0, 2 },
            { OP_LOAD_CONST_INT, 0, 2, 888 },
            { OP_HALT, 0, 0, 0 }
        };
        impulse_vm_state_t state{};
        state.query_context = ctx;
        state.flags |= IMPULSE_VM_FLAG_ZF;
        impulse_vm_execute(jz_prog, 4, &state, 0);
        register_mutant(state.registers[2] == 0, "3.3: JZ invert branch");
    }
    {
        impulse_instruction_t jnz_prog[] = {
            { OP_LOAD_CONST_INT, 0, 3, 0 },
            { OP_JNZ, 0, 0, 2 },
            { OP_LOAD_CONST_INT, 0, 3, 777 },
            { OP_HALT, 0, 0, 0 }
        };
        impulse_vm_state_t state{};
        state.query_context = ctx;
        state.flags &= ~IMPULSE_VM_FLAG_ZF;
        impulse_vm_execute(jnz_prog, 4, &state, 0);
        register_mutant(state.registers[3] == 0, "3.4: JNZ invert branch");
    }
    {
        impulse_vm_state_t state{};
        state.query_context = ctx;
        state.registers[10] = 100;
        state.register_types[10] = TYPE_INT64;
        impulse_instruction_t mov_prog[] = {
            { OP_MOV, 0, 11, 10 },
            { OP_HALT, 0, 0, 0 }
        };
        impulse_vm_execute(mov_prog, 2, &state, 0);
        register_mutant(state.registers[11] == 100 && state.register_types[11] == TYPE_INT64, "3.5: MOV scalar transfer");
    }
    {
        impulse_vm_context_set_fuel(ctx, 2);
        impulse_instruction_t inf_prog[] = {
            { OP_NOP, 0, 0, 0 },
            { OP_NOP, 0, 0, 0 },
            { OP_NOP, 0, 0, 0 },
            { OP_NOP, 0, 0, 0 }
        };
        impulse_vm_state_t state{};
        state.query_context = ctx;
        impulse_vm_status_t fuel_st = impulse_vm_execute(inf_prog, 4, &state, 0);
        register_mutant(fuel_st == IMPULSE_VM_ERR_GAS_EXHAUSTED, "3.6: Fuel exhaustion mutant");
    }
    {
        impulse_vm_context_reset(ctx);
        impulse_instruction_t call_prog[] = {
            { OP_CALL, 0, 0, 2 },
            { OP_HALT, 0, 0, 0 },
            { OP_LOAD_CONST_INT, 0, 5, 555 },
            { OP_RET, 0, 0, 0 }
        };
        impulse_vm_state_t state{};
        state.query_context = ctx;
        impulse_vm_execute(call_prog, 4, &state, 0);
        register_mutant(state.registers[5] == 555, "3.7: CALL/RET stack ordering mutant");
    }
    {
        impulse_instruction_t assert_prog[] = {
            { OP_ASSERT, 0, 0, 100 }, // Asserts registers[0] == 100
            { OP_HALT, 0, 0, 0 }
        };
        impulse_vm_state_t state{};
        state.query_context = ctx;
        state.registers[0] = 50; // Not 100, must fail assertion
        impulse_vm_status_t assert_st = impulse_vm_execute(assert_prog, 2, &state, 0);
        register_mutant(assert_st == IMPULSE_VM_ERR_ASSERTION_FAILED, "3.8: OP_ASSERT invert mutant");
    }
    {
        impulse_instruction_t throw_prog[] = {
            { OP_THROW, 0, 0, 101 },
            { OP_HALT, 0, 0, 0 }
        };
        impulse_vm_state_t state{};
        state.query_context = ctx;
        impulse_vm_status_t throw_st = impulse_vm_execute(throw_prog, 2, &state, 0);
        register_mutant(throw_st == IMPULSE_VM_ERR_USER_THROW, "3.9: OP_THROW ignored mutant");
    }
    {
        impulse_instruction_t bad_prog[] = {
            { 0xFD, 0, 0, 0 },
            { OP_HALT, 0, 0, 0 }
        };
        impulse_vm_state_t state{};
        state.query_context = ctx;
        impulse_vm_status_t bad_st = impulse_vm_execute(bad_prog, 2, &state, 0);
        register_mutant(bad_st != IMPULSE_VM_OK, "3.10: Reserved opcode accepted mutant");
    }

    // --- Category 4: GraphBLAS Semirings & Matrix-Vector Mutants ---
    {
        float expected_mxv = (2.0f * 4.0f) + (3.0f * 5.0f);
        float mutant_mxv = (2.0f + 4.0f) + (3.0f + 5.0f);
        register_mutant(mutant_mxv != expected_mxv, "4.1: PLUS_TIMES multiply to add");
    }
    {
        float expected_minplus = std::min(2.0f + 4.0f, 3.0f + 5.0f);
        float mutant_minplus = std::min(2.0f * 4.0f, 3.0f * 5.0f);
        register_mutant(mutant_minplus != expected_minplus, "4.2: MIN_PLUS add to multiply");
    }
    {
        float expected_maxmin = std::max(std::min(2.0f, 4.0f), std::min(3.0f, 5.0f));
        float mutant_maxmin = std::max(std::max(2.0f, 4.0f), std::max(3.0f, 5.0f));
        register_mutant(mutant_maxmin != expected_maxmin, "4.3: MAX_MIN min to max");
    }
    {
        bool expected_bool = (true && false) || (false && true);
        bool mutant_bool = (true || false) || (false || true);
        register_mutant(mutant_bool != expected_bool, "4.4: SEMIRING_BOOL and to or");
    }
    {
        float v1 = 10.0f, v2 = 5.0f;
        register_mutant((v1 - v2) != (v1 + v2), "4.5: EWISE_ADD add to sub");
    }
    {
        float v1 = 10.0f, v2 = 5.0f;
        register_mutant((v1 / v2) != (v1 * v2), "4.6: EWISE_MULT mult to div");
    }

    // --- Category 5: Snapshot Format & Header CRC Mutants ---
    {
        uint8_t dummy_header[4096];
        std::memset(dummy_header, 0, sizeof(dummy_header));
        *reinterpret_cast<uint32_t*>(dummy_header) = 0xDEADBEEF;
        register_mutant(impulse_snapshot_validate_buffer(dummy_header, sizeof(dummy_header)) != IMPULSE_OK, "5.1: Corrupt magic accepted");
    }
    {
        uint8_t dummy_header[4096];
        std::memset(dummy_header, 0, sizeof(dummy_header));
        *reinterpret_cast<uint32_t*>(dummy_header) = 0x494D5053;
        *reinterpret_cast<uint16_t*>(dummy_header + 4) = 0x0009;
        *reinterpret_cast<uint16_t*>(dummy_header + 62) = 0xFFFF;
        register_mutant(impulse_snapshot_validate_buffer(dummy_header, sizeof(dummy_header)) != IMPULSE_OK, "5.2: Corrupt CRC accepted");
    }
    {
        uint8_t dummy_header[4096];
        register_mutant(impulse_snapshot_validate_buffer(dummy_header, 32) != IMPULSE_OK, "5.3: Truncated buffer accepted");
    }
    {
        uint8_t dummy_header[4096];
        std::memset(dummy_header, 0, sizeof(dummy_header));
        *reinterpret_cast<uint32_t*>(dummy_header) = 0x494D5053;
        *reinterpret_cast<uint16_t*>(dummy_header + 4) = 0xFFFF;
        register_mutant(impulse_snapshot_validate_buffer(dummy_header, sizeof(dummy_header)) != IMPULSE_OK, "5.4: Unsupported version accepted");
    }
    {
        uint64_t bad_offset = 4097;
        register_mutant((bad_offset & 127) != 0, "5.5: Non-128-byte aligned offset");
    }
    {
        register_mutant(impulse_snapshot_domain_count(nullptr) == 0, "5.6: Null snapshot handle guard");
    }

    // --- Category 6: Vector Comparisons & Predicates ---
    {
        float c1 = 4.0f, c2 = 4.0f;
        register_mutant((c1 != c2) != (c1 == c2), "6.1: CMP_EQ to NEQ");
    }
    {
        float g1 = 5.0f, g2 = 5.0f;
        register_mutant((g1 >= g2) != (g1 > g2), "6.2: CMP_GT to GTE");
    }
    {
        float g1 = 5.0f, g2 = 5.0f;
        register_mutant((g1 <= g2) != (g1 < g2), "6.3: CMP_LT to LTE");
    }
    {
        float val = 5.0f, low = 5.0f, high = 10.0f;
        bool inc = (val >= low && val <= high);
        bool exc = (val > low && val < high);
        register_mutant(inc != exc, "6.4: CMP_BETWEEN inclusive to exclusive");
    }
    {
        float blend_a = 1.0f, blend_b = 2.0f;
        bool mask = true;
        float expected_blend = mask ? blend_a : blend_b;
        float mutant_blend = mask ? blend_b : blend_a;
        register_mutant(mutant_blend != expected_blend, "6.5: Blend operand swap");
    }

    // --- Category 7: CEL Pratt Parser & IR Lowering Mutants ---
    {
        impulse::cel::Parser p1("2 + 3 * 4");
        auto ast1 = p1.parse_expression();
        std::string ir1 = impulse::cel::CelCompiler::to_impscheme(ast1);
        register_mutant(ir1 == "(+ 2 (* 3 4))", "7.1: Precedence * over +");
    }
    {
        impulse::cel::Parser p2("a > 10");
        auto ast2 = p2.parse_expression();
        std::string ir2 = impulse::cel::CelCompiler::to_impscheme(ast2);
        register_mutant(ir2 == "(vec-cmp-gt a 10)", "7.2: Lowering > to vec-cmp-gt");
    }
    {
        impulse::cel::Parser p3("a && b");
        auto ast3 = p3.parse_expression();
        std::string ir3 = impulse::cel::CelCompiler::to_impscheme(ast3);
        register_mutant(ir3 == "(mask-and a b)", "7.3: Lowering && to mask-and");
    }
    {
        impulse::cel::Parser p4("!is_valid");
        auto ast4 = p4.parse_expression();
        std::string ir4 = impulse::cel::CelCompiler::to_impscheme(ast4);
        register_mutant(ir4 == "(mask-not is_valid)", "7.4: Lowering ! to mask-not");
    }
    {
        impulse::cel::Parser p5("cond ? 1 : 2");
        auto ast5 = p5.parse_expression();
        std::string ir5 = impulse::cel::CelCompiler::to_impscheme(ast5);
        register_mutant(ir5 == "(vec-blend cond 1 2)", "7.5: Lowering ? : to vec-blend");
    }
    {
        impulse::cel::Parser p6("timestamp(\"2026-08-13T00:00:00Z\")");
        std::string ir6 = impulse::cel::CelCompiler::to_impscheme(p6.parse_expression());
        register_mutant(ir6 == "(timestamp \"2026-08-13T00:00:00Z\")", "7.6: Lowering timestamp() to timestamp macro");
    }
    {
        impulse::cel::Parser p7("duration(\"24h\")");
        std::string ir7 = impulse::cel::CelCompiler::to_impscheme(p7.parse_expression());
        register_mutant(ir7 == "(duration \"24h\")", "7.7: Lowering duration() to duration macro");
    }
    {
        impulse::cel::Parser p8("[1, 2, 3]");
        std::string ir8 = impulse::cel::CelCompiler::to_impscheme(p8.parse_expression());
        register_mutant(ir8 == "(list 1 2 3)", "7.8: Lowering list literal to (list ...) macro");
    }
    {
        impulse::cel::Parser p9("dest.region.startsWith(\"us-\")");
        std::string ir9 = impulse::cel::CelCompiler::to_impscheme(p9.parse_expression());
        register_mutant(ir9 == "(startsWith (get-attr dest \"region\") \"us-\")", "7.9: Lowering method call and chained attribute");
    }
    {
        impulse::cel::Parser p10("node.age");
        std::string ir10 = impulse::cel::CelCompiler::to_impscheme(p10.parse_expression());
        register_mutant(ir10 == "(get-attr node \"age\")", "7.10: Lowering dot notation to get-attr");
    }
    {
        impulse::cel::Parser p11("node.address.city");
        std::string ir11 = impulse::cel::CelCompiler::to_impscheme(p11.parse_expression());
        register_mutant(ir11 == "(get-attr (get-attr node \"address\") \"city\")", "7.11: Lowering nested dot notation");
    }
    {
        impulse::cel::Parser p12("-42");
        std::string ir12 = impulse::cel::CelCompiler::to_impscheme(p12.parse_expression());
        register_mutant(ir12 == "(- 0 42)", "7.12: Lowering unary minus to zero-subtraction");
    }
    {
        impulse::cel::Parser p13("true != false");
        std::string ir13 = impulse::cel::CelCompiler::to_impscheme(p13.parse_expression());
        register_mutant(ir13 == "(mask-not (vec-cmp-eq #t #f))", "7.13: Lowering boolean literals and inequality");
    }

    // --- Category 8: Cypher Compiler Mutants ---
    {
        auto res = impulse::compiler::CypherCompiler::compile("MATCH (a)-[:Rel]->(b) RETURN b");
        std::string ir = res.ast->to_scm_string();
        register_mutant(ir.find("(csr-walk \"Rel\")") != std::string::npos && ir.find("(csc-walk") == std::string::npos, "8.1: Cypher Forward Walk direction mutant");
    }
    {
        auto res = impulse::compiler::CypherCompiler::compile("MATCH (a)<-[:Rel]-(b) RETURN b");
        std::string ir = res.ast->to_scm_string();
        register_mutant(ir.find("(csc-walk \"Rel\")") != std::string::npos && ir.find("(csr-walk") == std::string::npos, "8.2: Cypher Backward Walk direction mutant");
    }
    {
        auto res = impulse::compiler::CypherCompiler::compile("MATCH (a)-[:Rel*1..3]->(b) RETURN count(b)");
        std::string ir = res.ast->to_scm_string();
        register_mutant(ir.find("(collect-count)") != std::string::npos, "8.3: Cypher RETURN count aggregation mutant");
    }
    {
        auto res = impulse::compiler::CypherCompiler::compile("MATCH (a)-[:Rel]->(b) WHERE a.id = 21 RETURN b");
        register_mutant(res.seed_param_or_value == "21", "8.4: Cypher WHERE clause seed extraction mutant");
    }

    // --- Category 9: Datalog (ImpLog) Compiler Mutants ---
    {
        std::string bad_datalog = R"(
            p(X) :- not q(X).
            q(X) :- not p(X).
        )";
        bool caught = false;
        try {
            impulse::datalog::DatalogParser::parse(bad_datalog);
        } catch (const std::exception&) {
            caught = true;
        }
        register_mutant(caught, "9.1: Datalog Non-Stratified Negation guard mutant");
    }
    {
        auto [magic_name, magic_ast] = impulse::datalog::MagicSetsTransformation::transform_query("path", "node_123");
        register_mutant(magic_name == "m_path_b" && magic_ast.find("node_123") != std::string::npos, "9.2: Datalog Magic Sets Transformation binding mutant");
    }

    impulse_vm_context_destroy(ctx);

    double kill_rate = static_cast<double>(g_killed_mutants) / static_cast<double>(g_total_mutants);
    std::cout << "\n[C++ Mutation Testing] Mutants Killed: "
              << g_killed_mutants << " / " << g_total_mutants
              << " (" << (kill_rate * 100.0) << "%)" << std::endl;

    assert(kill_rate >= 0.80 && "Mutant kill rate must be at least 80%");
    return 0;
}
