/*
 * impulse_graph_test.cpp — Round-trip integration test for the Impulse C-ABI kernel.
 *
 * Tests: writer → finalize → open → inspect → query → sampler → close
 * Build: compiled via CMake `ctest` target with IMPULSE_BUILD_TESTS=ON
 */

#include "impulse_graph.h"
#include "impulse_sha256.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#define TEST_FILE "__impulse_test_snapshot.bin"

// Minimal assertion macro with file/line diagnostics
#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        std::fprintf(stderr, "FAIL: %s:%d: %s != %s\n", __FILE__, __LINE__, #a, #b); \
        std::abort(); \
    } \
} while (0)

#define ASSERT_TRUE(x) do { \
    if (!(x)) { \
        std::fprintf(stderr, "FAIL: %s:%d: %s is false\n", __FILE__, __LINE__, #x); \
        std::abort(); \
    } \
} while (0)

#define ASSERT_FALSE(x) do { \
    if ((x)) { \
        std::fprintf(stderr, "FAIL: %s:%d: %s is true\n", __FILE__, __LINE__, #x); \
        std::abort(); \
    } \
} while (0)

// ---- Test: SHA-256 known-answer vector ----
static void test_sha256_known_answer() {
    // SHA-256("abc") = ba7816bf 8f01cfea 414140de 5dae2223 b00361a3 96177a9c b410ff61 f20015ad
    const uint8_t input[] = { 'a', 'b', 'c' };
    uint8_t hash[32];
    impulse_sha256(input, 3, hash);

    const uint8_t expected[32] = {
        0xba,0x78,0x16,0xbf,0x8f,0x01,0xcf,0xea,
        0x41,0x41,0x40,0xde,0x5d,0xae,0x22,0x23,
        0xb0,0x03,0x61,0xa3,0x96,0x17,0x7a,0x9c,
        0xb4,0x10,0xff,0x61,0xf2,0x00,0x15,0xad
    };
    ASSERT_EQ(std::memcmp(hash, expected, 32), 0);
    std::printf("  PASS: test_sha256_known_answer\n");
}

// ---- Test: write → open → inspect round trip ----
static void test_round_trip_write_read() {
    // Build a small graph: 4 nodes, 5 edges
    // 0 → 1, 0 → 2, 1 → 2, 2 → 3, 3 → 0
    const uint32_t row_offsets[] = { 0, 2, 3, 4, 5 };
    const uint32_t col_indices[] = { 1, 2, 2, 3, 0 };

    impulse_writer_t* writer = impulse_writer_create(TEST_FILE, 0);
    ASSERT_TRUE(writer != nullptr);

    impulse_status_t st;
    st = impulse_writer_add_domain(writer, 0, IMPULSE_KEY_TYPE_INT32, "nodes");
    ASSERT_EQ(st, IMPULSE_OK);

    st = impulse_writer_add_relation(
        writer,
        0, 0,                           // src_domain, tgt_domain
        IMPULSE_ENC_RAW_UINT32,         // encoding
        4, 5,                           // node_count, edge_count
        0,                              // section_features (auto-set by writer)
        row_offsets, sizeof(row_offsets),
        col_indices, sizeof(col_indices)
    );
    ASSERT_EQ(st, IMPULSE_OK);

    st = impulse_writer_finalize(writer);
    ASSERT_EQ(st, IMPULSE_OK);
    impulse_writer_destroy(writer);

    // Re-open and validate
    impulse_snapshot_t* snap = impulse_snapshot_open(TEST_FILE, &st);
    ASSERT_EQ(st, IMPULSE_OK);
    ASSERT_TRUE(snap != nullptr);

    ASSERT_EQ(impulse_snapshot_domain_count(snap), 1);
    ASSERT_EQ(impulse_snapshot_relation_count(snap), 1);

    // Inspect relation directory
    impulse_relation_directory_entry_t entry;
    st = impulse_snapshot_get_relation_entry(snap, 0, &entry);
    ASSERT_EQ(st, IMPULSE_OK);
    ASSERT_EQ(entry.node_count, 4ULL);
    ASSERT_EQ(entry.edge_count, 5ULL);
    ASSERT_EQ(entry.encoding_type, IMPULSE_ENC_RAW_UINT32);

    impulse_snapshot_close(snap);
    std::printf("  PASS: test_round_trip_write_read\n");
}

