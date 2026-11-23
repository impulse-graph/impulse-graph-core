/**
 * @file test_v0_10_1_remediation.cpp
 * @brief TDD & MC/DC regression tests for impulse-graph-core v0.10.1 Wave 1A (#136, #144, #145).
 */

#include "impulse_graph.h"
#include "impulse_index.h"
#include "impulse_vm.h"

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <new>
#include <string>
#include <vector>

#ifndef _WIN32
#include <unistd.h>
#endif

#define TEST_ASSERT(cond)                                                               \
    do {                                                                                \
        if (!(cond)) {                                                                  \
            std::fprintf(stderr, "FAIL: %s (%s:%d)\n", #cond, __FILE__, __LINE__);      \
            std::abort();                                                               \
        }                                                                               \
    } while (0)

// ============================================================================
// Thread-local heap allocation counter (active in non-ASan builds)
// ============================================================================
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define IMPULSE_HAS_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer)
#define IMPULSE_HAS_ASAN 1
#else
#define IMPULSE_HAS_ASAN 0
#endif
#else
#define IMPULSE_HAS_ASAN 0
#endif

static thread_local bool g_count_allocs = false;
static thread_local size_t g_alloc_count = 0;

#if !IMPULSE_HAS_ASAN
void* operator new(std::size_t sz) {
    if (g_count_allocs) {
        ++g_alloc_count;
    }
    if (sz == 0) sz = 1;
    void* p = std::malloc(sz);
    if (!p) throw std::bad_alloc();
    return p;
}

void* operator new[](std::size_t sz) {
    return ::operator new(sz);
}

void operator delete(void* p) noexcept {
    std::free(p);
}

void operator delete[](void* p) noexcept {
    std::free(p);
}

void operator delete(void* p, std::size_t) noexcept {
    std::free(p);
}

void operator delete[](void* p, std::size_t) noexcept {
    std::free(p);
}
#endif

