#include "impulse_graph.h"
#include "impulse_index.h"
#include "impulse_vm.h"
#include "impulse_vm_internal.hpp"
#include "impulse_compiler.hpp"
#include "impulse_sexpr.hpp"
#include "impulse_cel.h"
#include "impulse_cypher.hpp"
#include "impulse_datalog.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <chrono>

using namespace impulse::compiler;
using namespace impulse::impscm;
using namespace impulse::cel;
using namespace impulse::datalog;

// ============================================================================
// Test Suite 1: Issue #53 (Storage, Index, Snapshot bounds and alignment)
// ============================================================================
void test_issue_53_storage_and_index() {
    printf("[Security Hardening] Testing Issue #53: Storage, Section Bounds & Index Hardening...\n");

    // 1. Footer length < 16 corrupted trailer: open must fail safely without leaking
    {
        uint8_t corrupt_file[128] = {0};
        corrupt_file[0] = 'I'; corrupt_file[1] = 'M'; corrupt_file[2] = 'P'; corrupt_file[3] = 'S';
        uint64_t bad_footer_len = 8ULL;
        std::memcpy(&corrupt_file[120], &bad_footer_len, 8);

        const char* path = "temp_corrupt_footer.imps";
        FILE* fp = fopen(path, "wb");
        assert(fp != nullptr);
        fwrite(corrupt_file, 1, sizeof(corrupt_file), fp);
        fclose(fp);

        impulse_status_t out_status = IMPULSE_OK;
        const impulse_snapshot_t* snap = impulse_snapshot_open(path, &out_status);
        assert(snap == nullptr);
        assert(out_status != IMPULSE_OK);
        (void)snap;
        (void)out_status;
        remove(path);
    }

    // 2. Index: MPHF lookup with invalid/corrupt index bytes (division by zero / bounds check)
    {
        uint8_t corrupt_mphf[64] = {0};
        uint32_t out_node_id = 999;
        impulse_status_t st = impulse_index_minimal_perfect_hash_lookup(
            corrupt_mphf, sizeof(corrupt_mphf), "test_key", &out_node_id);
        (void)st;
        (void)out_node_id;
    }

    // 3. Index: Permutation range query with small/invalid index_bytes
    {
        uint8_t corrupt_index[32] = {0};
        uint32_t out_nodes[4] = {0};
        size_t out_count = 0;
        float dummy_val = 1.0f;
        impulse_status_t st = impulse_index_permutation_range_query(
            corrupt_index, sizeof(corrupt_index), 0.0, 10.0, &dummy_val, out_nodes, 4, &out_count);
        assert(st != IMPULSE_OK);
        assert(out_count == 0);
        (void)st;
    }

    // 4. Index: Inverted bitset lookup with corrupt/small index bytes
    {
        uint8_t corrupt_inv[32] = {0};
        const uint64_t* out_words = nullptr;
        size_t out_num_words = 0;
        impulse_status_t st = impulse_index_inverted_bitset_lookup(
            corrupt_inv, sizeof(corrupt_inv), "search_term", &out_words, &out_num_words);
        assert(st != IMPULSE_OK || out_num_words == 0);
        (void)st;
    }

    // 5. Index: Trigram search with corrupt/truncated index bytes
    {
        uint8_t corrupt_tri[32] = {0};
        uint64_t out_words[2] = {0};
        size_t out_num_words = 0;
        impulse_status_t st = impulse_index_trigram_search(
            corrupt_tri, sizeof(corrupt_tri), "abc", out_words, 2, &out_num_words);
        assert(st != IMPULSE_OK || out_num_words == 0);
        (void)st;
    }

    printf("  [PASS] Issue #53: Storage, Section Bounds & Index Hardening.\n");
}

