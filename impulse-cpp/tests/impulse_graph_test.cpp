/*
 * impulse_graph_test.cpp — Round-trip integration test for the Impulse C-ABI kernel (v0.9.0).
 *
 * Tests: writer → finalize → open → inspect → query → sampler → close
 */

#include "impulse_graph.h"
#include "impulse_sha256.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

#define TEST_FILE "__impulse_test_snapshot_v09.bin"

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

static void test_sha256_known_answer() {
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
        0, 0,
        IMPULSE_ENC_RAW,
        4, 5,
        0,
        row_offsets, sizeof(row_offsets),
        col_indices, sizeof(col_indices)
    );
    ASSERT_EQ(st, IMPULSE_OK);

    // Set metadata
    st = impulse_writer_set_metadata(writer, "tenant_id", "tenant_123");
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

    // Metadata check
    char mval[64];
    st = impulse_snapshot_get_metadata(snap, "tenant_id", mval, sizeof(mval));
    ASSERT_EQ(st, IMPULSE_OK);
    ASSERT_EQ(std::string(mval), "tenant_123");

    impulse_snapshot_close(snap);
    std::printf("  PASS: test_round_trip_write_read\n");
}

static void test_reachability() {
    impulse_status_t st;
    impulse_snapshot_t* snap = impulse_snapshot_open(TEST_FILE, &st);
    ASSERT_EQ(st, IMPULSE_OK);

    // Edges: 0→1, 0→2, 1→2, 2→3, 3→0
    ASSERT_TRUE(impulse_snapshot_is_reachable(snap, 0, 0, 1));
    ASSERT_TRUE(impulse_snapshot_is_reachable(snap, 0, 0, 2));
    ASSERT_TRUE(impulse_snapshot_is_reachable(snap, 0, 1, 2));
    ASSERT_TRUE(impulse_snapshot_is_reachable(snap, 0, 2, 3));
    ASSERT_TRUE(impulse_snapshot_is_reachable(snap, 0, 3, 0));

    // Non-edges
    ASSERT_FALSE(impulse_snapshot_is_reachable(snap, 0, 0, 3));
    ASSERT_FALSE(impulse_snapshot_is_reachable(snap, 0, 1, 0));

    impulse_snapshot_close(snap);
    std::printf("  PASS: test_reachability\n");
}

static void test_neighbor_sampler() {
    impulse_status_t st;
    impulse_snapshot_t* snap = impulse_snapshot_open(TEST_FILE, &st);
    ASSERT_EQ(st, IMPULSE_OK);

    uint64_t src_nodes[] = { 0 };
    uint64_t out_src[16], out_tgt[16];
    size_t out_count = 0;

    st = impulse_snapshot_sample_neighbors(
        snap, 0, src_nodes, 1,
        -1, 42,
        out_src, out_tgt, 16, &out_count
    );
    ASSERT_EQ(st, IMPULSE_OK);
    ASSERT_EQ(out_count, 2u);
    ASSERT_EQ(out_src[0], 0u);
    ASSERT_EQ(out_src[1], 0u);
    ASSERT_EQ(out_tgt[0], 1u);
    ASSERT_EQ(out_tgt[1], 2u);

    impulse_snapshot_close(snap);
    std::printf("  PASS: test_neighbor_sampler\n");
}

static void test_null_arguments() {
    impulse_status_t st;
    impulse_snapshot_t* snap = impulse_snapshot_open(nullptr, &st);
    ASSERT_TRUE(snap == nullptr);
    ASSERT_EQ(st, IMPULSE_ERR_INVALID_ARGUMENT);

    ASSERT_TRUE(impulse_writer_create(nullptr, 0) == nullptr);
    ASSERT_EQ(impulse_snapshot_domain_count(nullptr), 0);
    ASSERT_EQ(impulse_snapshot_relation_count(nullptr), 0);
    ASSERT_TRUE(impulse_snapshot_get_buffer(nullptr, 0, 0) == nullptr);

    std::printf("  PASS: test_null_arguments\n");
}

static int32_t memory_write_callback(const void* data, size_t bytes, void* user_data) {
    auto* vec = static_cast<std::vector<uint8_t>*>(user_data);
    const auto* p = static_cast<const uint8_t*>(data);
    vec->insert(vec->end(), p, p + bytes);
    return 0;
}

static void test_stream_compaction_and_writer() {
    impulse_status_t st;
    impulse_snapshot_t* base_snap = impulse_snapshot_open(TEST_FILE, &st);
    ASSERT_EQ(st, IMPULSE_OK);
    ASSERT_TRUE(base_snap != nullptr);

    std::vector<uint8_t> stream_buf;
    st = impulse_snapshot_compact_to_stream(base_snap, nullptr, 0, memory_write_callback, &stream_buf);
    ASSERT_EQ(st, IMPULSE_OK);
    ASSERT_TRUE(stream_buf.size() > 4096);

    const char* stream_file = "__impulse_test_stream_compact.bin";
    FILE* f = std::fopen(stream_file, "wb");
    ASSERT_TRUE(f != nullptr);
    std::fwrite(stream_buf.data(), 1, stream_buf.size(), f);
    std::fclose(f);

    impulse_snapshot_t* streamed_snap = impulse_snapshot_open(stream_file, &st);
    ASSERT_EQ(st, IMPULSE_OK);
    ASSERT_TRUE(streamed_snap != nullptr);

    ASSERT_EQ(impulse_snapshot_domain_count(streamed_snap), 1);
    ASSERT_EQ(impulse_snapshot_relation_count(streamed_snap), 1);

    char mval[64];
    st = impulse_snapshot_get_metadata(streamed_snap, "tenant_id", mval, sizeof(mval));
    ASSERT_EQ(st, IMPULSE_OK);
    ASSERT_EQ(std::string(mval), "tenant_123");

    impulse_snapshot_close(streamed_snap);
    impulse_snapshot_close(base_snap);
    std::remove(stream_file);

    std::printf("  PASS: test_stream_compaction_and_writer\n");
}

int main() {
    std::printf("impulse_graph_test: running tests...\n\n");

    test_sha256_known_answer();
    test_round_trip_write_read();
    test_reachability();
    test_neighbor_sampler();
    test_null_arguments();
    test_stream_compaction_and_writer();

    std::remove(TEST_FILE);
    std::printf("\nAll tests passed.\n");
    return 0;
}
