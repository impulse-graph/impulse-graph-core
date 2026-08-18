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

int main() {
    std::printf("=== Running C Statement API Suite ===\n");
    test_statement_lifecycle_cypher();
    test_statement_lifecycle_datalog();
    test_impulse_exec_convenience();
    std::printf("=== All C Statement API Tests Passed ===\n");
    return 0;
}
