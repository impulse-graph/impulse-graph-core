#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "impulse_bytecode_emitter.hpp"
#include "impulse_compiler.hpp"
#include "impulse_cypher.hpp"
#include "impulse_graph.h"
#include "impulse_hexio.h"
#include "impulse_index.h"
#include "impulse_sexpr.hpp"
#include "impulse_vm.h"
#include "impulse_vm_fluent.hpp"
#include "impulse_vm_internal.hpp"
#if !defined(_WIN32)
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace {

int g_failures = 0;

#define CHECK(cond, msg)                                                       \
    do {                                                                       \
        if (!(cond)) {                                                         \
            std::fprintf(stderr, "[FAIL] %s:%d: %s (%s)\n", __FILE__,          \
                         __LINE__, #cond, (msg));                              \
            ++g_failures;                                                      \
            return;                                                            \
        }                                                                      \
    } while (0)

// 1.1: IMP_VM_SET_ZF / IMP_VM_SET_ST must not truncate upper bits of uint64_t flags
void test_flags_upper_bits_preserved() {
    impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr);
    CHECK(ctx != nullptr, "create empty ctx");

    // Program:
    //   R0 = 1 (node id)
    //   OP_SET_UNION R1, R0, R0 -> non-empty bitset, clears ZF via IMP_VM_SET_ZF(false)
    impulse_instruction_t prog[4] = {};
    prog[0].opcode = OP_LOAD_CONST_INT;
    prog[0].dst_reg = 0;
    prog[0].payload = 1;

    prog[1].opcode = OP_INIT_INPUT_NODE;
    prog[1].dst_reg = 0;
    prog[1].payload = 0;

    prog[2].opcode = OP_SET_UNION; // non-empty -> IMP_VM_SET_ZF(false)
    prog[2].dst_reg = 1;
    prog[2].payload = 0 | (0u << 16);

    prog[3].opcode = OP_HALT;

    impulse_vm_state_t state{};
    state.query_context = ctx;
    state.flags = 0xCAFEBABE12340001ULL; // upper bits set + ZF initially set

    impulse_vm_status_t st = impulse_vm_execute(prog, 4, &state, 1);
    CHECK(st == IMPULSE_VM_OK, "OP_SET_UNION executed");
    CHECK((state.flags & 0xFFFFFFFFFFFFFF00ULL) == 0xCAFEBABE12340000ULL,
          "IMP_VM_SET_ZF(false) must preserve upper bits 8..63 of vm_state->flags");

    impulse_vm_context_destroy(ctx);
}

// 1.2: OP_ISLAND_DETECT with invalid/wrong-type attribute slot must not read OOB
void test_island_detect_invalid_attr_type_and_size() {
    impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr);
    CHECK(ctx != nullptr, "create empty ctx");

    // 4-node path 0->1->2->3 (3 edges)
    uint32_t offsets[5] = {0, 1, 2, 3, 3};
    uint32_t targets[3] = {1, 2, 3};
    impulse_vm_context_mock_csr(ctx, 0, offsets, targets, 4, 3);

    // Bind an undersized/wrong-type attribute buffer (only 1 byte, TYPE_INT8) on relation 0
    uint8_t tiny_attr[1] = {42};
    if (ctx->attribute_slots.empty()) ctx->attribute_slots.resize(1);
    ctx->attribute_slots[0].push_back({tiny_attr, 1, nullptr, 0, 0x01 /* TYPE_INT8 */, 1});

    // Run OP_ISLAND_DETECT with src1=R0 (int64=0), src2=R0 (int64=0), rel=0
    impulse_instruction_t prog[3] = {};
    prog[0].opcode = OP_LOAD_CONST_INT;
    prog[0].dst_reg = 0;
    prog[0].payload = 0;

    prog[1].opcode = OP_ISLAND_DETECT;
    prog[1].dst_reg = 1;
    prog[1].payload = 0; // src1=0, src2=0, rel=0

    prog[2].opcode = OP_HALT;

    impulse_vm_state_t state{};
    state.query_context = ctx;
    impulse_vm_status_t st = impulse_vm_execute(prog, 3, &state, 0);
    // Should either reject invalid attribute cleanly or ignore non-int32/undersized attr without OOB read
    CHECK(st == IMPULSE_VM_OK || st == IMPULSE_VM_ERR_INVALID_REGISTER || st == IMPULSE_VM_ERR_OUT_OF_BOUNDS,
          "OP_ISLAND_DETECT handled invalid branch_ids safely");

    impulse_vm_context_destroy(ctx);
}

// 1.4: impulse_vm_context_bitset_fill bounds check
void test_bitset_fill_bounds() {
    impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr);
    CHECK(ctx != nullptr, "create empty ctx");
    // Requesting fill larger than allocated node_capacity must not write OOB
    impulse_vm_context_bitset_fill(ctx, 0, 100000ULL);
    impulse_vm_context_destroy(ctx);
}

