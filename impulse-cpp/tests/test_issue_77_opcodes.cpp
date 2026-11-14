#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <vector>

#include "impulse_vm.h"
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

// 1. op_08 with OP_08_FLAG_NODE_VECTOR must allocate from node_vectors pool, not float_vectors
void test_op08_node_vector_pool_isolation() {
    impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr);
    CHECK(ctx != nullptr, "create ctx");

    uint32_t data[3] = {10, 20, 30};
    impulse_vm_context_bind_inline_data(ctx, data, sizeof(data));

    impulse_instruction_t prog[2] = {};
    prog[0].opcode = OP_LOAD_INLINE_ARRAY;
    prog[0].flags = 0x01u; // OP_08_FLAG_NODE_VECTOR
    prog[0].dst_reg = 0;
    prog[0].payload = (3u << 16) | 0u; // count=3, offset=0
    prog[1].opcode = OP_HALT;

    impulse_vm_state_t state{};
    state.query_context = ctx;
    impulse_vm_status_t st = impulse_vm_execute(prog, 2, &state, 0);
    CHECK(st == IMPULSE_VM_OK, "OP_LOAD_INLINE_ARRAY node vec ok");
    CHECK(state.register_types[0] == TYPE_NODE_VECTOR, "dst is TYPE_NODE_VECTOR");
    int h = static_cast<int>(state.registers[0]);
    CHECK(ctx->node_vectors_allocated[h] == true, "node_vectors_allocated must be true");

    impulse_vm_context_destroy(ctx);
}

// 2. op_50_jmp with offset == 0 must terminate with GAS_EXHAUSTED (not hang), negative underflows to OOB
void test_jmp_zero_and_underflow_guard() {
    impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr);
    ctx->fuel_enabled = true;
    ctx->fuel = 100;
    impulse_instruction_t prog[2] = {};
    prog[0].opcode = OP_JMP;
    prog[0].payload = 0;
    prog[1].opcode = OP_HALT;

    impulse_vm_state_t state{};
    state.query_context = ctx;
    impulse_vm_status_t st = impulse_vm_execute(prog, 2, &state, 0);
    CHECK(st == IMPULSE_VM_ERR_GAS_EXHAUSTED, "OP_JMP offset=0 must hit GAS_EXHAUSTED");

    prog[0].payload = static_cast<uint32_t>(static_cast<int32_t>(-5));
    ctx->fuel = 100;
    state = {};
    state.query_context = ctx;
    st = impulse_vm_execute(prog, 2, &state, 0);
    CHECK(st == IMPULSE_VM_ERR_OUT_OF_BOUNDS, "OP_JMP negative underflow must return OOB");

    impulse_vm_context_destroy(ctx);
}

// 3. op_75_set_max_dop must not overflow signed int when req_dop > INT_MAX
void test_set_max_dop_clamp_and_overflow() {
    impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr);
    CHECK(ctx != nullptr, "create ctx");

    impulse_instruction_t prog[2] = {};
    prog[0].opcode = OP_SET_MAX_DOP;
    prog[0].dst_reg = 0;
    prog[0].payload = 0xFFFFFFFFu; // > INT_MAX -> previously cast to -1!
    prog[1].opcode = OP_HALT;

    impulse_vm_state_t state{};
    state.query_context = ctx;
    impulse_vm_status_t st = impulse_vm_execute(prog, 2, &state, 0);
    CHECK(st == IMPULSE_VM_OK, "SET_MAX_DOP ok");
    CHECK(static_cast<int64_t>(state.registers[0]) >= 1,
          "SET_MAX_DOP must not wrap negative on > INT_MAX");

    impulse_vm_context_destroy(ctx);
}

// 5. op_8b_coo_walk_filtered must check v < max_nodes before indexing flt_words
void test_coo_walk_filtered_oob_target_node() {
    impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr);
    CHECK(ctx != nullptr, "create ctx");

    // Relation 0: node 0 -> huge OOB target node 99999999
    uint32_t offsets[2] = {0, 1};
    uint32_t targets[1] = {99999999u};
    impulse_vm_context_mock_csr(ctx, 0, offsets, targets, 1, 1);

    impulse_instruction_t prog[4] = {};
    // R0 = node 0 (TYPE_NODE_ID)
    prog[0].opcode = OP_INIT_INPUT_NODE;
    prog[0].dst_reg = 0;
    // R1 = bitset {0} via OP_SET_UNION
    prog[1].opcode = OP_SET_UNION;
    prog[1].dst_reg = 1;
    prog[1].payload = 0 | (0u << 16);
    // R2 = OP_COO_WALK_FILTERED(src=R1, filter=R1, rel=0)
    prog[2].opcode = OP_COO_WALK_FILTERED;
    prog[2].dst_reg = 2;
    prog[2].payload = 1 | (1u << 8) | (0u << 16);
    prog[3].opcode = OP_HALT;

    impulse_vm_state_t state{};
    state.query_context = ctx;
    impulse_vm_status_t st = impulse_vm_execute(prog, 4, &state, 0);
    CHECK(st == IMPULSE_VM_OK, "OP_COO_WALK_FILTERED safely ignored OOB target node");

    impulse_vm_context_destroy(ctx);
}

