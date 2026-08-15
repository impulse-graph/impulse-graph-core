/**
 * @file test_cpp_compiler_parity.cpp
 * @brief Comprehensive Verification Test for C++ Optimizing Compiler Parity.
 */

#include "impulse_compiler.hpp"
#include "impulse_vm.h"
#include "impulse_graph.h"

#include <iostream>
#include <cassert>
#include <vector>
#include <string>
#include <cstdio>

using namespace impulse::compiler;

static void test_ast_construction_and_serialization() {
    std::cout << "--- [Test 1] AST Construction & ImpScheme S-Expression Output ---" << std::endl;

    auto prog = ScmProgram::of(
        ScmWalk::forward("DaG"),
        ScmWalk::forward("GpPW"),
        ScmWalk::reverse("GpPW"),
        ScmWalk::reverse("CbG"),
        ScmCollect::bitset()
    );

    std::string s_expr = prog->to_scm_string();
    std::cout << s_expr << "\n" << std::endl;

    assert(s_expr.find("(program") != std::string::npos);
    assert(s_expr.find("(csr-walk \"DaG\")") != std::string::npos);
    assert(s_expr.find("(csr-walk \"GpPW\")") != std::string::npos);
    assert(s_expr.find("(csc-walk \"GpPW\")") != std::string::npos);
    assert(s_expr.find("(csc-walk \"CbG\")") != std::string::npos);
    assert(s_expr.find("(collect-bitset)") != std::string::npos);

    std::cout << "  ✓ AST Construction & S-Expression Output: PASSED\n" << std::endl;
}

static void test_parameter_binding_and_constant_folding() {
    std::cout << "--- [Test 2] Parameter Binding & Constant Folding ---" << std::endl;

    auto prog = ScmProgram::of(
        ScmWalk::forward("DaG", std::make_shared<ScmVectorFilter>("confidence", CompareOp::GTE, std::make_shared<ScmSymbol>("@minConfidence"))),
        ScmCollect::bitset()
    );

    std::unordered_map<std::string, double> params = {
        { "@minConfidence", 0.85 }
    };

    auto compiled = ImpulseCompiler::compile(prog, nullptr, params);
    std::string s_expr = compiled.optimized_ast->to_scm_string();
    std::cout << "Optimized AST:\n" << s_expr << "\n" << std::endl;

    assert(s_expr.find("0.85") != std::string::npos);
    assert(s_expr.find("@minConfidence") == std::string::npos);

    std::cout << "  ✓ Parameter Binding: PASSED\n" << std::endl;
}

static void test_kernel_fusion_2hop() {
    std::cout << "--- [Test 3] 2-Hop Kernel Fusion Optimization ---" << std::endl;

    // Test A: Enabled Fusion (multiplicity <= 1.5)
    auto prog = ScmProgram::of(
        ScmWalk::forward("DaG"),
        ScmWalk::forward("GpPW"),
        ScmCollect::bitset()
    );

    CompilerOptions opt_fused = CompilerOptions::default_options();
    opt_fused.enable_kernel_fusion = true;
    opt_fused.fused_2hop_max_multiplicity_threshold = 2.0;

    auto compiled = ImpulseCompiler::compile(prog, nullptr, {}, opt_fused);
    std::string disasm = compiled.to_impas_string();
    std::cout << "Disassembly with 2-Hop Fusion:\n" << disasm << std::endl;

    assert(disasm.find("OP_CSR_WALK_2HOP") != std::string::npos);
    assert(disasm.find("[seed-inlined]") != std::string::npos);
    assert(disasm.find("[early-exit]") != std::string::npos);
    assert(compiled.instruction_count() == 3); // OP_CSR_WALK_2HOP, OP_COLLECT_BITSET, OP_HALT

    // Test B: Disabled Fusion (ablation check)
    CompilerOptions opt_nofuse = CompilerOptions::default_options();
    opt_nofuse.enable_kernel_fusion = false;

    auto compiled_nofuse = ImpulseCompiler::compile(prog, nullptr, {}, opt_nofuse);
    std::string disasm_nofuse = compiled_nofuse.to_impas_string();
    std::cout << "Disassembly without Fusion (Hop-Hop):\n" << disasm_nofuse << std::endl;

    assert(disasm_nofuse.find("OP_CSR_WALK_2HOP") == std::string::npos);
    assert(compiled_nofuse.instruction_count() == 4); // OP_CSR_WALK, OP_CSR_WALK, OP_COLLECT, OP_HALT

    std::cout << "  ✓ 2-Hop Kernel Fusion Ablation: PASSED\n" << std::endl;
}

static void test_register_cache_ping_ponging() {
    std::cout << "--- [Test 4] Register Cache Ping-Ponging (R0 <-> R1) ---" << std::endl;

    auto prog = ScmProgram::of(
        ScmWalk::forward("DaG"),
        ScmWalk::forward("GpPW"),
        ScmWalk::reverse("GpPW"),
        ScmWalk::reverse("CbG"),
        ScmCollect::bitset()
    );

    CompilerOptions opts = CompilerOptions::default_options();
    opts.enable_kernel_fusion = false; // test 4 discrete walks
    opts.enable_register_ping_pong = true;

    auto compiled = ImpulseCompiler::compile(prog, nullptr, {}, opts);
    std::string disasm = compiled.to_impas_string();
    std::cout << "Disassembly (4 Hops with R0 <-> R1 Ping-Pong):\n" << disasm << std::endl;

    // Check register sequence: R0 -> R1, R1 -> R0, R0 -> R1, R1 -> R0
    assert(compiled.instructions[0].dst_reg == 1);
    assert(compiled.instructions[1].dst_reg == 0);
    assert(compiled.instructions[2].dst_reg == 1);
    assert(compiled.instructions[3].dst_reg == 0);

    std::cout << "  ✓ Register Cache Ping-Ponging: PASSED\n" << std::endl;
}

