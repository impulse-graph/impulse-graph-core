#include "impulse_graph.h"
#include "impulse_vm.h"
#include "impulse_vm_v11.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "ASSERTION FAILED: %s at %s:%d\n", #cond, __FILE__, __LINE__); \
        std::abort(); \
    } \
} while (0)

static impulse_snapshot_t* build_test_snapshot(const char* path) {
    impulse_writer_t* w = impulse_writer_create(path, 0);
    TEST_ASSERT(w != nullptr);
    TEST_ASSERT(impulse_writer_add_domain(w, 0, 0, "User") == IMPULSE_OK);
    // 0 -> 1, 0 -> 2, 1 -> 3
    uint32_t row_off[5] = {0, 2, 3, 3, 3};
    uint32_t col_tgt[3] = {1, 2, 3};
    TEST_ASSERT(impulse_writer_add_relation(w, 0, 0, 0, 4, 3, 0,
                                            row_off, sizeof(row_off), col_tgt, sizeof(col_tgt)) == IMPULSE_OK);
    TEST_ASSERT(impulse_writer_finalize(w) == IMPULSE_OK);
    impulse_status_t open_rc = IMPULSE_OK;
    impulse_snapshot_t* snap = impulse_snapshot_open(path, &open_rc);
    TEST_ASSERT(open_rc == IMPULSE_OK && snap != nullptr);
    return snap;
}

static void test_statement_lifecycle_cypher_and_reuse(const impulse_snapshot_t* snap) {
    std::printf("Testing SQLite-style impulse_stmt lifecycle with openCypher on .imps v0.11...\n");

    const char* cypher = "MATCH (u:User)-[:rel_0]->(v:User) WHERE u.id = $seed RETURN v";
    impulse_stmt_t* stmt = nullptr;
    TEST_ASSERT(impulse_stmt_prepare(snap, cypher, &stmt) == IMPULSE_OK);
    TEST_ASSERT(stmt != nullptr);

    size_t buf_size = impulse_stmt_buffer_size(stmt);
    TEST_ASSERT(buf_size > 0);

    TEST_ASSERT(impulse_stmt_bind_node(stmt, "$seed", 0) == IMPULSE_OK);
    TEST_ASSERT(impulse_stmt_bind_int(stmt, "$minAge", 18) == IMPULSE_OK);
    TEST_ASSERT(impulse_stmt_bind_str(stmt, "$tenant", "acme-corp") == IMPULSE_OK);
    uint8_t uuid[16] = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
    TEST_ASSERT(impulse_stmt_bind_uuid(stmt, "$tenant_uuid", uuid) == IMPULSE_OK);

    std::vector<uint8_t> buffer(buf_size);
    TEST_ASSERT(impulse_stmt_execute(stmt, buffer.data(), buffer.size()) == IMPULSE_OK);
    TEST_ASSERT(impulse_stmt_row_count(stmt) > 0);
    TEST_ASSERT(impulse_stmt_column_count(stmt) == 1);
    TEST_ASSERT(impulse_stmt_column_type(stmt, 0) == 0x08);
    TEST_ASSERT(std::strlen(impulse_stmt_column_name(stmt, 0)) > 0);
    TEST_ASSERT(!impulse_stmt_column_is_null(stmt, 0, 0));

    const uint64_t* words = static_cast<const uint64_t*>(impulse_stmt_column_data(stmt, 0));
    TEST_ASSERT(words != nullptr);
    TEST_ASSERT((words[0] & 0xFULL) == ((1ULL << 1) | (1ULL << 2)));

    // Zero-alloc reuse with seed = 1 -> reaches node 3
    TEST_ASSERT(impulse_stmt_bind_node(stmt, "$seed", 1) == IMPULSE_OK);
    TEST_ASSERT(impulse_stmt_execute(stmt, buffer.data(), buffer.size()) == IMPULSE_OK);
    words = static_cast<const uint64_t*>(impulse_stmt_column_data(stmt, 0));
    TEST_ASSERT((words[0] & 0xFULL) == (1ULL << 3));

    impulse_stmt_finalize(stmt);
    std::printf("  [PASS] openCypher statement lifecycle & zero-alloc reuse\n");
}

static void test_statement_lifecycle_datalog_and_exec(const impulse_snapshot_t* snap) {
    std::printf("Testing ImpLog Datalog & impulse_exec convenience on .imps v0.11...\n");

    const char* datalog = "reach(Y) :- rel_0(X, Y).";
    impulse_stmt_t* stmt = nullptr;
    TEST_ASSERT(impulse_stmt_prepare(snap, datalog, &stmt) == IMPULSE_OK);
    uint64_t seeds[2] = {0, 1};
    TEST_ASSERT(impulse_stmt_bind_nodes(stmt, "$seeds", seeds, 2) == IMPULSE_OK);
    std::vector<uint8_t> buf(impulse_stmt_buffer_size(stmt));
    TEST_ASSERT(impulse_stmt_execute(stmt, buf.data(), buf.size()) == IMPULSE_OK);
    const uint64_t* words = static_cast<const uint64_t*>(impulse_stmt_column_data(stmt, 0));
    TEST_ASSERT((words[0] & 0xFULL) == ((1ULL << 1) | (1ULL << 2) | (1ULL << 3)));
    impulse_stmt_finalize(stmt);

    impulse_execution_result_t res{};
    TEST_ASSERT(impulse_exec(snap, "MATCH (u:User)-[:rel_0]->(v:User) RETURN v", 0, &res) == IMPULSE_OK);
    TEST_ASSERT(res.status == IMPULSE_VM_V11_OK);
    TEST_ASSERT(res.row_count > 0 && res.column_count == 1 && res.data_ptr != nullptr);
    std::printf("  [PASS] ImpLog Datalog & impulse_exec\n");
}