// 6. op_a7_stream_filter must reject NaN (null) values
void test_stream_filter_rejects_nan() {
    impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr);
    CHECK(ctx != nullptr, "create ctx");

    uint32_t csr_off[3] = {0, 1, 1};
    uint32_t csr_tgt[1] = {1};
    impulse_vm_context_mock_csr(ctx, 0, csr_off, csr_tgt, 2, 1);

    float nan_val = std::numeric_limits<float>::quiet_NaN();
    uint32_t nan_bits = 0;
    std::memcpy(&nan_bits, &nan_val, sizeof(nan_bits));

    // Program:
    //  0: LOAD_CONST_INT R0, 0 (seed node 0)
    //  1: CSR_WALK_STREAM R1, R0, rel=0
    //  2: STREAM_FUNC_BEGIN
    //  3: STREAM_LOAD_CONST s0, NaN
    //  4: STREAM_FILTER s0
    //  5: STREAM_YIELD
    //  6: STREAM_FUNC_END
    //  7: SET_CARDINALITY R2, R1
    //  8: HALT
    impulse_instruction_t prog[9] = {};
    prog[0].opcode = OP_LOAD_CONST_INT;
    prog[0].dst_reg = 0;
    prog[0].payload = 0;

    prog[1].opcode = OP_CSR_WALK_STREAM;
    prog[1].flags = IMPULSE_VM_OP_FLAG_INPUT_SEED;
    prog[1].dst_reg = 1;
    prog[1].payload = 0 | (0u << 16);

    prog[2].opcode = OP_STREAM_FUNC_BEGIN;

    prog[3].opcode = OP_STREAM_LOAD_CONST;
    prog[3].dst_reg = 0;
    prog[3].payload = nan_bits;

    prog[4].opcode = OP_STREAM_FILTER;
    prog[4].dst_reg = 0;

    prog[5].opcode = OP_STREAM_YIELD;
    prog[6].opcode = OP_STREAM_FUNC_END;

    prog[7].opcode = OP_SET_CARDINALITY;
    prog[7].dst_reg = 2;
    prog[7].payload = 1;

    prog[8].opcode = OP_HALT;

    impulse_vm_state_t state{};
    state.query_context = ctx;
    impulse_vm_status_t st = impulse_vm_execute(prog, 9, &state, 0);
    CHECK(st == IMPULSE_VM_OK, "CSR_WALK_STREAM executed");
    CHECK(state.registers[2] == 0, "OP_STREAM_FILTER must reject NaN (null) so yield is aborted");

    impulse_vm_context_destroy(ctx);
}

// 7. op_14_node_filter must read int16_t (not int8_t) for IMP_TYPE_CODE_INT16 attributes
void test_op14_node_filter_int16() {
    impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr);
    CHECK(ctx != nullptr, "create ctx");

    int16_t attr_vals[3] = {10, 300, -5};
    ctx->domain_node_counts.resize(1, 3);
    ctx->domain_attribute_slots.resize(1);
    ctx->domain_attribute_slots[0].resize(1);
    ctx->domain_attribute_slots[0][0].type_code = IMP_TYPE_CODE_INT16;
    ctx->domain_attribute_slots[0][0].data_ptr = attr_vals;
    ctx->domain_attribute_slots[0][0].data_bytes = sizeof(attr_vals);

    // Filter node 1 (attr == 300): fails if op_14 reads int8_t (44 != 300)
    impulse_instruction_t prog[4] = {};
    prog[0].opcode = OP_LOAD_CONST_INT;
    prog[0].dst_reg = 0;
    prog[0].payload = 1; // node 1

    prog[1].opcode = OP_LOAD_CONST_INT;
    prog[1].dst_reg = 1;
    prog[1].payload = 300; // expected value 300

    prog[2].opcode = OP_NODE_FILTER;
    prog[2].dst_reg = 2;
    // src=R0, val_reg=R1, attr_id=0, rel_id=0
    prog[2].payload = 0u | (1u << 8) | (0u << 16) | (0u << 24);

    prog[3].opcode = OP_HALT;

    impulse_vm_state_t state{};
    state.query_context = ctx;
    impulse_vm_status_t st = impulse_vm_execute(prog, 4, &state, 0);
    CHECK(st == IMPULSE_VM_OK, "OP_NODE_FILTER int16 ok");
    CHECK((state.flags & IMPULSE_VM_FLAG_ZF) == 0, "OP_NODE_FILTER int16 must match 300 (ZF=0)");

    impulse_vm_context_destroy(ctx);
}