// ============================================================================
// Test Suite 2: Issue #54 (VM Static Verifier Parity & Opcode Hardening)
// ============================================================================
void test_issue_54_vm_verifier_and_opcodes() {
    printf("[Security Hardening] Testing Issue #54: VM Static Verifier Parity & Opcodes...\n");

    // 1. impulse_vm_validate: Reject reserved opcodes
    {
        uint8_t reserved_ops[] = {
            0x0D, 0x28, 0x29, 0x2B, 0x2C, 0x3E, 0x3F,
            0x4C, 0x4D, 0x4E, 0x4F, 0x59, 0x5D, 0x5E, 0x5F,
            0x6D, 0x6E, 0x6F, 0x76, 0x77, 0x7F
        };
        for (uint8_t op : reserved_ops) {
            impulse_instruction_t code[] = { { op, 0, 0, 0 }, { OP_HALT, 0, 0, 0 } };
            impulse_vm_status_t st = impulse_vm_validate(code, 2);
            assert(st == IMPULSE_VM_ERR_RESERVED_OPCODE);
            (void)st;
        }
    }

    // 2. impulse_vm_validate: Reject invalid opcodes
    {
        uint8_t invalid_ops[] = { 0x96, 0x97, 0x9F, 0xBF, 0xCC, 0xCD, 0xFE, 0xFF };
        for (uint8_t op : invalid_ops) {
            impulse_instruction_t code[] = { { op, 0, 0, 0 }, { OP_HALT, 0, 0, 0 } };
            impulse_vm_status_t st = impulse_vm_validate(code, 2);
            assert(st == IMPULSE_VM_ERR_INVALID_OPCODE);
            (void)st;
        }
    }

    // 3. impulse_vm_validate: Reject invalid register (>= 64)
    {
        impulse_instruction_t code_dst[] = { { OP_INIT_INPUT_NODE, 0, 64, 0 }, { OP_HALT, 0, 0, 0 } };
        assert(impulse_vm_validate(code_dst, 2) == IMPULSE_VM_ERR_INVALID_REGISTER);
        (void)code_dst;

        impulse_instruction_t code_src[] = { { OP_CSR_WALK, 0, 1, 99 }, { OP_HALT, 0, 0, 0 } };
        assert(impulse_vm_validate(code_src, 2) == IMPULSE_VM_ERR_INVALID_REGISTER);
        (void)code_src;

        impulse_instruction_t code_mov[] = { { OP_MOV, 0, 1, 75 }, { OP_HALT, 0, 0, 0 } };
        assert(impulse_vm_validate(code_mov, 2) == IMPULSE_VM_ERR_INVALID_REGISTER);
        (void)code_mov;
    }

    // 4. impulse_vm_validate: Reject out of bounds JMP / CALL targets
    {
        // Jump offset +50 in 2-instruction program
        impulse_instruction_t code_jmp[] = { { OP_JMP, 0, 0, 50 }, { OP_HALT, 0, 0, 0 } };
        assert(impulse_vm_validate(code_jmp, 2) == IMPULSE_VM_ERR_OUT_OF_BOUNDS);
        (void)code_jmp;

        // Jump offset -5 (before pc 0)
        impulse_instruction_t code_jmp_neg[] = { { OP_JMP, 0, 0, static_cast<uint32_t>(-5) }, { OP_HALT, 0, 0, 0 } };
        assert(impulse_vm_validate(code_jmp_neg, 2) == IMPULSE_VM_ERR_OUT_OF_BOUNDS);
        (void)code_jmp_neg;

        // Call target >= instruction_count
        impulse_instruction_t code_call[] = { { OP_CALL, 0, 0, 100 }, { OP_HALT, 0, 0, 0 } };
        assert(impulse_vm_validate(code_call, 2) == IMPULSE_VM_ERR_OUT_OF_BOUNDS);
        (void)code_call;
    }

    // 5. impulse_vm_validate: Stream shader sub-opcodes register validation
    {
        // sDst >= 16 rejected
        impulse_instruction_t code_stream_dst[] = { { OP_STREAM_LOAD_SRC, 0, 16, 0 }, { OP_HALT, 0, 0, 0 } };
        assert(impulse_vm_validate(code_stream_dst, 2) == IMPULSE_VM_ERR_INVALID_REGISTER);
        (void)code_stream_dst;

        // sPayloadLow >= 16 in stream math rejected
        uint32_t bad_payload = (0u << 16) | 16u; // sPayloadLow = 16
        impulse_instruction_t code_stream_math[] = { { OP_STREAM_MATH_ADD, 0, 1, bad_payload }, { OP_HALT, 0, 0, 0 } };
        assert(impulse_vm_validate(code_stream_math, 2) == IMPULSE_VM_ERR_INVALID_REGISTER);
        (void)code_stream_math;

        // sPayloadHigh >= 16 in stream math rejected
        uint32_t bad_payload2 = (16u << 16) | 0u; // sPayloadHigh = 16
        impulse_instruction_t code_stream_math2[] = { { OP_STREAM_MATH_ADD, 0, 1, bad_payload2 }, { OP_HALT, 0, 0, 0 } };
        assert(impulse_vm_validate(code_stream_math2, 2) == IMPULSE_VM_ERR_INVALID_REGISTER);
        (void)code_stream_math2;

        // globalReg >= 64 in OP_STREAM_REDUCE rejected
        uint32_t bad_global = (0u << 16) | 64u;
        impulse_instruction_t code_stream_red[] = { { OP_STREAM_REDUCE, 0, 1, bad_global }, { OP_HALT, 0, 0, 0 } };
        assert(impulse_vm_validate(code_stream_red, 2) == IMPULSE_VM_ERR_INVALID_REGISTER);
        (void)code_stream_red;
    }

    // 6. Fuel exhaustion in fixpoint internal loops
    {
        impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr);
        assert(ctx != nullptr);

        // Enable fuel and set to 1 unit
        impulse_vm_context_set_fuel(ctx, 1);
        assert(ctx->fuel_enabled);
        assert(ctx->fuel == 1);

        impulse_vm_state_t vm_state{};
        vm_state.query_context = ctx;

        // Run lane walk in fixpoint mode with insufficient fuel
        int h_src = impulse_vm_context_acquire_lane_frontier(ctx);
        assert(h_src >= 0);
        auto& lf = ctx->lane_frontiers[h_src];
        lf.width = IMPULSE_LANE_WIDTH_8;
        lf.active_mask = 0xFF;
        lf.lanes.assign(3, 0);
        lf.lanes[0] = 0x01; // node 0 active in lane 0
        lf.active_words.assign(1, 0);
        lf.active_words[0] = 1ULL; // word 0 has bit 0 set

        vm_state.registers[0] = static_cast<uint64_t>(h_src);
        vm_state.register_types[0] = TYPE_LANE_FRONTIER;

        // Mock CSR topology with cycle: 0->1, 1->2, 2->0
        uint32_t offsets[4] = { 0, 1, 2, 3 };
        uint32_t targets[3] = { 1, 2, 0 };
        impulse_vm_context_mock_csr(ctx, 0, offsets, targets, 3, 3);

        // Instruction: OP_LANE_CSR_WALK in fixpoint mode (flag 0x04)
        impulse_instruction_t prog[] = {
            { OP_LANE_CSR_WALK, 0x04, 1, 0 },
            { OP_HALT, 0, 0, 0 }
        };

        impulse_vm_status_t st = impulse_vm_execute(prog, 2, &vm_state, 0);
        assert(st == IMPULSE_VM_ERR_GAS_EXHAUSTED);
        (void)st;

        impulse_vm_context_destroy(ctx);
    }

    // 7. OP_INIT_MOCK_GRAPH bounds checking
    {
        impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr);
        impulse_vm_state_t vm_state{};
        vm_state.query_context = ctx;

        // Set inline data with non-monotonic offsets: offsets[0]=0, offsets[1]=5, offsets[2]=2
        uint32_t bad_offsets[] = { 3, 0, 5, 2 }; // node_count=3, offsets
        impulse_vm_context_bind_inline_data(ctx, reinterpret_cast<const uint8_t*>(bad_offsets), sizeof(bad_offsets));

        impulse_instruction_t prog[] = {
            { OP_INIT_MOCK_GRAPH, 0, 0, 0 },
            { OP_HALT, 0, 0, 0 }
        };
        impulse_vm_status_t st = impulse_vm_execute(prog, 2, &vm_state, 0);
        assert(st == IMPULSE_VM_ERR_OUT_OF_BOUNDS);
        (void)st;

        impulse_vm_context_destroy(ctx);
    }

    // 8. OP_STABLE_CHECK handle validation
    {
        impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr);
        impulse_vm_state_t vm_state{};
        vm_state.query_context = ctx;

        // Register with handle out of bounds (2000 >= VM_MAX_BITSET_HANDLES)
        vm_state.registers[1] = 2000;
        vm_state.register_types[1] = TYPE_BITSET_HANDLE;
        vm_state.registers[2] = 2000;
        vm_state.register_types[2] = TYPE_BITSET_HANDLE;

        impulse_instruction_t prog[] = {
            { OP_STABLE_CHECK, 0, 1, 2 },
            { OP_HALT, 0, 0, 0 }
        };
        impulse_vm_status_t st = impulse_vm_execute(prog, 2, &vm_state, 0);
        assert(st == IMPULSE_VM_ERR_INVALID_REGISTER);
        (void)st;

        impulse_vm_context_destroy(ctx);
    }

    printf("  [PASS] Issue #54: VM Static Verifier Parity & Opcodes.\n");
}