// ---- Test: reachability queries ----
static void test_reachability() {
    impulse_status_t st;
    impulse_snapshot_t* snap = impulse_snapshot_open(TEST_FILE, &st);
    ASSERT_EQ(st, IMPULSE_OK);

    // Edges: 0→1, 0→2, 1→2, 2→3, 3→0
    ASSERT_TRUE(impulse_snapshot_is_reachable(snap, 0, 0, 0, 1));
    ASSERT_TRUE(impulse_snapshot_is_reachable(snap, 0, 0, 0, 2));
    ASSERT_TRUE(impulse_snapshot_is_reachable(snap, 0, 1, 0, 2));
    ASSERT_TRUE(impulse_snapshot_is_reachable(snap, 0, 2, 0, 3));
    ASSERT_TRUE(impulse_snapshot_is_reachable(snap, 0, 3, 0, 0));

    // Non-edges
    ASSERT_FALSE(impulse_snapshot_is_reachable(snap, 0, 0, 0, 3));
    ASSERT_FALSE(impulse_snapshot_is_reachable(snap, 0, 1, 0, 0));
    ASSERT_FALSE(impulse_snapshot_is_reachable(snap, 0, 3, 0, 2));

    // Out-of-range node
    ASSERT_FALSE(impulse_snapshot_is_reachable(snap, 0, 99, 0, 0));

    impulse_snapshot_close(snap);
    std::printf("  PASS: test_reachability\n");
}

// ---- Test: neighbor sampler ----
static void test_neighbor_sampler() {
    impulse_status_t st;
    impulse_snapshot_t* snap = impulse_snapshot_open(TEST_FILE, &st);
    ASSERT_EQ(st, IMPULSE_OK);

    // Sample all neighbors of node 0 (should get 1, 2)
    uint32_t src_nodes[] = { 0 };
    uint32_t out_src[16], out_tgt[16];
    size_t out_count = 0;

    st = impulse_snapshot_sample_neighbors(
        snap, 0, src_nodes, 1,
        -1,     // k_samples = -1 → return all
        42,     // seed
        out_src, out_tgt, 16, &out_count
    );
    ASSERT_EQ(st, IMPULSE_OK);
    ASSERT_EQ(out_count, 2u);  // node 0 has 2 neighbors
    ASSERT_EQ(out_src[0], 0u);
    ASSERT_EQ(out_src[1], 0u);
    // col_indices for node 0: {1, 2}
    ASSERT_EQ(out_tgt[0], 1u);
    ASSERT_EQ(out_tgt[1], 2u);

    // Test capacity overflow
    st = impulse_snapshot_sample_neighbors(
        snap, 0, src_nodes, 1,
        -1, 42,
        out_src, out_tgt, 1, &out_count  // capacity = 1, but node 0 has 2 neighbors
    );
    ASSERT_EQ(st, IMPULSE_ERR_BUFFER_OVERFLOW);
    ASSERT_EQ(out_count, 1u);  // wrote 1 before overflow

    impulse_snapshot_close(snap);
    std::printf("  PASS: test_neighbor_sampler\n");
}

// ---- Test: crypto stubs fail-closed ----
static void test_crypto_stubs_fail_closed() {
    uint8_t fake_sk[64] = {};
    uint8_t fake_pk[32] = {};

    // Signing should fail (not implemented)
    impulse_status_t st = impulse_snapshot_sign_ed25519(TEST_FILE, fake_sk, fake_pk, 0);
    ASSERT_EQ(st, IMPULSE_ERR_UNSUPPORTED_GLOBAL_FEATURE);

    // Verify on unsigned snapshot should succeed (no crypto flag set)
    impulse_snapshot_t* snap = impulse_snapshot_open(TEST_FILE, &st);
    ASSERT_EQ(st, IMPULSE_OK);
    st = impulse_snapshot_verify_ed25519(snap);
    ASSERT_EQ(st, IMPULSE_OK);  // no crypto flag → nothing to verify → OK
    impulse_snapshot_close(snap);

    std::printf("  PASS: test_crypto_stubs_fail_closed\n");
}