// 8. op_1d_adaptive_walk extended pull path must advance PC by 2 (skipping extension word)
void test_op1d_adaptive_walk_extended_pull_pc() {
    impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr);
    CHECK(ctx != nullptr, "create ctx");

    // Dense 4-node graph with CSR + CSC so adaptive walk triggers pull mode
    uint32_t csr_off[5] = {0, 1, 2, 3, 4};
    uint32_t csr_tgt[4] = {1, 2, 3, 0};
    uint32_t csc_off[5] = {0, 1, 2, 3, 4};
    uint32_t csc_tgt[4] = {3, 0, 1, 2};
    impulse_vm_context_mock_csr(ctx, 0, csr_off, csr_tgt, 4, 4);
    ctx->slots[0].csc_offsets_ptr = csc_off;
    ctx->slots[0].csc_targets_ptr = csc_tgt;
    ctx->slots[0].csc_node_count = 4;

    // Seed frontier with {0, 1, 2, 3} (> 1/20 of nodes -> pull mode), then extended OP_ADAPTIVE_WALK
    // Word 0: LOAD_CONST_INT R0, 0
    // Word 1: SET_UNION R1, R0, R0  -> bitset {0}
    // Word 2: LOAD_CONST_INT R2, 1
    // Word 3: SET_UNION R1, R1, R2  -> bitset {0,1} (2/4 = 50% > 5%, triggers pull!)
    // Word 4: OP_ADAPTIVE_WALK (EXTENDED) dst=R3, src=R1
    // Word 5: Extension word (dst=0 for rel=0), opcode = OP_TRAP (0x04) so if PC+=1 it traps!
    // Word 6: OP_HALT
    impulse_instruction_t prog[7] = {};
    prog[0].opcode = OP_LOAD_CONST_INT; prog[0].dst_reg = 0; prog[0].payload = 0;
    prog[1].opcode = OP_INIT_INPUT_NODE; prog[1].dst_reg = 0;
    prog[2].opcode = OP_LOAD_CONST_INT; prog[2].dst_reg = 2; prog[2].payload = 1;
    prog[3].opcode = OP_CSR_WALK; prog[3].flags = IMPULSE_VM_OP_FLAG_INPUT_SEED | IMPULSE_VM_OP_FLAG_ACCUMULATE;
    prog[3].dst_reg = 1; prog[3].payload = 0 | (0u << 16);

    prog[4].opcode = OP_ADAPTIVE_WALK;
    prog[4].flags = IMP_FLAG_EXTENDED_WORD;
    prog[4].dst_reg = 3;
    prog[4].payload = 1; // src = R1

    prog[5].opcode = OP_TRAP; // Should be skipped as extension word!
    prog[5].dst_reg = 0;      // rel = 0

    prog[6].opcode = OP_HALT;

    impulse_vm_state_t state{};
    state.query_context = ctx;
    impulse_vm_status_t st = impulse_vm_execute(prog, 7, &state, 0);
    CHECK(st == IMPULSE_VM_OK, "OP_ADAPTIVE_WALK extended pull must advance PC by 2 (not execute word 5)");

    impulse_vm_context_destroy(ctx);
}

// 9. op_39 / op_3c must safely return OOB on forged out-of-range vector handles
void test_op39_op3c_forged_handle_bounds() {
    impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr);
    CHECK(ctx != nullptr, "create ctx");

    impulse_instruction_t prog[2] = {};
    prog[0].opcode = OP_EXTRACT_VALIDITY;
    prog[0].dst_reg = 1;
    prog[0].payload = 0; // src = R0
    prog[1].opcode = OP_HALT;

    impulse_vm_state_t state{};
    state.query_context = ctx;
    state.registers[0] = 999999u; // forged out-of-range handle
    state.register_types[0] = TYPE_FLOAT_VECTOR;
    impulse_vm_status_t st = impulse_vm_execute(prog, 2, &state, 0);
    CHECK(st == IMPULSE_VM_ERR_OUT_OF_BOUNDS, "OP_EXTRACT_VALIDITY forged handle must return OOB");

    prog[0].opcode = OP_L1_NORM_DIFF;
    prog[0].dst_reg = 2;
    prog[0].payload = 0 | (0u << 16);
    state = {};
    state.query_context = ctx;
    state.registers[0] = 999999u;
    state.register_types[0] = TYPE_FLOAT_VECTOR;
    st = impulse_vm_execute(prog, 2, &state, 0);
    CHECK(st == IMPULSE_VM_ERR_OUT_OF_BOUNDS, "OP_L1_NORM_DIFF forged handle must return OOB");

    impulse_vm_context_destroy(ctx);
}