static void test_statement_null_and_boundaries(const impulse_snapshot_t* snap) {
    std::printf("Testing C Statement API null guards and boundaries...\n");

    impulse_stmt_t* stmt = nullptr;
    TEST_ASSERT(impulse_stmt_prepare(snap, nullptr, &stmt) == IMPULSE_ERR_INVALID_ARGUMENT);
    TEST_ASSERT(impulse_stmt_prepare(snap, "MATCH (a) RETURN a", nullptr) == IMPULSE_ERR_INVALID_ARGUMENT);
    TEST_ASSERT(impulse_stmt_buffer_size(nullptr) == 0);
    impulse_stmt_finalize(nullptr);

    TEST_ASSERT(impulse_stmt_prepare(snap, "MATCH (u:User)-[:rel_0]->(v:User) RETURN v", &stmt) == IMPULSE_OK);
    TEST_ASSERT(impulse_stmt_bind_node(nullptr, "$n", 1) == IMPULSE_ERR_INVALID_ARGUMENT);
    TEST_ASSERT(impulse_stmt_bind_node(stmt, nullptr, 1) == IMPULSE_ERR_INVALID_ARGUMENT);
    TEST_ASSERT(impulse_stmt_bind_nodes(stmt, "$n", nullptr, 5) == IMPULSE_ERR_INVALID_ARGUMENT);
    TEST_ASSERT(impulse_stmt_bind_bitset(stmt, "$b", nullptr, 2) == IMPULSE_ERR_INVALID_ARGUMENT);
    TEST_ASSERT(impulse_stmt_bind_roaring(stmt, "$r", nullptr, 4) == IMPULSE_ERR_INVALID_ARGUMENT);
    TEST_ASSERT(impulse_stmt_bind_str(stmt, "$s", nullptr) == IMPULSE_ERR_INVALID_ARGUMENT);
    TEST_ASSERT(impulse_stmt_bind_uuid(stmt, "$uuid", nullptr) == IMPULSE_ERR_INVALID_ARGUMENT);
    TEST_ASSERT(impulse_stmt_bind_vector(stmt, "$vec", nullptr, 3) == IMPULSE_ERR_INVALID_ARGUMENT);

    TEST_ASSERT(impulse_stmt_execute(nullptr, nullptr, 0) == IMPULSE_ERR_INVALID_ARGUMENT);
    TEST_ASSERT(impulse_stmt_execute(stmt, nullptr, 1000) == IMPULSE_ERR_INVALID_ARGUMENT);
    std::vector<uint8_t> small_buf(10);
    TEST_ASSERT(impulse_stmt_execute(stmt, small_buf.data(), small_buf.size()) == IMPULSE_ERR_INVALID_ARGUMENT);

    TEST_ASSERT(std::strlen(impulse_stmt_column_name(stmt, 999)) == 0);
    TEST_ASSERT(impulse_stmt_column_type(stmt, 999) == 0);
    TEST_ASSERT(impulse_stmt_column_dim(stmt, 999) == 1);
    TEST_ASSERT(impulse_stmt_column_data(stmt, 999) == nullptr);
    TEST_ASSERT(impulse_stmt_column_is_null(stmt, 999, 0));

    impulse_execution_result_t exec_res{};
    TEST_ASSERT(impulse_exec(nullptr, "MATCH (a) RETURN a", 0, &exec_res) == IMPULSE_ERR_INVALID_ARGUMENT);
    TEST_ASSERT(impulse_exec(snap, nullptr, 0, &exec_res) == IMPULSE_ERR_INVALID_ARGUMENT);
    TEST_ASSERT(impulse_exec(snap, "MATCH (a) RETURN a", 0, nullptr) == IMPULSE_ERR_INVALID_ARGUMENT);

    impulse_stmt_finalize(stmt);
    std::printf("  [PASS] C Statement API null & boundary checks\n");
}

int main() {
    std::string path_str = (std::filesystem::temp_directory_path() / "impulse_test_stmt_v11.imps").string();
    const char* path = path_str.c_str();
    impulse_snapshot_t* snap = build_test_snapshot(path);
    test_statement_lifecycle_cypher_and_reuse(snap);
    test_statement_lifecycle_datalog_and_exec(snap);
    test_statement_null_and_boundaries(snap);
    impulse_snapshot_close(snap);
    std::remove(path);
    std::printf("=== All C Statement API (v0.11) Tests Passed ===\n");
    return 0;
}