// ---- Test: writer destroy does not crash ----
static void test_writer_destroy() {
    impulse_writer_t* writer = impulse_writer_create("__unused.bin", 0);
    ASSERT_TRUE(writer != nullptr);
    impulse_writer_destroy(writer);
    impulse_writer_destroy(nullptr);  // should not crash
    std::printf("  PASS: test_writer_destroy\n");
}

// ---- Test: null argument handling ----
static void test_null_arguments() {
    impulse_status_t st;

    // Null file path
    impulse_snapshot_t* snap = impulse_snapshot_open(nullptr, &st);
    ASSERT_TRUE(snap == nullptr);
    ASSERT_EQ(st, IMPULSE_ERR_INVALID_ARGUMENT);

    // Null writer
    ASSERT_TRUE(impulse_writer_create(nullptr, 0) == nullptr);

    // Null snapshot operations
    ASSERT_EQ(impulse_snapshot_domain_count(nullptr), 0);
    ASSERT_EQ(impulse_snapshot_relation_count(nullptr), 0);
    ASSERT_TRUE(impulse_snapshot_get_buffer(nullptr, 0, 0) == nullptr);
    ASSERT_FALSE(impulse_snapshot_is_reachable(nullptr, 0, 0, 0, 0));

    std::printf("  PASS: test_null_arguments\n");
}

// ---- Test: get_buffer overflow-safe bounds ----
static void test_get_buffer_overflow() {
    impulse_status_t st;
    impulse_snapshot_t* snap = impulse_snapshot_open(TEST_FILE, &st);
    ASSERT_EQ(st, IMPULSE_OK);

    // Valid access
    const void* ptr = impulse_snapshot_get_buffer(snap, 0, 4);
    ASSERT_TRUE(ptr != nullptr);

    // Out of bounds
    ptr = impulse_snapshot_get_buffer(snap, 0, UINT64_MAX);
    ASSERT_TRUE(ptr == nullptr);

    // Overflow: offset + size wraps around
    ptr = impulse_snapshot_get_buffer(snap, UINT64_MAX - 1, 4);
    ASSERT_TRUE(ptr == nullptr);

    impulse_snapshot_close(snap);
    std::printf("  PASS: test_get_buffer_overflow\n");
}

// ---- Test: compaction preserves domain catalog metadata ----
static void test_compaction_domain_catalog_preservation() {
    const char* base_file = "__comp_domain_base.imps";
    const char* comp_file = "__comp_domain_compacted.imps";

    impulse_writer_t* writer = impulse_writer_create(base_file, 0);
    ASSERT_TRUE(writer != nullptr);

    impulse_status_t st = impulse_writer_add_domain(writer, 0, IMPULSE_KEY_TYPE_INT32, "users");
    ASSERT_EQ(st, IMPULSE_OK);
    st = impulse_writer_add_domain(writer, 1, IMPULSE_KEY_TYPE_INT32, "roles");
    ASSERT_EQ(st, IMPULSE_OK);

    const uint32_t row_offs[] = { 0, 2, 3 };
    const uint32_t col_tgts[] = { 1, 2, 0 };

    st = impulse_writer_add_relation(
        writer, 0, 1, IMPULSE_ENC_RAW_UINT32, 2, 3, 0,
        row_offs, sizeof(row_offs), col_tgts, sizeof(col_tgts)
    );
    ASSERT_EQ(st, IMPULSE_OK);
    st = impulse_writer_finalize(writer);
    ASSERT_EQ(st, IMPULSE_OK);
    impulse_writer_destroy(writer);

    impulse_snapshot_t* base_snap = impulse_snapshot_open(base_file, &st);
    ASSERT_EQ(st, IMPULSE_OK);
    ASSERT_EQ(impulse_snapshot_domain_count(base_snap), 2);

    impulse_delta_layer_t* delta = impulse_delta_layer_create(0, 1, "rel");
    impulse_delta_layer_add_edge(delta, 0, 99);

    impulse_delta_layer_t* deltas[] = { delta };
    st = impulse_snapshot_compact_to_file(base_snap, deltas, 1, comp_file);
    ASSERT_EQ(st, IMPULSE_OK);

    impulse_delta_layer_destroy(delta);
    impulse_snapshot_close(base_snap);

    impulse_snapshot_t* comp_snap = impulse_snapshot_open(comp_file, &st);
    ASSERT_EQ(st, IMPULSE_OK);
    ASSERT_TRUE(comp_snap != nullptr);
    ASSERT_EQ(impulse_snapshot_domain_count(comp_snap), 2);

    impulse_snapshot_close(comp_snap);
    std::remove(base_file);
    std::remove(comp_file);
    std::printf("  PASS: test_compaction_domain_catalog_preservation\n");
}