// 10. op_41_mxv must safely return OOB on forged out-of-range src_vec handle
void test_op41_mxv_forged_handle_bounds() {
    impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr);
    CHECK(ctx != nullptr, "create ctx");

    uint32_t csr_off[3] = {0, 1, 1};
    uint32_t csr_tgt[1] = {1};
    impulse_vm_context_mock_csr(ctx, 0, csr_off, csr_tgt, 2, 1);

    impulse_instruction_t prog[2] = {};
    prog[0].opcode = OP_MXV;
    prog[0].dst_reg = 1;
    prog[0].payload = 0 | (0u << 8) | (SEMIRING_PLUS_TIMES << 16);
    prog[1].opcode = OP_HALT;

    impulse_vm_state_t state{};
    state.query_context = ctx;
    state.registers[0] = 999999u; // forged handle
    state.register_types[0] = TYPE_FLOAT_VECTOR;
    impulse_vm_status_t st = impulse_vm_execute(prog, 2, &state, 0);
    CHECK(st == IMPULSE_VM_ERR_OUT_OF_BOUNDS, "OP_MXV forged handle must return OOB");

    impulse_vm_context_destroy(ctx);
}

// 11. op_a4_stream_load_edge must use elem_sz=8 for IMP_TYPE_CODE_INT64 so OOB eIdx returns 0.0f
void test_opa4_stream_load_edge_int64_elem_size() {
    impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr);
    CHECK(ctx != nullptr, "create ctx");

    // Relation with 3 edges, but attribute buffer only has 1 int64_t (8 bytes).
    // With buggy elem_sz=4, cnt=2, so eIdx=1 reads 8 bytes past the 8-byte buffer!
    uint32_t csr_off[3] = {0, 2, 2};
    uint32_t csr_tgt[2] = {1, 1};
    impulse_vm_context_mock_csr(ctx, 0, csr_off, csr_tgt, 2, 2);

    std::vector<int64_t> one_i64 = {42};
    ctx->attribute_slots.resize(1);
    ctx->attribute_slots[0].resize(2);
    ctx->attribute_slots[0][1].type_code = IMP_TYPE_CODE_INT64;
    ctx->attribute_slots[0][1].data_ptr = one_i64.data();
    ctx->attribute_slots[0][1].data_bytes = sizeof(int64_t); // only 1 element!

    // For eIdx=1 (second edge), cnt must be 8/8 = 1, so eIdx=1 >= cnt -> s0 = 0.0f (not OOB memory!)
    // We accumulate into R1 via STREAM_REDUCE SUM; total must be 42.0f + 0.0f = 42.0f.
    impulse_instruction_t prog[8] = {};
    prog[0].opcode = OP_LOAD_CONST_INT; prog[0].dst_reg = 0; prog[0].payload = 0;
    prog[1].opcode = OP_CSR_WALK_STREAM; prog[1].flags = IMPULSE_VM_OP_FLAG_INPUT_SEED;
    prog[1].dst_reg = 1; prog[1].payload = 0 | (0u << 16);
    prog[2].opcode = OP_STREAM_FUNC_BEGIN;
    prog[3].opcode = OP_STREAM_LOAD_EDGE; prog[3].dst_reg = 0; prog[3].payload = 1;
    prog[4].opcode = OP_STREAM_REDUCE; prog[4].flags = 0u; prog[4].dst_reg = 0; prog[4].payload = 2;
    prog[5].opcode = OP_STREAM_YIELD;
    prog[6].opcode = OP_STREAM_FUNC_END;
    prog[7].opcode = OP_HALT;

    impulse_vm_state_t state{};
    state.query_context = ctx;
    impulse_vm_status_t st = impulse_vm_execute(prog, 8, &state, 0);
    CHECK(st == IMPULSE_VM_OK, "CSR_WALK_STREAM int64 edge attr ok");
    int h2 = static_cast<int>(state.registers[2]);
    CHECK(ctx->float_vectors[h2][1] == 42.0f, "OOB int64 edge attr must not read past buffer");

    impulse_vm_context_destroy(ctx);
}

} // namespace

int main() {
    test_op08_node_vector_pool_isolation();
    test_jmp_zero_and_underflow_guard();
    test_set_max_dop_clamp_and_overflow();
    test_coo_walk_filtered_oob_target_node();
    test_stream_filter_rejects_nan();
    test_op14_node_filter_int16();
    test_op1d_adaptive_walk_extended_pull_pc();
    test_op39_op3c_forged_handle_bounds();
    test_op41_mxv_forged_handle_bounds();
    test_opa4_stream_load_edge_int64_elem_size();

    if (g_failures > 0) {
        std::fprintf(stderr, "\n[SUMMARY] %d issue #77 opcode TDD checks FAILED\n", g_failures);
        return 1;
    }
    std::printf("[SUMMARY] All issue #77 opcode TDD checks PASSED\n");
    return 0;
}
