#include "impulse_graph.h"
#include "impulse_vm.h"
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#define TEST_ASSERT(cond) do { \
    if (!(cond)) { \
        std::fprintf(stderr, "ASSERTION FAILED: %s at %s:%d\n", #cond, __FILE__, __LINE__); \
        std::abort(); \
    } \
} while (0)

static void test_statement_lifecycle_cypher() {
    std::printf("Testing SQLite-style impulse_stmt lifecycle with openCypher...\n");

    const char* cypher = "MATCH (u:User)-[:Follows]->(v:User) WHERE u.id = $seed RETURN v";
    impulse_stmt_t* stmt = nullptr;
    
    impulse_status_t rc = impulse_stmt_prepare(nullptr, cypher, &stmt);
    TEST_ASSERT(rc == IMPULSE_OK);
    TEST_ASSERT(stmt != nullptr);

    size_t buf_size = impulse_stmt_buffer_size(stmt);
    TEST_ASSERT(buf_size > 0);

    // Bind parameters
    rc = impulse_stmt_bind_node(stmt, "$seed", 42);
    TEST_ASSERT(rc == IMPULSE_OK);

    rc = impulse_stmt_bind_int(stmt, "$minAge", 18);
    TEST_ASSERT(rc == IMPULSE_OK);

    rc = impulse_stmt_bind_str(stmt, "$tenant", "acme-corp");
    TEST_ASSERT(rc == IMPULSE_OK);

    uint8_t uuid[16] = {0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07, 0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F, 0x10};
    rc = impulse_stmt_bind_uuid(stmt, "$tenant_uuid", uuid);
    TEST_ASSERT(rc == IMPULSE_OK);

    // Allocate caller buffer
    std::vector<uint8_t> buffer(buf_size);

    // Execute in caller buffer
    rc = impulse_stmt_execute(stmt, buffer.data(), buffer.size());
    TEST_ASSERT(rc == IMPULSE_OK);

    // Inspect results
    size_t rows = impulse_stmt_row_count(stmt);
    uint32_t cols = impulse_stmt_column_count(stmt);
    TEST_ASSERT(rows > 0);
    TEST_ASSERT(cols >= 1);

    const char* col0_name = impulse_stmt_column_name(stmt, 0);
    TEST_ASSERT(col0_name != nullptr && std::strlen(col0_name) > 0);

    const void* col0_data = impulse_stmt_column_data(stmt, 0);
    TEST_ASSERT(col0_data != nullptr);

    // Nullability check
    bool is_null = impulse_stmt_column_is_null(stmt, 0, 0);
    TEST_ASSERT(!is_null);

    impulse_stmt_finalize(stmt);
    std::printf("  [PASS] openCypher statement lifecycle\n");
}

static void test_statement_lifecycle_datalog() {
    std::printf("Testing SQLite-style impulse_stmt lifecycle with ImpLog Datalog...\n");

    const char* datalog = "reach(Y) :- edge(X, Y).";
    impulse_stmt_t* stmt = nullptr;
    
    impulse_status_t rc = impulse_stmt_prepare(nullptr, datalog, &stmt);
    TEST_ASSERT(rc == IMPULSE_OK);
    TEST_ASSERT(stmt != nullptr);

    uint64_t seeds[3] = {10, 20, 30};
    rc = impulse_stmt_bind_nodes(stmt, "$seeds", seeds, 3);
    TEST_ASSERT(rc == IMPULSE_OK);

    uint64_t bitset[2] = {0x01ULL, 0x02ULL};
    rc = impulse_stmt_bind_bitset(stmt, "$frontier", bitset, 2);
    TEST_ASSERT(rc == IMPULSE_OK);

    size_t buf_size = impulse_stmt_buffer_size(stmt);
    TEST_ASSERT(buf_size > 0);

    std::vector<uint8_t> buffer(buf_size);
    rc = impulse_stmt_execute(stmt, buffer.data(), buffer.size());
    TEST_ASSERT(rc == IMPULSE_OK);

    impulse_stmt_finalize(stmt);
    std::printf("  [PASS] ImpLog Datalog statement lifecycle\n");
}