static void test_execution_on_vm() {
    std::cout << "--- [Test 5] Live Execution of Compiled Bytecode on Impulse VM ---" << std::endl;

    const char* snap_path = "test_parity_mock.imps";
    impulse_writer_t* writer = impulse_writer_create(snap_path, 0);
    assert(writer != nullptr);

    impulse_status_t st_w = impulse_writer_add_domain(writer, 0, IMPULSE_KEY_TYPE_INT32, "Default");
    assert(st_w == IMPULSE_OK);

    // Build mock relations:
    // DaG (Rel 0): node 10 -> [20, 21]
    std::vector<uint32_t> offsets1(1001, 0);
    offsets1[10] = 0;
    for (size_t i = 11; i <= 1000; ++i) offsets1[i] = 2;
    std::vector<uint32_t> targets1 = { 20, 21 };

    st_w = impulse_writer_add_relation(writer, 0, 0, IMPULSE_ENC_RAW, 1000, targets1.size(), 0,
                                       offsets1.data(), offsets1.size() * sizeof(uint32_t),
                                       targets1.data(), targets1.size() * sizeof(uint32_t));
    assert(st_w == IMPULSE_OK);

    // GpPW (Rel 1): node 20 -> [30], node 21 -> [31, 32]
    std::vector<uint32_t> offsets2(1001, 0);
    offsets2[20] = 0;
    offsets2[21] = 1;
    for (size_t i = 22; i <= 1000; ++i) offsets2[i] = 3;
    std::vector<uint32_t> targets2 = { 30, 31, 32 };

    st_w = impulse_writer_add_relation(writer, 0, 0, IMPULSE_ENC_RAW, 1000, targets2.size(), 0,
                                       offsets2.data(), offsets2.size() * sizeof(uint32_t),
                                       targets2.data(), targets2.size() * sizeof(uint32_t));
    assert(st_w == IMPULSE_OK);

    st_w = impulse_writer_finalize(writer);
    if (st_w != IMPULSE_OK) {
        std::cerr << "impulse_writer_finalize failed with code: " << st_w << " - " << impulse_get_last_error() << std::endl;
    }
    assert(st_w == IMPULSE_OK);
    impulse_writer_destroy(writer);

    impulse_status_t st_snap = IMPULSE_OK;
    impulse_snapshot_t* snap = impulse_snapshot_open(snap_path, &st_snap);
    assert(snap != nullptr);
    assert(st_snap == IMPULSE_OK);

    auto* ctx = impulse_vm_context_create(snap);
    assert(ctx != nullptr);

    // Compile 2-hop fused query: seed 10 -> should reach 30, 31, 32
    auto prog = ScmProgram::of(
        ScmWalk::forward("DaG"),
        ScmWalk::forward("GpPW"),
        ScmCollect::bitset()
    );
    GraphCatalog catalog;
    catalog.register_relation("DaG", 0, 1.0);
    catalog.register_relation("GpPW", 1, 1.0);

    auto compiled = ImpulseCompiler::compile(prog, &catalog);

    impulse_vm_state_t state{};
    state.query_context = ctx;

    impulse_vm_status_t st = impulse_vm_execute(compiled.data(), compiled.instruction_count(), &state, 10);
    assert(st == IMPULSE_VM_OK);

    uint16_t res_reg = compiled.result_register;
    assert(state.register_types[res_reg] == TYPE_BITSET_HANDLE);
    int handle = static_cast<int>(state.registers[res_reg]);
    assert(handle >= 0);

    bool has30 = impulse_vm_context_bitset_test(ctx, handle, 30);
    bool has31 = impulse_vm_context_bitset_test(ctx, handle, 31);
    bool has32 = impulse_vm_context_bitset_test(ctx, handle, 32);
    bool has99 = impulse_vm_context_bitset_test(ctx, handle, 99);

    std::cout << "  Node 30 reachable: " << (has30 ? "YES" : "NO") << std::endl;
    std::cout << "  Node 31 reachable: " << (has31 ? "YES" : "NO") << std::endl;
    std::cout << "  Node 32 reachable: " << (has32 ? "YES" : "NO") << std::endl;
    std::cout << "  Node 99 reachable: " << (has99 ? "YES" : "NO") << std::endl;

    assert(has30 && has31 && has32);
    assert(!has99);

    impulse_vm_context_destroy(ctx);
    impulse_snapshot_close(snap);
    std::remove(snap_path);

    std::cout << "  ✓ Live Impulse VM Execution: PASSED\n" << std::endl;
}

int main() {
    std::cout << "=========================================================================\n";
    std::cout << "       IMPULSE GRAPH C++ OPTIMIZING COMPILER PARITY TEST SUITE           \n";
    std::cout << "=========================================================================\n\n";

    test_ast_construction_and_serialization();
    test_parameter_binding_and_constant_folding();
    test_kernel_fusion_2hop();
    test_register_cache_ping_ponging();
    test_execution_on_vm();

    std::cout << "=========================================================================\n";
    std::cout << "                   ALL C++ COMPILER TESTS PASSED!                        \n";
    std::cout << "=========================================================================\n";
    return 0;
}