// ---- Test: compaction supports RAW_UINT64 target encoding ----
static void test_compaction_raw_uint64_encoding() {
    const char* base_file = "__comp_u64_base.imps";
    const char* comp_file = "__comp_u64_compacted.imps";

    impulse_writer_t* writer = impulse_writer_create(base_file, 0);
    ASSERT_TRUE(writer != nullptr);

    impulse_writer_add_domain(writer, 0, IMPULSE_KEY_TYPE_INT64, "nodes");

    const uint32_t row_offs[] = { 0, 2 };
    const uint64_t col_tgts64[] = { 100ULL, 200ULL };

    impulse_status_t st = impulse_writer_add_relation(
        writer, 0, 0, IMPULSE_ENC_RAW_UINT64, 1, 2, 0,
        row_offs, sizeof(row_offs), col_tgts64, sizeof(col_tgts64)
    );
    ASSERT_EQ(st, IMPULSE_OK);
    st = impulse_writer_finalize(writer);
    ASSERT_EQ(st, IMPULSE_OK);
    impulse_writer_destroy(writer);

    impulse_snapshot_t* base_snap = impulse_snapshot_open(base_file, &st);
    ASSERT_EQ(st, IMPULSE_OK);

    st = impulse_snapshot_compact_to_file(base_snap, nullptr, 0, comp_file);
    ASSERT_EQ(st, IMPULSE_OK);

    impulse_snapshot_close(base_snap);

    impulse_snapshot_t* comp_snap = impulse_snapshot_open(comp_file, &st);
    ASSERT_EQ(st, IMPULSE_OK);
    ASSERT_TRUE(comp_snap != nullptr);
    ASSERT_TRUE(impulse_snapshot_is_reachable(comp_snap, 0, 0, 0, 100));
    ASSERT_TRUE(impulse_snapshot_is_reachable(comp_snap, 0, 0, 0, 200));

    impulse_snapshot_close(comp_snap);
    std::remove(base_file);
    std::remove(comp_file);
    std::printf("  PASS: test_compaction_raw_uint64_encoding\n");
}