static void test_impulse_exec_convenience() {
    std::printf("Testing impulse_exec one-line convenience...\n");

    const char* query = "MATCH (u:User)-[:Follows]->(v:User) RETURN v";
    impulse_execution_result_t result{};
    
    impulse_status_t rc = impulse_exec(nullptr, query, 101, &result);
    TEST_ASSERT(rc == IMPULSE_OK);
    TEST_ASSERT(result.status == IMPULSE_VM_OK);
    TEST_ASSERT(result.row_count > 0);
    TEST_ASSERT(result.column_count >= 1);
    TEST_ASSERT(result.scalar_value == 101);

    std::printf("  [PASS] impulse_exec convenience API\n");
}

static void test_statement_null_and_boundaries() {
    std::printf("Testing C Statement API null argument guards and boundaries...\n");

    impulse_stmt_t* stmt = nullptr;
    TEST_ASSERT(impulse_stmt_prepare(nullptr, nullptr, &stmt) == IMPULSE_ERR_INVALID_ARGUMENT);
    TEST_ASSERT(impulse_stmt_prepare(nullptr, "MATCH (a) RETURN a", nullptr) == IMPULSE_ERR_INVALID_ARGUMENT);
    TEST_ASSERT(impulse_stmt_buffer_size(nullptr) == 0);
    impulse_stmt_finalize(nullptr);

    // Prepare valid statement
    impulse_status_t rc = impulse_stmt_prepare(nullptr, "MATCH (u:User)-[:Follows]->(v:User) RETURN v", &stmt);
    TEST_ASSERT(rc == IMPULSE_OK);
    TEST_ASSERT(stmt != nullptr);

    // Null guards for all bindings
    TEST_ASSERT(impulse_stmt_bind_node(nullptr, "$n", 1) == IMPULSE_ERR_INVALID_ARGUMENT);
    TEST_ASSERT(impulse_stmt_bind_node(stmt, nullptr, 1) == IMPULSE_ERR_INVALID_ARGUMENT);

    TEST_ASSERT(impulse_stmt_bind_nodes(nullptr, "$n", nullptr, 0) == IMPULSE_ERR_INVALID_ARGUMENT);
    TEST_ASSERT(impulse_stmt_bind_nodes(stmt, nullptr, nullptr, 0) == IMPULSE_ERR_INVALID_ARGUMENT);
    TEST_ASSERT(impulse_stmt_bind_nodes(stmt, "$n", nullptr, 5) == IMPULSE_ERR_INVALID_ARGUMENT);

    TEST_ASSERT(impulse_stmt_bind_bitset(nullptr, "$b", nullptr, 0) == IMPULSE_ERR_INVALID_ARGUMENT);
    TEST_ASSERT(impulse_stmt_bind_bitset(stmt, nullptr, nullptr, 0) == IMPULSE_ERR_INVALID_ARGUMENT);
    TEST_ASSERT(impulse_stmt_bind_bitset(stmt, "$b", nullptr, 2) == IMPULSE_ERR_INVALID_ARGUMENT);

    uint8_t roar_bytes[4] = {1, 2, 3, 4};
    TEST_ASSERT(impulse_stmt_bind_roaring(nullptr, "$r", roar_bytes, 4) == IMPULSE_ERR_INVALID_ARGUMENT);
    TEST_ASSERT(impulse_stmt_bind_roaring(stmt, nullptr, roar_bytes, 4) == IMPULSE_ERR_INVALID_ARGUMENT);
    TEST_ASSERT(impulse_stmt_bind_roaring(stmt, "$r", nullptr, 4) == IMPULSE_ERR_INVALID_ARGUMENT);
    TEST_ASSERT(impulse_stmt_bind_roaring(stmt, "$r", roar_bytes, 4) == IMPULSE_OK);

    TEST_ASSERT(impulse_stmt_bind_int(nullptr, "$i", 10) == IMPULSE_ERR_INVALID_ARGUMENT);
    TEST_ASSERT(impulse_stmt_bind_int(stmt, nullptr, 10) == IMPULSE_ERR_INVALID_ARGUMENT);

    TEST_ASSERT(impulse_stmt_bind_uint(nullptr, "$u", 10) == IMPULSE_ERR_INVALID_ARGUMENT);
    TEST_ASSERT(impulse_stmt_bind_uint(stmt, nullptr, 10) == IMPULSE_ERR_INVALID_ARGUMENT);
    TEST_ASSERT(impulse_stmt_bind_uint(stmt, "$u", 10) == IMPULSE_OK);

    TEST_ASSERT(impulse_stmt_bind_float(nullptr, "$f", 3.14) == IMPULSE_ERR_INVALID_ARGUMENT);
    TEST_ASSERT(impulse_stmt_bind_float(stmt, nullptr, 3.14) == IMPULSE_ERR_INVALID_ARGUMENT);
    TEST_ASSERT(impulse_stmt_bind_float(stmt, "$f", 3.14) == IMPULSE_OK);

    TEST_ASSERT(impulse_stmt_bind_str(nullptr, "$s", "val") == IMPULSE_ERR_INVALID_ARGUMENT);
    TEST_ASSERT(impulse_stmt_bind_str(stmt, nullptr, "val") == IMPULSE_ERR_INVALID_ARGUMENT);
    TEST_ASSERT(impulse_stmt_bind_str(stmt, "$s", nullptr) == IMPULSE_ERR_INVALID_ARGUMENT);

    TEST_ASSERT(impulse_stmt_bind_uuid(nullptr, "$uuid", roar_bytes) == IMPULSE_ERR_INVALID_ARGUMENT);
    TEST_ASSERT(impulse_stmt_bind_uuid(stmt, nullptr, roar_bytes) == IMPULSE_ERR_INVALID_ARGUMENT);
    TEST_ASSERT(impulse_stmt_bind_uuid(stmt, "$uuid", nullptr) == IMPULSE_ERR_INVALID_ARGUMENT);

    float vec[3] = {1.0f, 2.0f, 3.0f};
    TEST_ASSERT(impulse_stmt_bind_vector(nullptr, "$vec", vec, 3) == IMPULSE_ERR_INVALID_ARGUMENT);
    TEST_ASSERT(impulse_stmt_bind_vector(stmt, nullptr, vec, 3) == IMPULSE_ERR_INVALID_ARGUMENT);
    TEST_ASSERT(impulse_stmt_bind_vector(stmt, "$vec", nullptr, 3) == IMPULSE_ERR_INVALID_ARGUMENT);
    TEST_ASSERT(impulse_stmt_bind_vector(stmt, "$vec", vec, 3) == IMPULSE_OK);

    // Execute with null buffer and insufficient buffer
    TEST_ASSERT(impulse_stmt_execute(nullptr, nullptr, 0) == IMPULSE_ERR_INVALID_ARGUMENT);
    TEST_ASSERT(impulse_stmt_execute(stmt, nullptr, 1000) == IMPULSE_ERR_INVALID_ARGUMENT);
    std::vector<uint8_t> small_buf(10);
    TEST_ASSERT(impulse_stmt_execute(stmt, small_buf.data(), small_buf.size()) == IMPULSE_ERR_INVALID_ARGUMENT);

    // Out-of-bounds column inspection
    TEST_ASSERT(std::strlen(impulse_stmt_column_name(nullptr, 0)) == 0);
    TEST_ASSERT(std::strlen(impulse_stmt_column_name(stmt, 999)) == 0);
    TEST_ASSERT(impulse_stmt_column_type(nullptr, 0) == 0);
    TEST_ASSERT(impulse_stmt_column_type(stmt, 999) == 0);
    TEST_ASSERT(impulse_stmt_column_dim(nullptr, 0) == 1);
    TEST_ASSERT(impulse_stmt_column_dim(stmt, 999) == 1);
    TEST_ASSERT(impulse_stmt_column_data(nullptr, 0) == nullptr);
    TEST_ASSERT(impulse_stmt_column_data(stmt, 999) == nullptr);
    TEST_ASSERT(impulse_stmt_column_is_null(nullptr, 0, 0));
    TEST_ASSERT(impulse_stmt_column_is_null(stmt, 999, 0));

    // Exec null checks
    impulse_execution_result_t exec_res{};
    TEST_ASSERT(impulse_exec(nullptr, nullptr, 0, &exec_res) == IMPULSE_ERR_INVALID_ARGUMENT);
    TEST_ASSERT(impulse_exec(nullptr, "MATCH (a) RETURN a", 0, nullptr) == IMPULSE_ERR_INVALID_ARGUMENT);

    impulse_stmt_finalize(stmt);
    std::printf("  [PASS] C Statement API null & boundary checks\n");
}

int main() {
    std::printf("=== Running C Statement API Suite ===\n");
    test_statement_lifecycle_cypher();
    test_statement_lifecycle_datalog();
    test_impulse_exec_convenience();
    test_statement_null_and_boundaries();
    std::printf("=== All C Statement API Tests Passed ===\n");
    return 0;
}