// 2.1: MPHF lookup for first inserted key (previously str_off == 0)
void test_mphf_first_key_lookup() {
    const char* keys[3] = {"alpha", "beta", "gamma"};
    void* bytes = nullptr;
    size_t sz = 0;
    impulse_status_t st = impulse_index_build_minimal_perfect_hash(keys, 3, &bytes, &sz);
    CHECK(st == IMPULSE_OK && bytes != nullptr, "build mphf");

    uint32_t node_id = 999;
    st = impulse_index_minimal_perfect_hash_lookup(bytes, sz, "alpha", &node_id);
    std::free(bytes);
    CHECK(st == IMPULSE_OK, "lookup first key 'alpha' must succeed");
    CHECK(node_id == 0, "lookup 'alpha' must return node_id 0");
}

// 2.2: Delta layer 64-bit node IDs must not truncate upper 32 bits
void test_delta_layer_64bit_node_ids() {
    impulse_delta_layer_t* delta = impulse_delta_layer_create(0, 0, "rel");
    CHECK(delta != nullptr, "create delta");

    uint64_t high_src = (1ULL << 32) | 5ULL;
    uint64_t high_tgt = (2ULL << 32) | 7ULL;
    uint64_t low_src = 5ULL;
    uint64_t low_tgt = 7ULL;

    CHECK(impulse_delta_layer_tombstone_edge(delta, high_src, high_tgt) == IMPULSE_OK, "tombstone high edge");
    CHECK(impulse_delta_layer_is_tombstoned(delta, high_src, high_tgt) == true, "high edge tombstoned");
    CHECK(impulse_delta_layer_is_tombstoned(delta, low_src, low_tgt) == false,
          "low 32-bit colliding edge must NOT be tombstoned");

    impulse_delta_layer_destroy(delta);
}

// 2.4a: impulse_stmt_column_is_null OOB row_idx
void test_stmt_column_is_null_oob() {
    impulse_stmt_t* stmt = nullptr;
    impulse_status_t rc = impulse_stmt_prepare(nullptr, "RETURN $seed", &stmt);
    if (rc == IMPULSE_OK && stmt) {
        bool is_null = impulse_stmt_column_is_null(stmt, 0, 99999999ULL);
        CHECK(is_null == true, "OOB row_idx must safely return true");
        impulse_stmt_finalize(stmt);
    }
}

// 2.4b: impulse_execute_compiled_query null/short bytecode check
void test_execute_compiled_query_invalid_bytecode() {
    impulse_compiled_query_t bad_q{};
    bad_q.bytecode = nullptr;
    bad_q.bytecode_length = 0;
    bad_q.instruction_count = 5;

    impulse_vm_state_t state{};
    int rc = impulse_execute_compiled_query(nullptr, &bad_q, &state, 0);
    CHECK(rc != IMPULSE_VM_OK, "null bytecode must fail");

    uint8_t tiny[4] = {0, 0, 0, 0};
    bad_q.bytecode = tiny;
    bad_q.bytecode_length = 4;
    bad_q.instruction_count = 5;
    rc = impulse_execute_compiled_query(nullptr, &bad_q, &state, 0);
    CHECK(rc != IMPULSE_VM_OK, "short bytecode_length must fail");
}

// 2.6: impulse_hexio_read_buf integer overflow check
void test_hexio_overflow() {
    uint8_t buf[16] = {0};
    char out[64] = {0};
    int r = impulse_hexio_read_buf(buf, sizeof(buf),
                                   std::numeric_limits<size_t>::max() - 2, 10,
                                   out, sizeof(out));
    CHECK(r == -1, "offset + count overflow must return -1");
}

// 3.1: SExprParser mismatched delimiters must not hang / OOM
void test_sexpr_mismatched_delimiters() {
#if !defined(_WIN32)
    pid_t pid = fork();
    if (pid == 0) {
        alarm(1);
        bool threw = false;
        try {
            impulse::impscm::SExprParser p("[ )");
            (void)p.parse();
        } catch (const std::exception&) {
            threw = true;
        }
        _exit(threw ? 0 : 2);
    }
    int status = 0;
    waitpid(pid, &status, 0);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0,
          "SExprParser('[ )') must throw rather than infinite-looping");
#else
    bool threw = false;
    try {
        impulse::impscm::SExprParser p("[ )");
        (void)p.parse();
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(threw, "SExprParser('[ )') must throw rather than infinite-looping");
#endif
}