namespace {

// Helper: build a small valid v0.10.0 snapshot in memory/temp file with a string key domain and a float16 embedding attr
std::string build_test_snapshot_file(const char* path) {
    impulse_writer_t* w = impulse_writer_create(path, 0);
    TEST_ASSERT(w != nullptr);

    TEST_ASSERT(impulse_writer_add_domain(w, 0, 0x0B /* VAR_STRING */, "User") == IMPULSE_OK);
    TEST_ASSERT(impulse_writer_set_domain_node_count(w, 0, 4) == IMPULSE_OK);

    // Key attribute (attr 0): VAR_STRING "alice", "bob", "carol", "dave"
    const char str_data[] = "alicebobcaroldave";
    const uint32_t str_offsets[5] = {0, 5, 8, 13, 17};
    TEST_ASSERT(impulse_writer_add_domain_attribute(
        w, 0, "username", 0x0B, 1,
        str_data, sizeof(str_data) - 1,
        str_offsets, sizeof(str_offsets)) == IMPULSE_OK);
    TEST_ASSERT(impulse_writer_set_domain_key_attr(w, 0, 0) == IMPULSE_OK);

    // Embedding attribute (attr 1): FLOAT16 (0x05), dim=4, 4 nodes -> 16 uint16_t values
    // IEEE-754 FP16 for 1.0f is 0x3C00, 0.5f is 0x3800, 2.0f is 0x4000
    const uint16_t fp16_embs[16] = {
        0x3C00, 0x3C00, 0x0000, 0x0000, // node 0: [1, 1, 0, 0]
        0x3800, 0x3800, 0x3800, 0x3800, // node 1: [0.5, 0.5, 0.5, 0.5]
        0x4000, 0x0000, 0x0000, 0x0000, // node 2: [2, 0, 0, 0]
        0x3C00, 0x0000, 0x3C00, 0x0000  // node 3: [1, 0, 1, 0]
    };
    TEST_ASSERT(impulse_writer_add_domain_attribute(
        w, 0, "emb", 0x05, 4,
        fp16_embs, sizeof(fp16_embs),
        nullptr, 0) == IMPULSE_OK);

    // Relation 0: Follows (0->1, 1->2, 2->3, 3->0)
    const uint32_t csr_off[5] = {0, 1, 2, 3, 4};
    const uint32_t csr_tgt[4] = {1, 2, 3, 0};
    TEST_ASSERT(impulse_writer_add_relation(
        w, 0, 0, IMPULSE_ENC_RAW, 4, 4, 0,
        csr_off, sizeof(csr_off),
        csr_tgt, sizeof(csr_tgt)) == IMPULSE_OK);

    TEST_ASSERT(impulse_writer_finalize(w) == IMPULSE_OK);
    impulse_writer_destroy(w);
    return std::string(path);
}

// ============================================================================
// #144 (C++): impulse_compile_to_impas must NOT write to stderr; must populate g_last_error
// ============================================================================
void test_issue_144_no_stderr_in_compile_to_impas() {
    std::printf("[TEST] #144 impulse_compile_to_impas zero stderr writes...\n");
#ifndef _WIN32
    int pipefd[2];
    TEST_ASSERT(::pipe(pipefd) == 0);
    std::fflush(stderr);
    int saved_stderr = ::dup(STDERR_FILENO);
    TEST_ASSERT(saved_stderr >= 0);
    TEST_ASSERT(::dup2(pipefd[1], STDERR_FILENO) >= 0);
    ::close(pipefd[1]);

    char out_buf[1024];
    size_t out_written = 0;
    // Trigger a parse/compile exception via empty/invalid Cypher script
    impulse_status_t rc = impulse_compile_to_impas(
        nullptr, "MATCH ((( INVALID SYNTAX !!!", IMPULSE_LANG_CYPHER,
        out_buf, sizeof(out_buf), &out_written);

    std::fflush(stderr);
    ::dup2(saved_stderr, STDERR_FILENO);
    ::close(saved_stderr);

    char captured[512];
    ssize_t nread = ::read(pipefd[0], captured, sizeof(captured) - 1);
    ::close(pipefd[0]);
    if (nread < 0) nread = 0;
    captured[nread] = '\0';

    TEST_ASSERT(rc != IMPULSE_OK);
    if (nread != 0) {
        std::fprintf(stderr, "Expected 0 bytes on stderr, got %zd bytes: %s\n", nread, captured);
    }
    TEST_ASSERT(nread == 0);
    const char* err = impulse_get_last_error();
    TEST_ASSERT(err != nullptr && err[0] != '\0');
#endif
    std::printf("  [PASS] #144 zero stderr writes & g_last_error populated\n");
}

// ============================================================================
// #145: Repeated impulse_stmt_execute() must perform 0 heap allocations
// ============================================================================
void test_issue_145_stmt_execute_zero_alloc(const impulse_snapshot_t* snap) {
    std::printf("[TEST] #145 impulse_stmt_execute allocation-free hot path...\n");

    const char* cypher = "MATCH (u:User)-[:Follows]->(v:User) WHERE u.id = $seed RETURN v";
    impulse_stmt_t* stmt = nullptr;
    TEST_ASSERT(impulse_stmt_prepare(snap, cypher, &stmt) == IMPULSE_OK);
    TEST_ASSERT(stmt != nullptr);

    size_t buf_size = impulse_stmt_buffer_size(stmt);
    TEST_ASSERT(buf_size > 0);
    std::vector<uint8_t> caller_buf(buf_size);

    TEST_ASSERT(impulse_stmt_bind_node(stmt, "$seed", 0) == IMPULSE_OK);
    // Warmup run (allows any one-time lazy reservation)
    TEST_ASSERT(impulse_stmt_execute(stmt, caller_buf.data(), caller_buf.size()) == IMPULSE_OK);
    TEST_ASSERT(impulse_stmt_row_count(stmt) > 0);

    // Hot-path loop: 50 executions must do 0 heap allocations
    g_alloc_count = 0;
    g_count_allocs = true;
    for (int i = 0; i < 50; ++i) {
        impulse_status_t rc = impulse_stmt_execute(stmt, caller_buf.data(), caller_buf.size());
        TEST_ASSERT(rc == IMPULSE_OK);
    }
    g_count_allocs = false;

#if !IMPULSE_HAS_ASAN
    if (g_alloc_count != 0) {
        std::fprintf(stderr, "FAIL: impulse_stmt_execute performed %zu heap allocations in 50 hot-path runs\n", g_alloc_count);
    }
    TEST_ASSERT(g_alloc_count == 0);
#endif

    impulse_stmt_finalize(stmt);
    std::printf("  [PASS] #145 impulse_stmt_execute 0 heap allocations\n");
}

// ============================================================================
// #145: Reused context OP_VECTOR_RERANK_DOT (FP16), OP_MAP_DENSE_TO_KEYS (0x92),
//       and OP_COLLECT_VALUE_MAP (0x93) must perform 0 heap allocations on repeat runs
// ============================================================================
void test_issue_145_opcodes_zero_alloc(const impulse_snapshot_t* snap) {
    std::printf("[TEST] #145 OP_CB (FP16), OP_92, OP_93 allocation-free repeat execution...\n");

    impulse_vm_context_t* ctx = impulse_vm_context_create(snap);
    TEST_ASSERT(ctx != nullptr);

    // Build bytecode:
    // 0: OP_LOAD_IMM R0 = 0 (node 0)
    // 1: OP_CSR_WALK R1 <- R0 via rel 0 (node 1)
    // 2: OP_MAP_DENSE_TO_KEYS (0x92) R2 <- R1, domain 0
    // 3: OP_VECTOR_RERANK_DOT (0xCB) R3 <- R1, attr 1 (FP16 emb), metric=0 (dot)
    // 4: OP_COLLECT_VALUE_MAP (0x93) R4 <- nodes=R1, vals=R3, domain=0
    // 5: OP_HALT R2
    impulse_instruction_t prog[6]{};
    prog[0] = {OP_INIT_INPUT_NODE, 0, 0, 0};
    prog[1] = {OP_CSR_WALK, 0, 1, (0u << 16) | 0u};
    prog[2] = {OP_MAP_DENSE_TO_KEYS, 0, 2, (0u << 8) | 1u};
    // 3: OP_VECTOR_RERANK_DOT (0xCB) R3 <- cand=R1, q=R5 (float vec), attr_id=1 (FP16 emb), flags=0 (dot)
    prog[3] = {OP_VECTOR_RERANK_DOT, 0, 3, (1u << 16) | (5u << 8) | 1u};
    // 4: OP_COLLECT_VALUE_MAP (0x93) R4 <- nodes=R1, vals=R3, domain=0
    prog[4] = {OP_COLLECT_VALUE_MAP, 0, 4, (0u << 16) | (3u << 8) | 1u};
    prog[5] = {OP_HALT, 0, 2, 0};

    auto run_once = [&]() {
        impulse_vm_context_reset(ctx);
        int h_q = impulse_vm_context_acquire_float_vector(ctx);
        TEST_ASSERT(h_q >= 0);
        for (size_t k = 0; k < 4; ++k) {
            impulse_vm_context_float_vector_set(ctx, static_cast<size_t>(h_q), k, 1.0f);
        }
        impulse_vm_state_t st{};
        std::memset(&st, 0, sizeof(st));
        st.query_context = ctx;
        st.registers[5] = static_cast<uint64_t>(h_q);
        st.register_types[5] = TYPE_FLOAT_VECTOR;
        impulse_vm_status_t rc = impulse_vm_execute(prog, 6, &st, 0);
        TEST_ASSERT(rc == IMPULSE_VM_OK);
        // Verify OP_92 produced "bob" (node 1)
        TEST_ASSERT(st.register_types[2] == TYPE_STRING_VECTOR);
        size_t s_handle = static_cast<size_t>(st.registers[2]);
        TEST_ASSERT(impulse_vm_context_string_vector_size(ctx, s_handle) == 1);
        const char* k0 = impulse_vm_context_string_vector_get(ctx, s_handle, 0);
        TEST_ASSERT(k0 != nullptr && std::strcmp(k0, "bob") == 0);
        // Verify OP_CB produced dot([0.5,0.5,0.5,0.5], [1,1,1,1]) == 2.0f
        TEST_ASSERT(st.register_types[3] == TYPE_FLOAT_VECTOR);
        size_t f_handle = static_cast<size_t>(st.registers[3]);
        const float* scores = impulse_vm_context_get_float_vector(ctx, f_handle);
        TEST_ASSERT(scores != nullptr && std::fabs(scores[0] - 2.0f) < 1e-3f);
    };

    // Warmup run (populates context-owned scratch capacity once)
    run_once();

    // Hot-path repeat runs: must allocate 0 times
    g_alloc_count = 0;
    g_count_allocs = true;
    for (int i = 0; i < 20; ++i) {
        run_once();
    }
    g_count_allocs = false;

#if !IMPULSE_HAS_ASAN
    if (g_alloc_count != 0) {
        std::fprintf(stderr, "FAIL: OP_CB/OP_92/OP_93 performed %zu heap allocations in 20 hot-path runs\n", g_alloc_count);
    }
    TEST_ASSERT(g_alloc_count == 0);
#endif

    impulse_vm_context_destroy(ctx);
    std::printf("  [PASS] #145 OP_CB/OP_92/OP_93 0 heap allocations on repeat runs\n");
}

// ============================================================================
// #136: MC/DC boundary & null-safety coverage across guarded extern "C" entrypoints
// ============================================================================
void test_issue_136_extern_c_boundaries(const impulse_snapshot_t* snap) {
    std::printf("[TEST] #136 extern \"C\" boundary & exception-guard tests...\n");

    // 1. impulse_snapshot_resolve_key MC/DC branches
    uint32_t out_id = 999;
    TEST_ASSERT(impulse_snapshot_resolve_key(nullptr, 0, "alice", 5, &out_id) == IMPULSE_ERR_INVALID_ARGUMENT);
    TEST_ASSERT(impulse_snapshot_resolve_key(snap, 0, nullptr, 5, &out_id) == IMPULSE_ERR_INVALID_ARGUMENT);
    TEST_ASSERT(impulse_snapshot_resolve_key(snap, 0, "alice", 5, nullptr) == IMPULSE_ERR_INVALID_ARGUMENT);
    TEST_ASSERT(impulse_snapshot_resolve_key(snap, 99, "alice", 5, &out_id) == IMPULSE_ERR_INVALID_ARGUMENT);
    TEST_ASSERT(impulse_snapshot_resolve_key(snap, 0, "alice", 5, &out_id) == IMPULSE_OK);
    TEST_ASSERT(out_id == 0);
    TEST_ASSERT(impulse_snapshot_resolve_key(snap, 0, "nonexistent", 11, &out_id) != IMPULSE_OK);

    // 2. impulse_stmt_bind_* null/empty MC/DC branches
    impulse_stmt_t* stmt = nullptr;
    TEST_ASSERT(impulse_stmt_prepare(snap, "MATCH (u:User)-[:Follows]->(v:User) RETURN v", &stmt) == IMPULSE_OK);
    uint64_t dummy_u64 = 1;
    uint8_t dummy_u8[16] = {0};
    float dummy_f = 1.0f;
    TEST_ASSERT(impulse_stmt_bind_node(nullptr, "$a", 1) == IMPULSE_ERR_INVALID_ARGUMENT);
    TEST_ASSERT(impulse_stmt_bind_node(stmt, nullptr, 1) == IMPULSE_ERR_INVALID_ARGUMENT);
    TEST_ASSERT(impulse_stmt_bind_nodes(stmt, "$n", nullptr, 1) == IMPULSE_ERR_INVALID_ARGUMENT);
    TEST_ASSERT(impulse_stmt_bind_nodes(stmt, "$n", nullptr, 0) == IMPULSE_OK);
    TEST_ASSERT(impulse_stmt_bind_nodes(stmt, "$n", &dummy_u64, 1) == IMPULSE_OK);
    TEST_ASSERT(impulse_stmt_bind_bitset(stmt, "$b", nullptr, 1) == IMPULSE_ERR_INVALID_ARGUMENT);
    TEST_ASSERT(impulse_stmt_bind_bitset(stmt, "$b", &dummy_u64, 1) == IMPULSE_OK);
    TEST_ASSERT(impulse_stmt_bind_roaring(stmt, "$r", nullptr, 1) == IMPULSE_ERR_INVALID_ARGUMENT);
    TEST_ASSERT(impulse_stmt_bind_roaring(stmt, "$r", dummy_u8, 4) == IMPULSE_OK);
    TEST_ASSERT(impulse_stmt_bind_vector(stmt, "$v", nullptr, 1) == IMPULSE_ERR_INVALID_ARGUMENT);
    TEST_ASSERT(impulse_stmt_bind_vector(stmt, "$v", &dummy_f, 1) == IMPULSE_OK);
    TEST_ASSERT(impulse_stmt_bind_int(nullptr, "$i", 1) == IMPULSE_ERR_INVALID_ARGUMENT);
    TEST_ASSERT(impulse_stmt_bind_uint(nullptr, "$u", 1) == IMPULSE_ERR_INVALID_ARGUMENT);
    TEST_ASSERT(impulse_stmt_bind_float(nullptr, "$f", 1.0) == IMPULSE_ERR_INVALID_ARGUMENT);
    TEST_ASSERT(impulse_stmt_bind_str(stmt, "$s", nullptr) == IMPULSE_ERR_INVALID_ARGUMENT);
    TEST_ASSERT(impulse_stmt_bind_uuid(stmt, "$id", nullptr) == IMPULSE_ERR_INVALID_ARGUMENT);
    impulse_stmt_finalize(stmt);

    // 3. impulse_index_* null/invalid-argument guards
    void* idx_bytes = nullptr;
    size_t idx_size = 0;
    int32_t sample_i32[4] = {10, 20, 30, 40};
    TEST_ASSERT(impulse_index_build_permutation(nullptr, 4, 0x03, &idx_bytes, &idx_size) == IMPULSE_ERR_INVALID_ARGUMENT);
    TEST_ASSERT(impulse_index_build_permutation(sample_i32, 0, 0x03, &idx_bytes, &idx_size) == IMPULSE_ERR_INVALID_ARGUMENT);
    TEST_ASSERT(impulse_index_build_zone_map(nullptr, 4, 0x03, &idx_bytes, &idx_size) == IMPULSE_ERR_INVALID_ARGUMENT);
    TEST_ASSERT(impulse_index_build_inverted_bitset(nullptr, 4, &idx_bytes, &idx_size) == IMPULSE_ERR_INVALID_ARGUMENT);
    TEST_ASSERT(impulse_index_build_minimal_perfect_hash(nullptr, 4, &idx_bytes, &idx_size) == IMPULSE_ERR_INVALID_ARGUMENT);
    TEST_ASSERT(impulse_index_build_trigram(nullptr, 4, &idx_bytes, &idx_size) == IMPULSE_ERR_INVALID_ARGUMENT);
    TEST_ASSERT(impulse_index_build_domain_split(nullptr, 4, &idx_bytes, &idx_size) == IMPULSE_ERR_INVALID_ARGUMENT);
    TEST_ASSERT(impulse_index_build_temporal_interval(nullptr, 4, 0x04, &idx_bytes, &idx_size) == IMPULSE_ERR_INVALID_ARGUMENT);

    // 4. impulse_writer_* & compaction null guards
    TEST_ASSERT(impulse_writer_add_domain(nullptr, 0, 0x03, "D") == IMPULSE_ERR_INVALID_ARGUMENT);
    TEST_ASSERT(impulse_writer_add_domain_attribute(nullptr, 0, "a", 0x03, 1, sample_i32, 16, nullptr, 0) == IMPULSE_ERR_INVALID_ARGUMENT);
    TEST_ASSERT(impulse_writer_add_relation(nullptr, 0, 0, 1, 4, 4, 0, nullptr, 0, nullptr, 0) == IMPULSE_ERR_INVALID_ARGUMENT);
    TEST_ASSERT(impulse_writer_add_attribute(nullptr, 0, "a", 0x03, 1, sample_i32, 16, nullptr, 0) == IMPULSE_ERR_INVALID_ARGUMENT);
    TEST_ASSERT(impulse_writer_add_index(nullptr, 0, 0xFFFF, 0, 1, "idx", sample_i32, 16, 0) == IMPULSE_ERR_INVALID_ARGUMENT);
    TEST_ASSERT(impulse_snapshot_compact_to_file(nullptr, nullptr, 0, "/tmp/no.imps") == IMPULSE_ERR_INVALID_ARGUMENT);
    TEST_ASSERT(impulse_snapshot_compact_to_stream(nullptr, nullptr, 0, nullptr, nullptr) == IMPULSE_ERR_INVALID_ARGUMENT);

    // 5. impulse_vm_context_mock_csr_typed null safety
    impulse_vm_context_mock_csr_typed(nullptr, 0, nullptr, nullptr, 0, 0, 4, 4);

    std::printf("  [PASS] #136 extern \"C\" boundary & exception-guard tests\n");
}

// ============================================================================
// #135: impulse_exec() must reuse a thread-local buffer (no per-call heap leak)
// ============================================================================
void test_issue_135_impulse_exec_thread_local_reuse(const impulse_snapshot_t* snap) {
    std::printf("[TEST] #135 impulse_exec thread-local buffer reuse & leak prevention...\n");

    const char* cypher = "MATCH (u:User)-[:Follows]->(v:User) WHERE u.id = $seed RETURN v";
    impulse_execution_result_t res1{};
    TEST_ASSERT(impulse_exec(snap, nullptr, 0, &res1) == IMPULSE_ERR_INVALID_ARGUMENT);
    TEST_ASSERT(impulse_exec(snap, cypher, 0, nullptr) == IMPULSE_ERR_INVALID_ARGUMENT);

    // Invalid syntax must fail cleanly without leaking
    impulse_execution_result_t err_res{};
    TEST_ASSERT(impulse_exec(snap, "MATCH ((( BAD", 0, &err_res) != IMPULSE_OK);

    // Two consecutive executions on the same thread must reuse the thread-local buffer
    TEST_ASSERT(impulse_exec(snap, cypher, 0, &res1) == IMPULSE_OK);
    TEST_ASSERT(res1.status == IMPULSE_VM_OK);
    TEST_ASSERT(res1.row_count > 0);
    TEST_ASSERT(res1.data_ptr != nullptr);
    const void* ptr1 = res1.data_ptr;

    impulse_execution_result_t res2{};
    TEST_ASSERT(impulse_exec(snap, cypher, 1, &res2) == IMPULSE_OK);
    TEST_ASSERT(res2.status == IMPULSE_VM_OK);
    TEST_ASSERT(res2.row_count > 0);
    TEST_ASSERT(res2.data_ptr != nullptr);
    TEST_ASSERT(res2.data_ptr == ptr1);

    std::printf("  [PASS] #135 impulse_exec reuses thread-local buffer without leaking\n");
}

} // namespace

int main() {
    const std::string tmp_snap = (std::filesystem::temp_directory_path() / "impulse_test_v0101_wave1a.imps").string();
    const std::string snap_path = build_test_snapshot_file(tmp_snap.c_str());
    impulse_status_t open_st = IMPULSE_ERR_IO_FAILURE;
    impulse_snapshot_t* snap = impulse_snapshot_open(snap_path.c_str(), &open_st);
    if (open_st != IMPULSE_OK) {
        std::fprintf(stderr, "snapshot open failed (%d): %s\n", static_cast<int>(open_st), impulse_get_last_error());
    }
    TEST_ASSERT(open_st == IMPULSE_OK);
    TEST_ASSERT(snap != nullptr);

    test_issue_144_no_stderr_in_compile_to_impas();
    test_issue_145_stmt_execute_zero_alloc(snap);
    test_issue_145_opcodes_zero_alloc(snap);
    test_issue_136_extern_c_boundaries(snap);
    test_issue_135_impulse_exec_thread_local_reuse(snap);

    impulse_snapshot_close(snap);
    std::remove(snap_path.c_str());
    std::printf("ALL Wave 1A v0.10.1 remediation tests PASSED!\n");
    return 0;
}