static void test_custom_metadata_and_compaction_updates() {
    const char* base_file = "__comp_meta_base.imps";
    const char* comp_file = "__comp_meta_compacted.imps";

    impulse_writer_t* writer = impulse_writer_create(base_file, 0);
    ASSERT_TRUE(writer != nullptr);

    impulse_status_t st = impulse_writer_add_domain(writer, 0, IMPULSE_KEY_TYPE_INT32, "users");
    ASSERT_EQ(st, IMPULSE_OK);

    st = impulse_writer_set_header_metadata(writer, "tenant_id", "tenant_456");
    ASSERT_EQ(st, IMPULSE_OK);
    st = impulse_writer_set_header_metadata(writer, "kafka_offset", "1001");
    ASSERT_EQ(st, IMPULSE_OK);
    st = impulse_writer_set_extended_metadata(writer, "schema_doc", "{\"version\": \"v2.4\"}");
    ASSERT_EQ(st, IMPULSE_OK);

    // Test non-UTF8 rejection
    const char bad_utf8[] = { (char)0xFF, (char)0xFE, 0 };
    st = impulse_writer_set_header_metadata(writer, "bad_key", bad_utf8);
    ASSERT_EQ(st, IMPULSE_ERR_INVALID_ARGUMENT);

    const uint32_t row_offs[] = { 0, 1 };
    const uint32_t col_tgts[] = { 0 };
    st = impulse_writer_add_relation(
        writer, 0, 0, IMPULSE_ENC_RAW_UINT32, 1, 1, 0,
        row_offs, sizeof(row_offs), col_tgts, sizeof(col_tgts)
    );
    ASSERT_EQ(st, IMPULSE_OK);

    st = impulse_writer_finalize(writer);
    ASSERT_EQ(st, IMPULSE_OK);
    impulse_writer_destroy(writer);

    // Open base snapshot and verify metadata getters
    impulse_snapshot_t* base_snap = impulse_snapshot_open(base_file, &st);
    ASSERT_EQ(st, IMPULSE_OK);

    char val_buf[128];
    st = impulse_snapshot_get_header_metadata(base_snap, "tenant_id", val_buf, sizeof(val_buf));
    ASSERT_EQ(st, IMPULSE_OK);
    ASSERT_TRUE(std::string(val_buf) == "tenant_456");

    st = impulse_snapshot_get_header_metadata(base_snap, "kafka_offset", val_buf, sizeof(val_buf));
    ASSERT_EQ(st, IMPULSE_OK);
    ASSERT_TRUE(std::string(val_buf) == "1001");

    st = impulse_snapshot_get_extended_metadata(base_snap, "schema_doc", val_buf, sizeof(val_buf));
    ASSERT_EQ(st, IMPULSE_OK);
    ASSERT_TRUE(std::string(val_buf) == "{\"version\": \"v2.4\"}");

    // Compact with updated metadata (e.g. updated kafka_offset to 2005)
    const char* keys[] = { "kafka_offset", "compaction_timestamp" };
    const char* vals[] = { "2005", "1785731000" };

    st = impulse_snapshot_compact_to_file_with_metadata(
        base_snap, nullptr, 0, comp_file, keys, vals, 2
    );
    ASSERT_EQ(st, IMPULSE_OK);
    impulse_snapshot_close(base_snap);

    // Verify compacted snapshot retains tenant_id and schema_doc, and updates kafka_offset
    impulse_snapshot_t* comp_snap = impulse_snapshot_open(comp_file, &st);
    ASSERT_EQ(st, IMPULSE_OK);

    st = impulse_snapshot_get_header_metadata(comp_snap, "tenant_id", val_buf, sizeof(val_buf));
    ASSERT_EQ(st, IMPULSE_OK);
    ASSERT_TRUE(std::string(val_buf) == "tenant_456");

    st = impulse_snapshot_get_header_metadata(comp_snap, "kafka_offset", val_buf, sizeof(val_buf));
    ASSERT_EQ(st, IMPULSE_OK);
    ASSERT_TRUE(std::string(val_buf) == "2005");

    st = impulse_snapshot_get_header_metadata(comp_snap, "compaction_timestamp", val_buf, sizeof(val_buf));
    ASSERT_EQ(st, IMPULSE_OK);
    ASSERT_TRUE(std::string(val_buf) == "1785731000");

    st = impulse_snapshot_get_extended_metadata(comp_snap, "schema_doc", val_buf, sizeof(val_buf));
    ASSERT_EQ(st, IMPULSE_OK);
    ASSERT_TRUE(std::string(val_buf) == "{\"version\": \"v2.4\"}");

    impulse_snapshot_close(comp_snap);
    std::remove(base_file);
    std::remove(comp_file);
    std::printf("  PASS: test_custom_metadata_and_compaction_updates\n");
}

int main() {
    std::printf("impulse_graph_test: running tests...\n\n");

    test_sha256_known_answer();
    test_round_trip_write_read();
    test_reachability();
    test_neighbor_sampler();
    test_crypto_stubs_fail_closed();
    test_writer_destroy();
    test_null_arguments();
    test_get_buffer_overflow();
    test_compaction_domain_catalog_preservation();
    test_compaction_raw_uint64_encoding();
    test_custom_metadata_and_compaction_updates();

    // Cleanup test file
    std::remove(TEST_FILE);

    std::printf("\nAll tests passed.\n");
    return 0;
}