// 3.2 & 3.6: ImpOpsBytecodeEmitter LOOP_DECR/JNZ relative offsets & REVERSE_CSC in sub_steps
void test_bytecode_emitter_loops_and_reverse_csc() {
    using namespace impulse::compiler;
    using namespace impulse::compiler::emitter;
    // Build AST: (program (loop 3 (program (walk-in "knows"))))
    auto sub_walk = ScmWalk::reverse("knows");
    auto sub_prog = ScmProgram::of(sub_walk);

    auto loop_list = std::make_shared<ScmList>(std::vector<AstPtr>{
        std::make_shared<ScmSymbol>("repeat"),
        ScmLiteral::of_int(3),
        sub_prog
    });

    auto prog = ScmProgram::of(loop_list);

    impulse_vm_program_t out = ImpOpsBytecodeEmitter::emit(prog, nullptr);

    bool found_csc = false;
    bool found_loop_decr = false;
    for (size_t pc = 0; pc < out.instruction_list.size(); ++pc) {
        const auto& ins = out.instruction_list[pc];
        if (ins.opcode == OP_CSC_WALK) found_csc = true;
        if (ins.opcode == OP_LOOP_DECR) {
            found_loop_decr = true;
            int32_t rel_off = static_cast<int32_t>(ins.payload);
            CHECK(rel_off < 0, "OP_LOOP_DECR payload must be negative relative offset, not absolute PC");
        }
    }
    CHECK(found_csc, "REVERSE_CSC inside loop sub_steps must emit OP_CSC_WALK");
    CHECK(found_loop_decr, "OP_LOOP_DECR must be emitted");
}

// 3.4: QueryBuilder::repeatUntilStable must emit OP_STABLE_CHECK + OP_JNZ with negative offset
void test_fluent_repeat_until_stable() {
    impulse::vm::QueryBuilder qb;
    qb.repeatUntilStable([](impulse::vm::QueryBuilder& b) {
        b.nop();
    });
    auto compiled = qb.compile();
    const auto& insns = compiled.bytecode();

    bool found_stable = false;
    bool found_jnz = false;
    for (size_t i = 0; i < insns.size(); ++i) {
        if (insns[i].opcode == OP_STABLE_CHECK) {
            found_stable = true;
            CHECK(insns[i].payload == 0, "OP_STABLE_CHECK must have src_reg 0 / payload 0");
            if (i + 1 < insns.size() && insns[i + 1].opcode == OP_JNZ) {
                found_jnz = true;
                int32_t off = static_cast<int32_t>(insns[i + 1].payload);
                CHECK(off < 0, "OP_JNZ after OP_STABLE_CHECK must have negative relative offset");
            }
        }
    }
    CHECK(found_stable && found_jnz, "repeatUntilStable must emit OP_STABLE_CHECK followed by OP_JNZ");
}

// 3.5: ImpulseCompiler LANE_SET_EDGE_MASK must emit flags=1 (Set Edge Mask)
void test_compiler_lane_set_edge_mask() {
    using namespace impulse::compiler;
    auto node = std::make_shared<ScmLaneSetEdgeMask>(3, "rel", 1);
    node->dst_reg = 2;

    auto prog = ScmProgram::of(node);

    ImpulseCompiler compiler;
    auto compiled = compiler.compile(prog, nullptr);
    bool found = false;
    for (const auto& ins : compiled.instructions) {
        if (ins.opcode == OP_LANE_SET_EDGE_MASK) {
            found = true;
            CHECK(ins.flags == 1, "OP_LANE_SET_EDGE_MASK must set flags=1 (Variant 1: Set Edge Mask)");
        }
    }
    CHECK(found, "OP_LANE_SET_EDGE_MASK must be emitted");
}

// 3.7: Compiler shader walk must preserve walk flags (FLAG_HALT_ON_EMPTY / FLAG_INPUT_SEED)
void test_compiler_shader_walk_preserves_flags() {
    using namespace impulse::compiler;
    auto w = ScmWalk::forward("knows");
    auto filt = std::make_shared<ScmStreamFilter>(std::make_shared<ScmSymbol>("true"));
    w->shader_steps.push_back(filt);

    auto prog = ScmProgram::of(w);

    ImpulseCompiler compiler;
    auto compiled = compiler.compile(prog, nullptr);
    bool found_walk = false;
    for (const auto& ins : compiled.instructions) {
        if (ins.opcode == OP_CSR_WALK_STREAM) {
            found_walk = true;
            CHECK((ins.flags & IMPULSE_VM_OP_FLAG_INPUT_SEED) != 0,
                  "OP_CSR_WALK_STREAM must preserve IMPULSE_VM_OP_FLAG_INPUT_SEED");
        }
    }
    CHECK(found_walk, "OP_CSR_WALK_STREAM must be emitted");
}

} // namespace

int main() {
    test_flags_upper_bits_preserved();
    test_island_detect_invalid_attr_type_and_size();
    test_bitset_fill_bounds();
    test_mphf_first_key_lookup();
    test_delta_layer_64bit_node_ids();
    test_stmt_column_is_null_oob();
    test_execute_compiled_query_invalid_bytecode();
    test_hexio_overflow();
    test_sexpr_mismatched_delimiters();
    test_bytecode_emitter_loops_and_reverse_csc();
    test_fluent_repeat_until_stable();
    test_compiler_lane_set_edge_mask();
    test_compiler_shader_walk_preserves_flags();

    if (g_failures > 0) {
        std::fprintf(stderr, "\n[SUMMARY] %d issue #78 TDD checks FAILED\n", g_failures);
        return 1;
    }
    std::printf("[SUMMARY] All issue #78 TDD checks PASSED\n");
    return 0;
}