// ============================================================================
// Test Suite 3: Issue #55 (Compiler Parsers Recursion & Depth Hardening)
// ============================================================================
void test_issue_55_compiler_parsers() {
    printf("[Security Hardening] Testing Issue #55: Compiler Parsers Recursion & Bounds...\n");

    // 1. SExprParser: Nesting depth > 256 throws exception
    {
        std::string deep_sexpr;
        for (int i = 0; i < 300; ++i) deep_sexpr += "(";
        deep_sexpr += "leaf";
        for (int i = 0; i < 300; ++i) deep_sexpr += ")";

        SExprParser parser(deep_sexpr);
        bool caught = false;
        try {
            parser.parse();
        } catch (const std::runtime_error& e) {
            caught = true;
            (void)e;
        }
        assert(caught);
        (void)caught;
    }

    // 2. CEL Parser: Nesting depth > 256 throws exception
    {
        std::string deep_cel;
        for (int i = 0; i < 300; ++i) deep_cel += "!(";
        deep_cel += "x > 0";
        for (int i = 0; i < 300; ++i) deep_cel += ")";

        Parser parser(deep_cel);
        bool caught = false;
        try {
            parser.parse();
        } catch (const std::runtime_error& e) {
            caught = true;
            (void)e;
        }
        assert(caught);
        (void)caught;
    }

    // 3. CypherParser: Variable-length path unrolling max_hops <= 64
    {
        // max_hops = 100 > 64 should be rejected
        bool caught = false;
        try {
            CypherParser::parse("MATCH (a)-[:Knows*1..100]->(b) RETURN b");
        } catch (const std::runtime_error& e) {
            caught = true;
            (void)e;
        }
        assert(caught);
        (void)caught;

        // max_hops < min_hops rejected
        caught = false;
        try {
            CypherParser::parse("MATCH (a)-[:Knows*5..2]->(b) RETURN b");
        } catch (const std::runtime_error& e) {
            caught = true;
            (void)e;
        }
        assert(caught);
        (void)caught;

        // Valid max_hops = 64 accepted
        auto query = CypherParser::parse("MATCH (a)-[:Knows*1..64]->(b) RETURN b");
        assert(query.path.steps[0].edge.max_hops == 64);
    }

    // 4. Datalog StratificationValidator: Diamond DAG memoization eliminates 2^V blowup
    {
        // Build 30-stage diamond DAG:
        // A0 -> B0, A0 -> C0, B0 -> A1, C0 -> A1, ... up to A30
        // Without memoization, exploring from A0 visits A30 2^30 (~1 billion) times!
        std::vector<DependencyEdge> diamond_edges;
        for (int i = 0; i < 30; ++i) {
            std::string a = "A"; a += std::to_string(i);
            std::string b = "B"; b += std::to_string(i);
            std::string c = "C"; c += std::to_string(i);
            std::string next_a = "A"; next_a += std::to_string(i + 1);

            diamond_edges.push_back({a, b, false});
            diamond_edges.push_back({a, c, false});
            diamond_edges.push_back({b, next_a, false});
            diamond_edges.push_back({c, next_a, false});
        }

        std::string err;
        auto t0 = std::chrono::steady_clock::now();
        bool valid = StratificationValidator::validate(diamond_edges, err);
        auto t1 = std::chrono::steady_clock::now();
        auto duration_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();

        assert(valid);
        assert(duration_ms < 50);
        (void)valid;
        (void)duration_ms;
    }

    // 5. Datalog StratificationValidator: Negation cycle detected
    {
        std::vector<DependencyEdge> neg_cycle = {
            {"A", "B", false},
            {"B", "C", true}, // Negated edge in cycle
            {"C", "A", false}
        };
        std::string err;
        bool valid = StratificationValidator::validate(neg_cycle, err);
        assert(!valid);
        assert(err.find("Non-stratifiable negation cycle") != std::string::npos);
        (void)valid;
    }

    printf("  [PASS] Issue #55: Compiler Parsers Recursion & Bounds.\n");
}

int main() {
    printf("=========================================================================\n");
    printf("    IMPULSE GRAPH SECURITY HARDENING VERIFICATION SUITE (#53, #54, #55)  \n");
    printf("=========================================================================\n");

    test_issue_53_storage_and_index();
    test_issue_54_vm_verifier_and_opcodes();
    test_issue_55_compiler_parsers();

    printf("=========================================================================\n");
    printf("    ALL SECURITY HARDENING TESTS PASSED SUCCESSFULLY!                    \n");
    printf("=========================================================================\n");
    return 0;
}
