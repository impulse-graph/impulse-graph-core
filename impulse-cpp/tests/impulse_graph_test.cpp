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
#include <string>

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

static void test_decimal64_storage_and_validation() {
    const char* dec_file = "__impulse_test_decimal64.imps";
    impulse_writer_t* w = impulse_writer_create(dec_file, 0);
    ASSERT_TRUE(w != nullptr);
    ASSERT_EQ(impulse_writer_add_domain(w, 0, 0x03, "Account"), IMPULSE_OK);
    ASSERT_EQ(impulse_writer_set_domain_node_count(w, 0, 3), IMPULSE_OK);

    int64_t vals[3] = {12345LL, -9999LL, 0LL};
    // Invalid (P, S) rejected at writer
    ASSERT_EQ(impulse_writer_add_domain_attribute_decimal64(w, 0, "bad_p0", 0x15, 1, 0, 0, vals, sizeof(vals)),
              IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_writer_add_domain_attribute_decimal64(w, 0, "bad_p19", 0x15, 1, 19, 2, vals, sizeof(vals)),
              IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_writer_add_domain_attribute_decimal64(w, 0, "bad_s", 0x15, 1, 5, 6, vals, sizeof(vals)),
              IMPULSE_ERR_INVALID_ARGUMENT);

    // Valid DECIMAL64(10, 2)
    ASSERT_EQ(impulse_writer_add_domain_attribute_decimal64(w, 0, "balance", 0x15, 1, 10, 2, vals, sizeof(vals)),
              IMPULSE_OK);

    uint32_t row_offs[4] = {0, 1, 1, 1};
    uint32_t col_idx[1] = {1};
    ASSERT_EQ(impulse_writer_add_relation(w, 0, 0, 0, 3, 1, 0, row_offs, sizeof(row_offs), col_idx, sizeof(col_idx)),
              IMPULSE_OK);
    ASSERT_EQ(impulse_writer_finalize(w), IMPULSE_OK);
    impulse_writer_destroy(w);

    impulse_status_t st = IMPULSE_OK;
    impulse_snapshot_t* snap = impulse_snapshot_open(dec_file, &st);
    ASSERT_EQ(st, IMPULSE_OK);
    ASSERT_TRUE(snap != nullptr);

    const void* ptr = nullptr;
    uint64_t bytes = 0;
    uint8_t tc = 0;
    uint32_t dim = 0;
    ASSERT_EQ(impulse_snapshot_get_domain_attribute_buffers(snap, 0, 0, &ptr, &bytes, nullptr, nullptr, &tc, &dim), IMPULSE_OK);
    ASSERT_EQ(tc, 0x15);
    ASSERT_EQ(dim, 1u);
    ASSERT_EQ(bytes, 24ULL);
    const auto* out_vals = static_cast<const int64_t*>(ptr);
    ASSERT_EQ(out_vals[0], 12345LL);
    ASSERT_EQ(out_vals[1], -9999LL);
    ASSERT_EQ(out_vals[2], 0LL);
    impulse_snapshot_close(snap);

    // Negative open test: raw 0x15 with precision=0, scale=0 (via generic writer API) must be rejected on open
    const char* bad_file = "__impulse_test_decimal64_bad.imps";
    impulse_writer_t* bw = impulse_writer_create(bad_file, 0);
    ASSERT_EQ(impulse_writer_add_domain(bw, 0, 0x03, "Account"), IMPULSE_OK);
    ASSERT_EQ(impulse_writer_set_domain_node_count(bw, 0, 3), IMPULSE_OK);
    ASSERT_EQ(impulse_writer_add_domain_attribute(bw, 0, "bad_dec", 0x15, 1, vals, sizeof(vals), nullptr, 0), IMPULSE_OK);
    ASSERT_EQ(impulse_writer_add_relation(bw, 0, 0, 0, 3, 1, 0, row_offs, sizeof(row_offs), col_idx, sizeof(col_idx)),
              IMPULSE_OK);
    ASSERT_EQ(impulse_writer_finalize(bw), IMPULSE_OK);
    impulse_writer_destroy(bw);

    impulse_snapshot_t* bad_snap = impulse_snapshot_open(bad_file, &st);
    ASSERT_TRUE(bad_snap == nullptr);
    ASSERT_EQ(st, IMPULSE_ERR_INVALID_ARGUMENT);

    // Regression: valid relation DECIMAL64(10, 2) via impulse_writer_add_attribute_decimal64
    const char* rel_ok_file = "__impulse_test_decimal64_rel_ok.imps";
    impulse_writer_t* rw = impulse_writer_create(rel_ok_file, 0);
    ASSERT_EQ(impulse_writer_add_domain(rw, 0, 0x03, "Account"), IMPULSE_OK);
    ASSERT_EQ(impulse_writer_set_domain_node_count(rw, 0, 3), IMPULSE_OK);
    uint32_t rel_rows[4] = {0, 2, 3, 3};
    uint32_t rel_cols[3] = {1, 2, 0};
    ASSERT_EQ(impulse_writer_add_relation(rw, 0, 0, 0, 3, 3, 0, rel_rows, sizeof(rel_rows), rel_cols, sizeof(rel_cols)),
              IMPULSE_OK);
    // Nullable relation DECIMAL64(10, 2): 128B validity bitmap + 24B payload = 152B
    std::vector<uint8_t> nullable_rel_buf(128 + 24, 0);
    nullable_rel_buf[0] = 0x05; // edges 0 and 2 valid
    std::memcpy(nullable_rel_buf.data() + 128, vals, sizeof(vals));
    ASSERT_EQ(impulse_writer_add_attribute_decimal64(rw, 0, "fee", 0x95, 1, 10, 2, nullable_rel_buf.data(), nullable_rel_buf.size()),
              IMPULSE_OK);
    ASSERT_EQ(impulse_writer_finalize(rw), IMPULSE_OK);
    impulse_writer_destroy(rw);

    impulse_snapshot_t* rel_snap = impulse_snapshot_open(rel_ok_file, &st);
    ASSERT_EQ(st, IMPULSE_OK);
    ASSERT_TRUE(rel_snap != nullptr);
    impulse_snapshot_close(rel_snap);

    // Regression #1 (CRITICAL): nullable relation DECIMAL64 with truncated payload (bitmap present, payload only 16B < 24B)
    const char* rel_trunc_file = "__impulse_test_decimal64_rel_trunc.imps";
    impulse_writer_t* rtw = impulse_writer_create(rel_trunc_file, 0);
    ASSERT_EQ(impulse_writer_add_domain(rtw, 0, 0x03, "Account"), IMPULSE_OK);
    ASSERT_EQ(impulse_writer_set_domain_node_count(rtw, 0, 3), IMPULSE_OK);
    ASSERT_EQ(impulse_writer_add_relation(rtw, 0, 0, 0, 3, 3, 0, rel_rows, sizeof(rel_rows), rel_cols, sizeof(rel_cols)),
              IMPULSE_OK);
    std::vector<uint8_t> trunc_rel_buf(128 + 16, 0);
    ASSERT_EQ(impulse_writer_add_attribute_decimal64(rtw, 0, "fee", 0x95, 1, 10, 2, trunc_rel_buf.data(), trunc_rel_buf.size()),
              IMPULSE_OK);
    ASSERT_EQ(impulse_writer_finalize(rtw), IMPULSE_OK);
    impulse_writer_destroy(rtw);

    impulse_snapshot_t* rel_trunc_snap = impulse_snapshot_open(rel_trunc_file, &st);
    ASSERT_TRUE(rel_trunc_snap == nullptr);
    ASSERT_EQ(st, IMPULSE_ERR_BUFFER_OVERFLOW);

    // Regression #2 (HIGH): relation DECIMAL64 with empty payload (data_bytes == 0) when edge_count > 0
    const char* rel_empty_file = "__impulse_test_decimal64_rel_empty.imps";
    impulse_writer_t* rew = impulse_writer_create(rel_empty_file, 0);
    ASSERT_EQ(impulse_writer_add_domain(rew, 0, 0x03, "Account"), IMPULSE_OK);
    ASSERT_EQ(impulse_writer_set_domain_node_count(rew, 0, 3), IMPULSE_OK);
    ASSERT_EQ(impulse_writer_add_relation(rew, 0, 0, 0, 3, 3, 0, rel_rows, sizeof(rel_rows), rel_cols, sizeof(rel_cols)),
              IMPULSE_OK);
    ASSERT_EQ(impulse_writer_add_attribute_decimal64(rew, 0, "fee", 0x15, 1, 10, 2, nullptr, 0), IMPULSE_OK);
    ASSERT_EQ(impulse_writer_finalize(rew), IMPULSE_OK);
    impulse_writer_destroy(rew);

    impulse_snapshot_t* rel_empty_snap = impulse_snapshot_open(rel_empty_file, &st);
    ASSERT_TRUE(rel_empty_snap == nullptr);
    ASSERT_EQ(st, IMPULSE_ERR_BUFFER_OVERFLOW);

    // Regression #5 (MEDIUM): dimension == 0 rejected for DECIMAL64
    ASSERT_EQ(impulse_writer_add_attribute_decimal64(nullptr, 0, "x", 0x15, 0, 10, 2, vals, sizeof(vals)),
              IMPULSE_ERR_INVALID_ARGUMENT);
    const char* dim0_file = "__impulse_test_decimal64_dim0.imps";
    impulse_writer_t* d0w = impulse_writer_create(dim0_file, 0);
    ASSERT_EQ(impulse_writer_add_domain(d0w, 0, 0x03, "Account"), IMPULSE_OK);
    ASSERT_EQ(impulse_writer_set_domain_node_count(d0w, 0, 3), IMPULSE_OK);
    ASSERT_EQ(impulse_writer_add_domain_attribute_decimal64(d0w, 0, "d0", 0x15, 0, 10, 2, vals, sizeof(vals)),
              IMPULSE_OK);
    ASSERT_EQ(impulse_writer_add_relation(d0w, 0, 0, 0, 3, 1, 0, row_offs, sizeof(row_offs), col_idx, sizeof(col_idx)),
              IMPULSE_OK);
    ASSERT_EQ(impulse_writer_finalize(d0w), IMPULSE_OK);
    impulse_writer_destroy(d0w);
    impulse_snapshot_t* dim0_snap = impulse_snapshot_open(dim0_file, &st);
    ASSERT_TRUE(dim0_snap == nullptr);
    ASSERT_EQ(st, IMPULSE_ERR_INVALID_ARGUMENT);

    std::remove(dec_file);
    std::remove(bad_file);
    std::remove(rel_ok_file);
    std::remove(rel_trunc_file);
    std::remove(rel_empty_file);
    std::remove(dim0_file);
    std::printf("  PASS: test_decimal64_storage_and_validation\n");
}

static void test_phase0_metadata_introspection() {
    const char* meta_file = "__impulse_test_phase0_introspection.imps";
    const uint32_t row_offsets[] = { 0, 1, 2, 2 };
    const uint32_t col_indices[] = { 1, 2 };
    const int64_t dom_dec_vals[] = { 10050, 20075, 30000 };
    const int64_t rel_dec_vals[] = { 500, 1250 };
    const char str_blob[] = "alicebobcarol";
    const uint32_t str_offs[] = { 0, 5, 8, 13 };
    const uint32_t idx_payload[] = { 2, 0, 1 };

    impulse_writer_t* w = impulse_writer_create(meta_file, IMPULSE_GLOBAL_FEAT_4KB_PAGE_ALIGNED);
    ASSERT_TRUE(w != nullptr);

    ASSERT_EQ(impulse_writer_add_domain(w, 0, IMPULSE_KEY_TYPE_STRING, "User"), IMPULSE_OK);
    ASSERT_EQ(impulse_writer_set_domain_node_count(w, 0, 3), IMPULSE_OK);
    ASSERT_EQ(impulse_writer_set_domain_dense_id_width(w, 0, 4), IMPULSE_OK);
    ASSERT_EQ(impulse_writer_add_domain_attribute(
        w, 0, "username", 0x0B, 0, str_blob, sizeof(str_blob) - 1, str_offs, sizeof(str_offs)
    ), IMPULSE_OK);
    ASSERT_EQ(impulse_writer_set_domain_key_attr(w, 0, 0), IMPULSE_OK);
    ASSERT_EQ(impulse_writer_add_domain_attribute_decimal64(
        w, 0, "balance", 0x15, 1, 12, 2, dom_dec_vals, sizeof(dom_dec_vals)
    ), IMPULSE_OK);

    ASSERT_EQ(impulse_writer_add_relation(
        w, 0, 0, IMPULSE_ENC_RAW, 3, 2, 0,
        row_offsets, sizeof(row_offsets), col_indices, sizeof(col_indices)
    ), IMPULSE_OK);
    ASSERT_EQ(impulse_writer_add_attribute_decimal64(
        w, 0, "amount", 0x15, 1, 10, 2, rel_dec_vals, sizeof(rel_dec_vals)
    ), IMPULSE_OK);

    ASSERT_EQ(impulse_writer_add_index(
        w, 0, 0xFFFF, 1, 0x01, "idx_user_balance", idx_payload, sizeof(idx_payload), 0xCAFEBABEULL
    ), IMPULSE_OK);

    ASSERT_EQ(impulse_writer_set_metadata(w, "z_key", "last"), IMPULSE_OK);
    ASSERT_EQ(impulse_writer_set_metadata(w, "a_key", "first"), IMPULSE_OK);
    ASSERT_EQ(impulse_writer_set_metadata(w, "m_key", "middle"), IMPULSE_OK);

    ASSERT_EQ(impulse_writer_finalize(w), IMPULSE_OK);
    impulse_writer_destroy(w);

    impulse_status_t st = IMPULSE_OK;
    impulse_snapshot_t* snap = impulse_snapshot_open(meta_file, &st);
    ASSERT_EQ(st, IMPULSE_OK);
    ASSERT_TRUE(snap != nullptr);

    // 1. Header & file size (naturally aligned impulse_snapshot_header_info_t)
    impulse_snapshot_header_info_t hdr{};
    ASSERT_EQ(impulse_snapshot_get_header(snap, &hdr), IMPULSE_OK);
    ASSERT_EQ(hdr.magic, IMPULSE_MAGIC);
    ASSERT_EQ(hdr.version, 11);
    ASSERT_EQ(hdr.data_offset, 4096U);
    ASSERT_EQ(hdr.domain_count, 1);
    ASSERT_EQ(hdr.relation_count, 1);
    ASSERT_EQ(hdr.index_count, 1);
    ASSERT_TRUE(hdr.timestamp_ms > 0ULL);
    ASSERT_EQ(hdr.footer_directory_offset, 0ULL);
    ASSERT_EQ(hdr.footer_directory_bytes, 0ULL);
    ASSERT_TRUE((hdr.required_features & IMPULSE_GLOBAL_FEAT_4KB_PAGE_ALIGNED) != 0);
    ASSERT_TRUE(hdr.header_checksum != 0);

    uint64_t fsize = impulse_snapshot_file_size(snap);
    ASSERT_TRUE(fsize >= 4096ULL);
    ASSERT_EQ(hdr.file_size, fsize);

    // 2. SHA-256 over full snapshot + verify UUID bytes match Page 0 offset 0x2E..0x3D
    uint8_t digest[32] = {};
    ASSERT_EQ(impulse_snapshot_compute_sha256(snap, digest), IMPULSE_OK);
    std::FILE* fp = std::fopen(meta_file, "rb");
    ASSERT_TRUE(fp != nullptr);
    std::vector<uint8_t> raw_file(static_cast<size_t>(fsize));
    ASSERT_EQ(std::fread(raw_file.data(), 1, raw_file.size(), fp), raw_file.size());
    std::fclose(fp);
    ASSERT_EQ(std::memcmp(hdr.snapshot_uuid, raw_file.data() + 0x2E, 16), 0);
    uint8_t expected_digest[32] = {};
    impulse_sha256(raw_file.data(), raw_file.size(), expected_digest);
    ASSERT_EQ(std::memcmp(digest, expected_digest, 32), 0);

    // 3. Domain attribute descriptors
    impulse_attribute_descriptor_t da0{}, da1{};
    const char* da0_name = nullptr;
    const char* da1_name = nullptr;
    ASSERT_EQ(impulse_snapshot_get_domain_attribute_descriptor(snap, 0, 0, &da0, &da0_name), IMPULSE_OK);
    ASSERT_TRUE(da0_name != nullptr && std::strcmp(da0_name, "username") == 0);
    ASSERT_EQ(da0.type_code, 0x0B);
    ASSERT_EQ(da0.dimension, 0U);
    ASSERT_TRUE(da0.data_offset >= 4096ULL && da0.data_bytes == sizeof(str_blob) - 1);
    ASSERT_TRUE(da0.offsets_offset >= 4096ULL && da0.offsets_bytes == sizeof(str_offs));

    ASSERT_EQ(impulse_snapshot_get_domain_attribute_descriptor(snap, 0, 1, &da1, &da1_name), IMPULSE_OK);
    ASSERT_TRUE(da1_name != nullptr && std::strcmp(da1_name, "balance") == 0);
    ASSERT_EQ(da1.type_code, 0x15);
    ASSERT_EQ(da1.reserved2, 12); // precision
    ASSERT_EQ(da1.reserved1, 2);  // scale
    ASSERT_EQ(da1.dimension, 1U);
    ASSERT_TRUE(da1.data_offset >= 4096ULL && da1.data_bytes == sizeof(dom_dec_vals));

    // 4. Relation attribute descriptor
    impulse_attribute_descriptor_t ra0{};
    const char* ra0_name = nullptr;
    ASSERT_EQ(impulse_snapshot_get_attribute_descriptor(snap, 0, 0, &ra0, &ra0_name), IMPULSE_OK);
    ASSERT_TRUE(ra0_name != nullptr && std::strcmp(ra0_name, "amount") == 0);
    ASSERT_EQ(ra0.type_code, 0x15);
    ASSERT_EQ(ra0.reserved2, 10);
    ASSERT_EQ(ra0.reserved1, 2);
    ASSERT_TRUE(ra0.data_offset >= 4096ULL && ra0.data_bytes == sizeof(rel_dec_vals));

    // 5. Secondary index entry
    ASSERT_EQ(impulse_snapshot_get_index_count(snap), 1);
    impulse_index_directory_entry_t idx_entry{};
    const char* idx_name = nullptr;
    ASSERT_EQ(impulse_snapshot_get_index_entry(snap, 0, &idx_entry, &idx_name), IMPULSE_OK);
    ASSERT_TRUE(idx_name != nullptr && std::strcmp(idx_name, "idx_user_balance") == 0);
    ASSERT_EQ(idx_entry.domain_id, 0);
    ASSERT_EQ(idx_entry.relation_id, 0xFFFF);
    ASSERT_EQ(idx_entry.attribute_index, 1);
    ASSERT_EQ(idx_entry.index_type, 0x01);
    ASSERT_TRUE(idx_entry.data_offset >= 4096ULL && idx_entry.data_bytes == sizeof(idx_payload));
    ASSERT_EQ(idx_entry.payload_feature_mask, 0xCAFEBABEULL);

    // 6. Metadata enumeration (sorted lexicographically by key)
    ASSERT_EQ(impulse_snapshot_get_metadata_count(snap), 3U);
    const char* k = nullptr;
    const char* v = nullptr;
    ASSERT_EQ(impulse_snapshot_get_metadata_entry(snap, 0, &k, &v), IMPULSE_OK);
    ASSERT_TRUE(std::strcmp(k, "a_key") == 0 && std::strcmp(v, "first") == 0);
    ASSERT_EQ(impulse_snapshot_get_metadata_entry(snap, 1, &k, &v), IMPULSE_OK);
    ASSERT_TRUE(std::strcmp(k, "m_key") == 0 && std::strcmp(v, "middle") == 0);
    ASSERT_EQ(impulse_snapshot_get_metadata_entry(snap, 2, &k, &v), IMPULSE_OK);
    ASSERT_TRUE(std::strcmp(k, "z_key") == 0 && std::strcmp(v, "last") == 0);

    // 7. Virtual components on non-virtual relation returns count = 0
    uint16_t vcount = 99;
    ASSERT_EQ(impulse_snapshot_get_relation_virtual_components(snap, 0, nullptr, 0, &vcount), IMPULSE_OK);
    ASSERT_EQ(vcount, 0);

    // 8. Negative / boundary checks + g_last_error coverage
    ASSERT_EQ(impulse_snapshot_get_header(nullptr, &hdr), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_TRUE(std::strlen(impulse_get_last_error()) > 0);
    ASSERT_EQ(impulse_snapshot_get_header(snap, nullptr), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_file_size(nullptr), 0ULL);
    ASSERT_EQ(impulse_snapshot_compute_sha256(nullptr, digest), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_compute_sha256(snap, nullptr), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_get_domain_attribute_descriptor(nullptr, 0, 0, &da0, nullptr), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_get_domain_attribute_descriptor(snap, 0, 0, nullptr, &da0_name), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_get_domain_attribute_descriptor(snap, 0, 99, &da0, nullptr), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_get_domain_attribute_descriptor(snap, 99, 0, &da0, nullptr), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_get_attribute_descriptor(nullptr, 0, 0, &ra0, nullptr), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_get_attribute_descriptor(snap, 0, 0, nullptr, &ra0_name), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_get_attribute_descriptor(snap, 99, 0, &ra0, nullptr), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_get_attribute_descriptor(snap, 0, 99, &ra0, nullptr), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_get_index_entry(nullptr, 0, &idx_entry, nullptr), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_get_index_entry(snap, 0, nullptr, &idx_name), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_get_index_entry(snap, 99, &idx_entry, nullptr), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_get_metadata_count(nullptr), 0U);
    ASSERT_EQ(impulse_snapshot_get_metadata_entry(nullptr, 0, &k, &v), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_get_metadata_entry(snap, 3, &k, &v), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_get_metadata_entry(snap, 0, nullptr, &v), IMPULSE_OK);
    ASSERT_TRUE(std::strcmp(v, "first") == 0);
    ASSERT_EQ(impulse_snapshot_get_metadata_entry(snap, 0, &k, nullptr), IMPULSE_OK);
    ASSERT_TRUE(std::strcmp(k, "a_key") == 0);
    ASSERT_EQ(impulse_snapshot_get_metadata_entry(snap, 0, nullptr, nullptr), IMPULSE_OK);
    ASSERT_EQ(impulse_snapshot_get_relation_virtual_components(nullptr, 0, nullptr, 0, &vcount), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_get_relation_virtual_components(snap, 99, nullptr, 0, &vcount), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_get_relation_virtual_components(snap, 0, nullptr, 0, nullptr), IMPULSE_ERR_INVALID_ARGUMENT);

    // Zero-allocation metadata lookup checks
    char mval[16] = {};
    ASSERT_EQ(impulse_snapshot_get_metadata(snap, "m_key", mval, sizeof(mval)), IMPULSE_OK);
    ASSERT_TRUE(std::strcmp(mval, "middle") == 0);
    ASSERT_EQ(impulse_snapshot_get_metadata(snap, "missing_key", mval, sizeof(mval)), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_get_metadata(snap, "m_key", mval, 3), IMPULSE_ERR_BUFFER_OVERFLOW);

    impulse_snapshot_close(snap);
    std::remove(meta_file);

    // 9. H4 regression: v0.11 per-domain dense_id_width=2 rejects 32-bit get_relation_buffers
    const char* w16_file = "__impulse_test_phase0_w16.imps";
    const uint32_t w16_rows[] = { 0, 1, 2 };
    const uint16_t w16_cols[] = { 1, 0 };
    impulse_writer_t* w16 = impulse_writer_create(w16_file, IMPULSE_GLOBAL_FEAT_4KB_PAGE_ALIGNED);
    ASSERT_TRUE(w16 != nullptr);
    ASSERT_EQ(impulse_writer_add_domain(w16, 0, IMPULSE_KEY_TYPE_INT32, "SmallDom"), IMPULSE_OK);
    ASSERT_EQ(impulse_writer_set_domain_node_count(w16, 0, 2), IMPULSE_OK);
    ASSERT_EQ(impulse_writer_set_domain_dense_id_width(w16, 0, 2), IMPULSE_OK);
    ASSERT_EQ(impulse_writer_add_relation_ex(
        w16, 0, 0, IMPULSE_ENC_RAW, 0, 4, 2, 2, 0,
        w16_rows, sizeof(w16_rows), w16_cols, sizeof(w16_cols),
        nullptr, 0, nullptr, 0,
        nullptr, 0, nullptr, 0, nullptr, 0, 0
    ), IMPULSE_OK);
    ASSERT_EQ(impulse_writer_finalize(w16), IMPULSE_OK);
    impulse_writer_destroy(w16);

    impulse_snapshot_t* w16_snap = impulse_snapshot_open(w16_file, &st);
    ASSERT_EQ(st, IMPULSE_OK);
    ASSERT_TRUE(w16_snap != nullptr);
    const uint32_t* u32_off = nullptr;
    const uint32_t* u32_tgt = nullptr;
    ASSERT_EQ(impulse_snapshot_get_relation_buffers(w16_snap, 0, &u32_off, &u32_tgt, nullptr, nullptr), IMPULSE_ERR_INVALID_ARGUMENT);
    const void* raw_off = nullptr;
    const void* raw_tgt = nullptr;
    uint8_t raw_nw = 0, raw_ew = 0;
    ASSERT_EQ(impulse_snapshot_get_relation_raw_buffers(w16_snap, 0, &raw_off, &raw_tgt, nullptr, nullptr, &raw_nw, &raw_ew), IMPULSE_OK);
    ASSERT_EQ(raw_nw, 2);
    ASSERT_EQ(raw_ew, 4);
    ASSERT_EQ(impulse_snapshot_get_metadata_count(w16_snap), 0U);
    ASSERT_EQ(impulse_snapshot_get_metadata_entry(w16_snap, 0, &k, &v), IMPULSE_ERR_INVALID_ARGUMENT);
    impulse_snapshot_close(w16_snap);
    std::remove(w16_file);

    // 10. Virtual super-relation introspection (inline, nested, dry-run, BUFFER_OVERFLOW)
    const char* virt_file = "__impulse_test_phase0_virtual.imps";
    {
        std::vector<uint8_t> buf(16384, 0);
        impulse_snapshot_header_t vhdr{};
        vhdr.magic = IMPULSE_MAGIC;
        vhdr.version = 11;
        vhdr.data_offset = 4096;
        vhdr.domain_count = 1;
        vhdr.relation_count = 4; // 0=phys0, 1=phys1, 2=virt_inline(0,1), 3=virt_nested(2)
        vhdr.required_features = IMPULSE_GLOBAL_FEAT_4KB_PAGE_ALIGNED;
        uint16_t crc = 0xFFFF;
        const uint8_t* hbytes = reinterpret_cast<const uint8_t*>(&vhdr);
        for (size_t i = 0; i < 0x3E; ++i) {
            crc ^= static_cast<uint16_t>(hbytes[i]) << 8;
            for (int b = 0; b < 8; ++b) crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021) : static_cast<uint16_t>(crc << 1);
        }
        vhdr.header_checksum = crc;
        std::memcpy(buf.data(), &vhdr, sizeof(vhdr));

        size_t cur = 4096;
        uint32_t str_bytes = 1;
        std::memcpy(buf.data() + cur, &str_bytes, 4);
        cur += 4;
        buf[cur++] = '\0';
        cur = (cur + 127) & ~static_cast<size_t>(127);

        impulse_domain_catalog_entry_t dom{};
        dom.domain_id = 0;
        dom.key_type = IMPULSE_KEY_TYPE_INT32;
        dom.dense_id_width = 4;
        dom.node_count = 2;
        dom.key_attr_index = 0xFFFF;
        std::memcpy(buf.data() + cur, &dom, sizeof(dom));
        cur += sizeof(dom);
        cur = (cur + 127) & ~static_cast<size_t>(127);

        const uint32_t p_rows[] = { 0, 1, 1 };
        const uint32_t p_cols[] = { 1 };
        const size_t p_row_off = 8192;
        const size_t p_col_off = 8320;
        std::memcpy(buf.data() + p_row_off, p_rows, sizeof(p_rows));
        std::memcpy(buf.data() + p_col_off, p_cols, sizeof(p_cols));

        for (uint16_t r = 0; r < 2; ++r) {
            impulse_relation_directory_entry_t rel{};
            rel.relation_id = r;
            rel.node_id_width = 4;
            rel.edge_index_width = 4;
            rel.node_count = 2;
            rel.edge_count = 1;
            rel.csr_row_off_offset = p_row_off;
            rel.csr_row_off_bytes = sizeof(p_rows);
            rel.csr_col_idx_offset = p_col_off;
            rel.csr_col_idx_bytes = sizeof(p_cols);
            std::memcpy(buf.data() + cur, &rel, sizeof(rel));
            cur += sizeof(rel);
        }
        // Relation 2: inline virtual -> {0, 1}
        {
            impulse_relation_directory_entry_t vrel{};
            vrel.relation_id = 2;
            vrel.section_features = IMP_REL_FEATURE_VIRTUAL;
            uint16_t cnt = 2, c0 = 0, c1 = 1;
            std::memcpy(&vrel.reserved2[0], &cnt, 2);
            std::memcpy(&vrel.reserved2[2], &c0, 2);
            std::memcpy(&vrel.reserved2[4], &c1, 2);
            std::memcpy(buf.data() + cur, &vrel, sizeof(vrel));
            cur += sizeof(vrel);
        }
        // Relation 3: nested virtual -> {2}
        {
            impulse_relation_directory_entry_t nrel{};
            nrel.relation_id = 3;
            nrel.section_features = IMP_REL_FEATURE_VIRTUAL;
            uint16_t cnt = 1, c0 = 2;
            std::memcpy(&nrel.reserved2[0], &cnt, 2);
            std::memcpy(&nrel.reserved2[2], &c0, 2);
            std::memcpy(buf.data() + cur, &nrel, sizeof(nrel));
            cur += sizeof(nrel);
        }
        std::FILE* vf = std::fopen(virt_file, "wb");
        ASSERT_TRUE(vf != nullptr);
        ASSERT_EQ(std::fwrite(buf.data(), 1, buf.size(), vf), buf.size());
        std::fclose(vf);
    }
    impulse_snapshot_t* vsnap = impulse_snapshot_open(virt_file, &st);
    ASSERT_EQ(st, IMPULSE_OK);
    ASSERT_TRUE(vsnap != nullptr);
    uint16_t cnt = 0;
    ASSERT_EQ(impulse_snapshot_get_relation_virtual_components(vsnap, 2, nullptr, 0, &cnt), IMPULSE_OK);
    ASSERT_EQ(cnt, 2);
    uint16_t small_buf[1] = {};
    ASSERT_EQ(impulse_snapshot_get_relation_virtual_components(vsnap, 2, small_buf, 1, &cnt), IMPULSE_ERR_BUFFER_OVERFLOW);
    ASSERT_EQ(cnt, 2);
    uint16_t full_buf[2] = {};
    ASSERT_EQ(impulse_snapshot_get_relation_virtual_components(vsnap, 2, full_buf, 2, &cnt), IMPULSE_OK);
    ASSERT_EQ(cnt, 2);
    ASSERT_EQ(full_buf[0], 0);
    ASSERT_EQ(full_buf[1], 1);
    uint16_t nested_buf[1] = {};
    ASSERT_EQ(impulse_snapshot_get_relation_virtual_components(vsnap, 3, nested_buf, 1, &cnt), IMPULSE_OK);
    ASSERT_EQ(cnt, 1);
    ASSERT_EQ(nested_buf[0], 2);
    impulse_snapshot_close(vsnap);
    std::remove(virt_file);

    // 11. H1 & M5 regression: footer metadata with embedded NUL or truncation must be rejected
    const char* bad_meta_file = "__impulse_test_phase0_bad_meta.imps";
    {
        // Build valid minimal snapshot with a footer metadata containing an embedded NUL key
        std::vector<uint8_t> buf(4096 + 128, 0);
        impulse_snapshot_header_t bhdr{};
        bhdr.magic = IMPULSE_MAGIC;
        bhdr.version = 11;
        bhdr.data_offset = 4096;
        uint16_t crc = 0xFFFF;
        const uint8_t* hbytes = reinterpret_cast<const uint8_t*>(&bhdr);
        for (size_t i = 0; i < 0x3E; ++i) {
            crc ^= static_cast<uint16_t>(hbytes[i]) << 8;
            for (int b = 0; b < 8; ++b) crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021) : static_cast<uint16_t>(crc << 1);
        }
        bhdr.header_checksum = crc;
        std::memcpy(buf.data(), &bhdr, sizeof(bhdr));
        uint32_t sb = 1;
        std::memcpy(buf.data() + 4096, &sb, 4);
        buf[4100] = '\0';

        // Append footer metadata: count=1, klen=3 ("a\0b"), vlen=1 ("x")
        size_t footer_start = buf.size();
        uint32_t mcnt = 1;
        uint16_t klen = 3;
        uint32_t vlen = 1;
        buf.insert(buf.end(), reinterpret_cast<uint8_t*>(&mcnt), reinterpret_cast<uint8_t*>(&mcnt) + 4);
        buf.insert(buf.end(), reinterpret_cast<uint8_t*>(&klen), reinterpret_cast<uint8_t*>(&klen) + 2);
        buf.push_back('a'); buf.push_back('\0'); buf.push_back('b');
        buf.insert(buf.end(), reinterpret_cast<uint8_t*>(&vlen), reinterpret_cast<uint8_t*>(&vlen) + 4);
        buf.push_back('x');
        impulse_footer_trailer_t tr{};
        tr.footer_length = (buf.size() - footer_start) + sizeof(tr);
        tr.spec_version = 11;
        tr.footer_magic = IMPULSE_MAGIC;
        buf.insert(buf.end(), reinterpret_cast<uint8_t*>(&tr), reinterpret_cast<uint8_t*>(&tr) + sizeof(tr));

        std::FILE* bf = std::fopen(bad_meta_file, "wb");
        ASSERT_TRUE(bf != nullptr);
        ASSERT_EQ(std::fwrite(buf.data(), 1, buf.size(), bf), buf.size());
        std::fclose(bf);
    }
    impulse_snapshot_t* bad_snap = impulse_snapshot_open(bad_meta_file, &st);
    ASSERT_TRUE(bad_snap == nullptr);
    ASSERT_EQ(st, IMPULSE_ERR_INVALID_ARGUMENT);
    std::remove(bad_meta_file);

    std::printf("  PASS: test_phase0_metadata_introspection\n");
}

static uint16_t compute_test_hdr_crc16(const void* hdr_ptr) {
    const uint8_t* data = static_cast<const uint8_t*>(hdr_ptr);
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < 0x3E; ++i) {
        crc ^= static_cast<uint16_t>(data[i]) << 8;
        for (int b = 0; b < 8; ++b) {
            crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021) : static_cast<uint16_t>(crc << 1);
        }
    }
    return crc;
}

static void write_raw_file(const char* path, const std::vector<uint8_t>& buf) {
    std::FILE* fp = std::fopen(path, "wb");
    ASSERT_TRUE(fp != nullptr);
    ASSERT_EQ(std::fwrite(buf.data(), 1, buf.size(), fp), buf.size());
    std::fclose(fp);
}

static void test_phase0_coverage_matrix() {
    impulse_status_t st = IMPULSE_OK;

    // 1. Virtual super-relations: >10 overflow slab (12 components), <=10 0xFFFF overflow,
    //    empty virtual (0 components), truncated <=10 0xFFFF overflow (M1), and bad attr on virtual (M3).
    const char* ovf_file = "__impulse_test_phase0_virt_ovf.imps";
    {
        std::vector<uint8_t> buf(24576, 0);
        impulse_snapshot_header_t hdr{};
        hdr.magic = IMPULSE_MAGIC;
        hdr.version = 11;
        hdr.data_offset = 4096;
        hdr.domain_count = 1;
        hdr.relation_count = 15; // 0..11 physical, 12=overflow(12), 13=0xFFFF-overflow(2), 14=empty-virt(0)
        hdr.timestamp_ms = 1700000000123ULL;
        hdr.required_features = IMPULSE_GLOBAL_FEAT_4KB_PAGE_ALIGNED;
        for (uint8_t i = 0; i < 16; ++i) hdr.snapshot_uuid[i] = static_cast<uint8_t>(0x10 + i);
        hdr.header_checksum = compute_test_hdr_crc16(&hdr);
        std::memcpy(buf.data(), &hdr, sizeof(hdr));

        size_t cur = 4096;
        uint32_t sb = 1;
        std::memcpy(buf.data() + cur, &sb, 4);
        cur += 4;
        buf[cur++] = '\0';
        cur = (cur + 127) & ~static_cast<size_t>(127);

        impulse_domain_catalog_entry_t dom{};
        dom.domain_id = 0;
        dom.key_type = IMPULSE_KEY_TYPE_INT32;
        dom.dense_id_width = 4;
        dom.node_count = 2;
        dom.key_attr_index = 0xFFFF;
        std::memcpy(buf.data() + cur, &dom, sizeof(dom));
        cur += sizeof(dom);
        cur = (cur + 127) & ~static_cast<size_t>(127);

        const uint32_t p_rows[] = { 0, 1, 1 };
        const uint32_t p_cols[] = { 1 };
        const size_t p_row_off = 12288;
        const size_t p_col_off = 12416;
        const size_t ovf12_off = 12544;
        const size_t ovf2_off = 12672;
        std::memcpy(buf.data() + p_row_off, p_rows, sizeof(p_rows));
        std::memcpy(buf.data() + p_col_off, p_cols, sizeof(p_cols));

        uint16_t ovf12_ids[12];
        for (uint16_t i = 0; i < 12; ++i) ovf12_ids[i] = i;
        std::memcpy(buf.data() + ovf12_off, ovf12_ids, sizeof(ovf12_ids));
        uint16_t ovf2_ids[2] = { 3, 7 };
        std::memcpy(buf.data() + ovf2_off, ovf2_ids, sizeof(ovf2_ids));

        for (uint16_t r = 0; r < 12; ++r) {
            impulse_relation_directory_entry_t rel{};
            rel.relation_id = r;
            rel.node_id_width = 4;
            rel.edge_index_width = 4;
            rel.node_count = 2;
            rel.edge_count = 1;
            rel.csr_row_off_offset = p_row_off;
            rel.csr_row_off_bytes = sizeof(p_rows);
            rel.csr_col_idx_offset = p_col_off;
            rel.csr_col_idx_bytes = sizeof(p_cols);
            std::memcpy(buf.data() + cur, &rel, sizeof(rel));
            cur += sizeof(rel);
        }
        // Rel 12: 12-component overflow virtual relation
        {
            impulse_relation_directory_entry_t vr{};
            vr.relation_id = 12;
            vr.section_features = IMP_REL_FEATURE_VIRTUAL;
            vr.csr_col_idx_offset = ovf12_off;
            vr.csr_col_idx_bytes = sizeof(ovf12_ids);
            uint16_t cnt = 12, sentinel = 0xFFFF;
            std::memcpy(&vr.reserved2[0], &cnt, 2);
            std::memcpy(&vr.reserved2[2], &sentinel, 2);
            std::memcpy(buf.data() + cur, &vr, sizeof(vr));
            cur += sizeof(vr);
        }
        // Rel 13: 2-component explicit 0xFFFF overflow virtual relation
        {
            impulse_relation_directory_entry_t vr{};
            vr.relation_id = 13;
            vr.section_features = IMP_REL_FEATURE_VIRTUAL;
            vr.csr_col_idx_offset = ovf2_off;
            vr.csr_col_idx_bytes = sizeof(ovf2_ids);
            uint16_t cnt = 2, sentinel = 0xFFFF;
            std::memcpy(&vr.reserved2[0], &cnt, 2);
            std::memcpy(&vr.reserved2[2], &sentinel, 2);
            std::memcpy(buf.data() + cur, &vr, sizeof(vr));
            cur += sizeof(vr);
        }
        // Rel 14: 0-component empty virtual relation
        {
            impulse_relation_directory_entry_t vr{};
            vr.relation_id = 14;
            vr.section_features = IMP_REL_FEATURE_VIRTUAL;
            std::memcpy(buf.data() + cur, &vr, sizeof(vr));
            cur += sizeof(vr);
        }
        write_raw_file(ovf_file, buf);

        impulse_snapshot_t* snap = impulse_snapshot_open(ovf_file, &st);
        ASSERT_EQ(st, IMPULSE_OK);
        ASSERT_TRUE(snap != nullptr);

        // Verify explicit timestamp_ms & non-zero snapshot_uuid in header_info
        impulse_snapshot_header_info_t hi{};
        ASSERT_EQ(impulse_snapshot_get_header(snap, &hi), IMPULSE_OK);
        ASSERT_EQ(hi.timestamp_ms, 1700000000123ULL);
        for (uint8_t i = 0; i < 16; ++i) ASSERT_EQ(hi.snapshot_uuid[i], static_cast<uint8_t>(0x10 + i));

        // Rel 12: dry-run, BUFFER_OVERFLOW, and full read
        uint16_t cnt = 0;
        ASSERT_EQ(impulse_snapshot_get_relation_virtual_components(snap, 12, nullptr, 0, &cnt), IMPULSE_OK);
        ASSERT_EQ(cnt, 12);
        uint16_t short_ids[4] = {};
        ASSERT_EQ(impulse_snapshot_get_relation_virtual_components(snap, 12, short_ids, 4, &cnt), IMPULSE_ERR_BUFFER_OVERFLOW);
        ASSERT_EQ(cnt, 12);
        uint16_t full_ids[12] = {};
        ASSERT_EQ(impulse_snapshot_get_relation_virtual_components(snap, 12, full_ids, 12, &cnt), IMPULSE_OK);
        ASSERT_EQ(cnt, 12);
        for (uint16_t i = 0; i < 12; ++i) ASSERT_EQ(full_ids[i], i);

        // Rel 13: 2-component explicit 0xFFFF overflow
        uint16_t two_ids[2] = {};
        ASSERT_EQ(impulse_snapshot_get_relation_virtual_components(snap, 13, two_ids, 2, &cnt), IMPULSE_OK);
        ASSERT_EQ(cnt, 2);
        ASSERT_EQ(two_ids[0], 3);
        ASSERT_EQ(two_ids[1], 7);

        // Rel 14: empty virtual (0 components)
        cnt = 99;
        ASSERT_EQ(impulse_snapshot_get_relation_virtual_components(snap, 14, nullptr, 0, &cnt), IMPULSE_OK);
        ASSERT_EQ(cnt, 0);
        impulse_snapshot_close(snap);

        // M1 regression: mutate Rel 13 (comp_count=2, tag=0xFFFF) to truncated overflow slab -> BUFFER_OVERFLOW
        size_t rel13_off = 4096 + 128 + 128 + 13 * sizeof(impulse_relation_directory_entry_t);
        uint64_t zero_bytes = 0;
        std::memcpy(buf.data() + rel13_off + offsetof(impulse_relation_directory_entry_t, csr_col_idx_bytes), &zero_bytes, 8);
        write_raw_file(ovf_file, buf);
        ASSERT_TRUE(impulse_snapshot_open(ovf_file, &st) == nullptr);
        ASSERT_EQ(st, IMPULSE_ERR_BUFFER_OVERFLOW);
        std::remove(ovf_file);
    }

    // 2. M3 regression: virtual relation with out-of-bounds edge attribute must fail open
    const char* virt_bad_attr_file = "__impulse_test_phase0_virt_bad_attr.imps";
    {
        std::vector<uint8_t> buf(8192, 0);
        impulse_snapshot_header_t hdr{};
        hdr.magic = IMPULSE_MAGIC;
        hdr.version = 11;
        hdr.data_offset = 4096;
        hdr.domain_count = 1;
        hdr.relation_count = 1;
        hdr.header_checksum = compute_test_hdr_crc16(&hdr);
        std::memcpy(buf.data(), &hdr, sizeof(hdr));

        size_t cur = 4096;
        uint32_t sb = 1;
        std::memcpy(buf.data() + cur, &sb, 4);
        cur += 4;
        buf[cur++] = '\0';
        cur = (cur + 127) & ~static_cast<size_t>(127);

        impulse_domain_catalog_entry_t dom{};
        dom.key_type = IMPULSE_KEY_TYPE_INT32;
        dom.dense_id_width = 4;
        dom.key_attr_index = 0xFFFF;
        std::memcpy(buf.data() + cur, &dom, sizeof(dom));
        cur += sizeof(dom);
        cur = (cur + 127) & ~static_cast<size_t>(127);

        impulse_relation_directory_entry_t vr{};
        vr.section_features = IMP_REL_FEATURE_VIRTUAL;
        vr.attr_count = 1;
        std::memcpy(buf.data() + cur, &vr, sizeof(vr));
        cur += sizeof(vr);

        impulse_attribute_descriptor_t bad_attr{};
        bad_attr.type_code = 0x06; // FLOAT32
        bad_attr.dimension = 1;
        bad_attr.data_offset = 1024 * 1024; // OOB
        bad_attr.data_bytes = 128;
        std::memcpy(buf.data() + cur, &bad_attr, sizeof(bad_attr));
        write_raw_file(virt_bad_attr_file, buf);

        ASSERT_TRUE(impulse_snapshot_open(virt_bad_attr_file, &st) == nullptr);
        ASSERT_EQ(st, IMPULSE_ERR_BUFFER_OVERFLOW);
        std::remove(virt_bad_attr_file);
    }

    // 3. v0.9 (0x0009) and v0.10 (0x000A) compatibility with Phase 0 getters
    for (uint16_t compat_ver : { static_cast<uint16_t>(9), static_cast<uint16_t>(10) }) {
        const char* cv_file = "__impulse_test_phase0_compat.imps";
        std::vector<uint8_t> buf(16384, 0);
        impulse_snapshot_header_t hdr{};
        hdr.magic = IMPULSE_MAGIC;
        hdr.version = compat_ver;
        hdr.data_offset = 4096;
        hdr.domain_count = 1;
        hdr.relation_count = 1;
        hdr.header_checksum = compute_test_hdr_crc16(&hdr);
        std::memcpy(buf.data(), &hdr, sizeof(hdr));

        size_t cur = 4096;
        const char pool[] = "\0Dom\0Rel";
        uint32_t sb = sizeof(pool);
        std::memcpy(buf.data() + cur, &sb, 4);
        cur += 4;
        std::memcpy(buf.data() + cur, pool, sizeof(pool));
        cur += sizeof(pool);
        cur = (cur + 127) & ~static_cast<size_t>(127);

        if (compat_ver == 9) {
            impulse_domain_catalog_entry_v0_9_t d9{};
            d9.domain_id = 0;
            d9.key_type = IMPULSE_KEY_TYPE_INT32;
            d9.name_offset = 1;
            d9.node_count = 2;
            std::memcpy(buf.data() + cur, &d9, sizeof(d9));
            cur += sizeof(d9);
        } else {
            impulse_domain_catalog_entry_v0_10_t d10{};
            d10.domain_id = 0;
            d10.key_type = IMPULSE_KEY_TYPE_INT32;
            d10.name_offset = 1;
            d10.node_count = 2;
            d10.key_attr_index = 0xFFFF;
            std::memcpy(buf.data() + cur, &d10, sizeof(d10));
            cur += sizeof(d10);
        }
        cur = (cur + 127) & ~static_cast<size_t>(127);

        const uint32_t rows[] = { 0, 1, 1 };
        const uint32_t cols[] = { 1 };
        std::memcpy(buf.data() + 8192, rows, sizeof(rows));
        std::memcpy(buf.data() + 8320, cols, sizeof(cols));

        impulse_relation_directory_entry_v0_10_t rel{};
        rel.relation_id = 0;
        rel.node_id_width = 4;
        rel.edge_index_width = 4;
        rel.name_offset = 5;
        rel.node_count = 2;
        rel.edge_count = 1;
        rel.csr_row_off_offset = 8192;
        rel.csr_row_off_bytes = sizeof(rows);
        rel.csr_col_idx_offset = 8320;
        rel.csr_col_idx_bytes = sizeof(cols);
        std::memcpy(buf.data() + cur, &rel, sizeof(rel));
        write_raw_file(cv_file, buf);

        impulse_snapshot_t* snap = impulse_snapshot_open(cv_file, &st);
        ASSERT_EQ(st, IMPULSE_OK);
        ASSERT_TRUE(snap != nullptr);
        impulse_snapshot_header_info_t hi{};
        ASSERT_EQ(impulse_snapshot_get_header(snap, &hi), IMPULSE_OK);
        ASSERT_EQ(hi.version, compat_ver);
        ASSERT_EQ(hi.domain_count, 1);
        ASSERT_EQ(hi.relation_count, 1);
        ASSERT_EQ(hi.index_count, 0);
        ASSERT_EQ(impulse_snapshot_get_metadata_count(snap), 0U);
        const char* rname = nullptr;
        ASSERT_EQ(impulse_snapshot_get_relation_name(snap, 0, &rname), IMPULSE_OK);
        ASSERT_TRUE(std::strcmp(rname, "Rel") == 0);
        impulse_snapshot_close(snap);
        std::remove(cv_file);
    }

    // 4. H2 regression: Streaming Footer-Catalog (footer_directory_offset == 0) + Explicit Footer-Catalog
    //    must NOT parse Section 2 directory bytes as garbage metadata.
    for (bool explicit_offset : { false, true }) {
        const char* fc_file = "__impulse_test_phase0_footer_cat.imps";
        std::vector<uint8_t> buf(4096, 0);

        // Append Section 2 directory at offset 4096 (inside footer block)
        const size_t footer_start = buf.size();
        uint32_t sb = 1;
        buf.insert(buf.end(), reinterpret_cast<uint8_t*>(&sb), reinterpret_cast<uint8_t*>(&sb) + 4);
        buf.push_back('\0');
        while (buf.size() % 128 != 0) buf.push_back(0);
        // 0 domains, 0 relations, 0 indices -> dir_end is right after aligned string table
        // Note: impulse_snapshot_open aligns cur to 128 after string table, then reads 0 domains,
        // aligns to 128, reads 0 relations, aligns to 128, reads 0 indices -> dir_end == buf.size()
        const size_t dir_bytes = buf.size() - footer_start;

        // Append valid metadata block: 1 entry ("mode" -> "stream")
        uint32_t mcnt = 1;
        uint16_t klen = 4;
        uint32_t vlen = 6;
        buf.insert(buf.end(), reinterpret_cast<uint8_t*>(&mcnt), reinterpret_cast<uint8_t*>(&mcnt) + 4);
        const char mkey[] = "mode";
        const char mval[] = "stream";
        buf.insert(buf.end(), reinterpret_cast<uint8_t*>(&klen), reinterpret_cast<uint8_t*>(&klen) + 2);
        buf.insert(buf.end(), mkey, mkey + 4);
        buf.insert(buf.end(), reinterpret_cast<uint8_t*>(&vlen), reinterpret_cast<uint8_t*>(&vlen) + 4);
        buf.insert(buf.end(), mval, mval + 6);

        impulse_footer_trailer_t tr{};
        tr.footer_length = (buf.size() - footer_start) + sizeof(tr);
        tr.spec_version = 11;
        tr.footer_magic = IMPULSE_MAGIC;
        buf.insert(buf.end(), reinterpret_cast<uint8_t*>(&tr), reinterpret_cast<uint8_t*>(&tr) + sizeof(tr));

        impulse_snapshot_header_t hdr{};
        hdr.magic = IMPULSE_MAGIC;
        hdr.version = 11;
        hdr.data_offset = 4096;
        hdr.required_features = IMPULSE_GLOBAL_FEAT_FOOTER_CATALOG;
        if (explicit_offset) {
            hdr.footer_directory_offset = footer_start;
            hdr.footer_directory_bytes = dir_bytes;
        }
        hdr.header_checksum = compute_test_hdr_crc16(&hdr);
        std::memcpy(buf.data(), &hdr, sizeof(hdr));
        write_raw_file(fc_file, buf);

        impulse_snapshot_t* snap = impulse_snapshot_open(fc_file, &st);
        ASSERT_EQ(st, IMPULSE_OK);
        ASSERT_TRUE(snap != nullptr);
        impulse_snapshot_header_info_t hi{};
        ASSERT_EQ(impulse_snapshot_get_header(snap, &hi), IMPULSE_OK);
        ASSERT_EQ(hi.footer_directory_offset, explicit_offset ? static_cast<uint64_t>(footer_start) : 0ULL);
        ASSERT_EQ(hi.footer_directory_bytes, explicit_offset ? static_cast<uint64_t>(dir_bytes) : 0ULL);
        ASSERT_EQ(impulse_snapshot_get_metadata_count(snap), 1U);
        const char* mk = nullptr;
        const char* mv = nullptr;
        ASSERT_EQ(impulse_snapshot_get_metadata_entry(snap, 0, &mk, &mv), IMPULSE_OK);
        ASSERT_TRUE(std::strcmp(mk, "mode") == 0 && std::strcmp(mv, "stream") == 0);
        impulse_snapshot_close(snap);
        std::remove(fc_file);
    }

    // 5. Metadata edge cases: empty key/value, duplicate key last-write-wins, embedded NUL in value,
    //    invalid UTF-8 in value, and truncated count/klen.
    auto make_meta_snap = [](const std::vector<uint8_t>& meta_payload) {
        std::vector<uint8_t> buf(4096 + 128, 0);
        impulse_snapshot_header_t hdr{};
        hdr.magic = IMPULSE_MAGIC;
        hdr.version = 11;
        hdr.data_offset = 4096;
        hdr.header_checksum = compute_test_hdr_crc16(&hdr);
        std::memcpy(buf.data(), &hdr, sizeof(hdr));
        uint32_t sb = 1;
        std::memcpy(buf.data() + 4096, &sb, 4);
        buf[4100] = '\0';
        size_t fstart = buf.size();
        buf.insert(buf.end(), meta_payload.begin(), meta_payload.end());
        impulse_footer_trailer_t tr{};
        tr.footer_length = (buf.size() - fstart) + sizeof(tr);
        tr.spec_version = 11;
        tr.footer_magic = IMPULSE_MAGIC;
        buf.insert(buf.end(), reinterpret_cast<uint8_t*>(&tr), reinterpret_cast<uint8_t*>(&tr) + sizeof(tr));
        return buf;
    };

    const char* me_file = "__impulse_test_phase0_meta_edges.imps";
    // 5a. Empty key + empty value + duplicate key overwrite ("dup" -> "v1", then "dup" -> "v2")
    {
        std::vector<uint8_t> mp;
        auto push_u32 = [&](uint32_t v) { mp.insert(mp.end(), reinterpret_cast<uint8_t*>(&v), reinterpret_cast<uint8_t*>(&v) + 4); };
        auto push_u16 = [&](uint16_t v) { mp.insert(mp.end(), reinterpret_cast<uint8_t*>(&v), reinterpret_cast<uint8_t*>(&v) + 2); };
        auto push_kv = [&](const char* k, size_t kl, const char* v, size_t vl) {
            push_u16(static_cast<uint16_t>(kl));
            mp.insert(mp.end(), k, k + kl);
            push_u32(static_cast<uint32_t>(vl));
            mp.insert(mp.end(), v, v + vl);
        };
        push_u32(3);
        push_kv("dup", 3, "v1", 2);
        push_kv("", 0, "", 0);
        push_kv("dup", 3, "v2", 2);
        write_raw_file(me_file, make_meta_snap(mp));

        impulse_snapshot_t* snap = impulse_snapshot_open(me_file, &st);
        ASSERT_EQ(st, IMPULSE_OK);
        ASSERT_TRUE(snap != nullptr);
        ASSERT_EQ(impulse_snapshot_get_metadata_count(snap), 2U);
        const char* k = nullptr;
        const char* v = nullptr;
        ASSERT_EQ(impulse_snapshot_get_metadata_entry(snap, 0, &k, &v), IMPULSE_OK);
        ASSERT_TRUE(std::strcmp(k, "") == 0 && std::strcmp(v, "") == 0);
        ASSERT_EQ(impulse_snapshot_get_metadata_entry(snap, 1, &k, &v), IMPULSE_OK);
        ASSERT_TRUE(std::strcmp(k, "dup") == 0 && std::strcmp(v, "v2") == 0);
        impulse_snapshot_close(snap);
    }
    // 5b. Embedded NUL in value rejected
    {
        std::vector<uint8_t> mp;
        uint32_t cnt = 1, vl = 3;
        uint16_t kl = 1;
        mp.insert(mp.end(), reinterpret_cast<uint8_t*>(&cnt), reinterpret_cast<uint8_t*>(&cnt) + 4);
        mp.insert(mp.end(), reinterpret_cast<uint8_t*>(&kl), reinterpret_cast<uint8_t*>(&kl) + 2);
        mp.push_back('k');
        mp.insert(mp.end(), reinterpret_cast<uint8_t*>(&vl), reinterpret_cast<uint8_t*>(&vl) + 4);
        mp.push_back('a'); mp.push_back('\0'); mp.push_back('b');
        write_raw_file(me_file, make_meta_snap(mp));
        ASSERT_TRUE(impulse_snapshot_open(me_file, &st) == nullptr);
        ASSERT_EQ(st, IMPULSE_ERR_INVALID_ARGUMENT);
    }
    // 5c. Invalid UTF-8 in value rejected
    {
        std::vector<uint8_t> mp;
        uint32_t cnt = 1, vl = 2;
        uint16_t kl = 1;
        mp.insert(mp.end(), reinterpret_cast<uint8_t*>(&cnt), reinterpret_cast<uint8_t*>(&cnt) + 4);
        mp.insert(mp.end(), reinterpret_cast<uint8_t*>(&kl), reinterpret_cast<uint8_t*>(&kl) + 2);
        mp.push_back('k');
        mp.insert(mp.end(), reinterpret_cast<uint8_t*>(&vl), reinterpret_cast<uint8_t*>(&vl) + 4);
        mp.push_back(0xC0); mp.push_back(0xAF); // overlong UTF-8
        write_raw_file(me_file, make_meta_snap(mp));
        ASSERT_TRUE(impulse_snapshot_open(me_file, &st) == nullptr);
        ASSERT_EQ(st, IMPULSE_ERR_INVALID_ARGUMENT);
    }
    // 5d. Truncated count > remaining bytes rejected with BUFFER_OVERFLOW
    {
        std::vector<uint8_t> mp;
        uint32_t cnt = 100;
        mp.insert(mp.end(), reinterpret_cast<uint8_t*>(&cnt), reinterpret_cast<uint8_t*>(&cnt) + 4);
        write_raw_file(me_file, make_meta_snap(mp));
        ASSERT_TRUE(impulse_snapshot_open(me_file, &st) == nullptr);
        ASSERT_EQ(st, IMPULSE_ERR_BUFFER_OVERFLOW);
    }
    std::remove(me_file);

    // 6. H4 CSC regression: src_domain dense_id_width=2 rejects 32-bit get_relation_csc_buffers
    const char* csc16_file = "__impulse_test_phase0_csc16.imps";
    {
        const uint32_t rows[] = { 0, 1, 2 };
        const uint16_t cols16[] = { 1, 0 };
        impulse_writer_t* w = impulse_writer_create(csc16_file, IMPULSE_GLOBAL_FEAT_4KB_PAGE_ALIGNED);
        ASSERT_EQ(impulse_writer_add_domain(w, 0, IMPULSE_KEY_TYPE_INT32, "SmallSrc"), IMPULSE_OK);
        ASSERT_EQ(impulse_writer_set_domain_node_count(w, 0, 2), IMPULSE_OK);
        ASSERT_EQ(impulse_writer_set_domain_dense_id_width(w, 0, 2), IMPULSE_OK);
        ASSERT_EQ(impulse_writer_add_relation_ex(
            w, 0, 0, IMPULSE_ENC_RAW, 0, 4, 2, 2, IMP_REL_FEATURE_INCLUDE_CSC,
            rows, sizeof(rows), cols16, sizeof(cols16),
            rows, sizeof(rows), cols16, sizeof(cols16),
            nullptr, 0, nullptr, 0, nullptr, 0, 0
        ), IMPULSE_OK);
        ASSERT_EQ(impulse_writer_finalize(w), IMPULSE_OK);
        impulse_writer_destroy(w);

        impulse_snapshot_t* snap = impulse_snapshot_open(csc16_file, &st);
        ASSERT_EQ(st, IMPULSE_OK);
        const uint32_t* csc_o = nullptr;
        const uint32_t* csc_t = nullptr;
        ASSERT_EQ(impulse_snapshot_get_relation_csc_buffers(snap, 0, &csc_o, &csc_t, nullptr, nullptr), IMPULSE_ERR_INVALID_ARGUMENT);
        const void* rcsc_o = nullptr;
        const void* rcsc_t = nullptr;
        uint8_t nw = 0, ew = 0;
        ASSERT_EQ(impulse_snapshot_get_relation_csc_raw_buffers(snap, 0, &rcsc_o, &rcsc_t, nullptr, nullptr, &nw, &ew), IMPULSE_OK);
        ASSERT_EQ(nw, 2);
        ASSERT_EQ(ew, 4);
        // v0.11 dense_id_width=2 with rel.node_id_width=0 in is_reachable & sample_neighbors
        ASSERT_TRUE(impulse_snapshot_is_reachable(snap, 0, 0, 1));
        ASSERT_TRUE(impulse_snapshot_is_reachable(snap, 0, 1, 0));
        ASSERT_TRUE(!impulse_snapshot_is_reachable(snap, 0, 0, 0));
        uint64_t seed_nodes[2] = { 0, 1 };
        uint64_t sampled_src[2] = { 99, 99 };
        uint64_t sampled_tgt[2] = { 99, 99 };
        size_t sampled_cnt = 0;
        ASSERT_EQ(impulse_snapshot_sample_neighbors(snap, 0, seed_nodes, 2, 1, 42, sampled_src, sampled_tgt, 2, &sampled_cnt), IMPULSE_OK);
        ASSERT_EQ(sampled_cnt, 2u);
        ASSERT_EQ(sampled_src[0], 0u);
        ASSERT_EQ(sampled_tgt[0], 1u);
        ASSERT_EQ(sampled_src[1], 1u);
        ASSERT_EQ(sampled_tgt[1], 0u);
        impulse_snapshot_close(snap);
        std::remove(csc16_file);
    }

    // 6b. v0.10 non-zero reserved byte 0x03 in domain catalog MUST be zeroed on open
    const char* v10_res_file = "__impulse_test_phase0_v10_reserved.imps";
    {
        const size_t dir_off = 4096;
        const size_t dom_off = dir_off + 128;
        const size_t total_size = dom_off + 128;
        std::vector<uint8_t> buf(total_size, 0);
        impulse_snapshot_header_t hdr{};
        hdr.magic = IMPULSE_MAGIC;
        hdr.version = 10;
        hdr.data_offset = static_cast<uint32_t>(dir_off);
        hdr.domain_count = 1;
        hdr.relation_count = 0;
        hdr.header_checksum = compute_test_hdr_crc16(&hdr);
        std::memcpy(buf.data(), &hdr, sizeof(hdr));
        uint32_t sp_len = 1;
        std::memcpy(buf.data() + dir_off, &sp_len, 4);
        impulse_domain_catalog_entry_t dom{};
        dom.domain_id = 0;
        dom.key_type = IMPULSE_KEY_TYPE_INT32;
        dom.dense_id_width = 0xAB; // non-zero reserved byte 0x03 in v0.10
        dom.node_count = 5;
        dom.key_attr_index = 0xFFFF;
        std::memcpy(buf.data() + dom_off, &dom, sizeof(dom));
        std::FILE* f = std::fopen(v10_res_file, "wb");
        ASSERT_TRUE(f != nullptr);
        ASSERT_EQ(std::fwrite(buf.data(), 1, buf.size(), f), buf.size());
        std::fclose(f);

        impulse_snapshot_t* snap = impulse_snapshot_open(v10_res_file, &st);
        ASSERT_EQ(st, IMPULSE_OK);
        ASSERT_TRUE(snap != nullptr);
        impulse_domain_catalog_entry_t got{};
        ASSERT_EQ(impulse_snapshot_get_domain_entry(snap, 0, &got, nullptr), IMPULSE_OK);
        ASSERT_EQ(got.dense_id_width, 0);
        impulse_snapshot_close(snap);
        std::remove(v10_res_file);
    }

    // 7. Virtual super-relation cycle and bottom-up max-depth (>16) rejection
    auto build_virtual_graph_file = [](const char* path, const std::vector<std::vector<uint16_t>>& children) {
        const uint16_t rel_cnt = static_cast<uint16_t>(children.size());
        const size_t dir_off = 4096;
        const size_t dom_off = dir_off + 128;
        const size_t rel_off = dom_off + 128;
        const size_t rel_bytes = static_cast<size_t>(rel_cnt) * sizeof(impulse_relation_directory_entry_t);
        const size_t total_size = ((rel_off + rel_bytes + 127) / 128) * 128;
        std::vector<uint8_t> buf(total_size, 0);
        impulse_snapshot_header_t hdr{};
        hdr.magic = IMPULSE_MAGIC;
        hdr.version = 11;
        hdr.data_offset = static_cast<uint32_t>(dir_off);
        hdr.domain_count = 1;
        hdr.relation_count = rel_cnt;
        hdr.header_checksum = compute_test_hdr_crc16(&hdr);
        std::memcpy(buf.data(), &hdr, sizeof(hdr));
        uint32_t sp_len = 1;
        std::memcpy(buf.data() + dir_off, &sp_len, 4);
        impulse_domain_catalog_entry_t dom{};
        dom.domain_id = 0;
        dom.key_type = IMPULSE_KEY_TYPE_INT32;
        dom.dense_id_width = 4;
        dom.node_count = 2;
        dom.key_attr_index = 0xFFFF;
        std::memcpy(buf.data() + dom_off, &dom, sizeof(dom));
        size_t cur = rel_off;
        for (uint16_t r = 0; r < rel_cnt; ++r) {
            impulse_relation_directory_entry_t re{};
            re.relation_id = r;
            re.node_id_width = 4;
            re.edge_index_width = 4;
            re.node_count = 2;
            if (!children[r].empty()) {
                re.section_features = IMP_REL_FEATURE_VIRTUAL;
                uint16_t cnt = static_cast<uint16_t>(children[r].size());
                std::memcpy(&re.reserved2[0], &cnt, 2);
                for (size_t j = 0; j < children[r].size(); ++j) {
                    uint16_t cid = children[r][j];
                    std::memcpy(&re.reserved2[2 + j * 2], &cid, 2);
                }
            }
            std::memcpy(buf.data() + cur, &re, sizeof(re));
            cur += sizeof(re);
        }
        std::FILE* f = std::fopen(path, "wb");
        ASSERT_TRUE(f != nullptr);
        ASSERT_EQ(std::fwrite(buf.data(), 1, buf.size(), f), buf.size());
        std::fclose(f);
    };

    const char* vdepth_file = "__impulse_test_phase0_vdepth.imps";
    {
        // Bottom-up chain: r0 physical, r1 -> {r0}, ..., r18 -> {r17} (depth 17 > 16)
        std::vector<std::vector<uint16_t>> ch(19);
        for (uint16_t k = 1; k <= 18; ++k) ch[k] = { static_cast<uint16_t>(k - 1) };
        build_virtual_graph_file(vdepth_file, ch);
        impulse_snapshot_t* snap = impulse_snapshot_open(vdepth_file, &st);
        ASSERT_TRUE(snap == nullptr);
        ASSERT_EQ(st, IMPULSE_ERR_INVALID_ARGUMENT);
        std::remove(vdepth_file);
    }

    const char* vdepth_top_file = "__impulse_test_phase0_vdepth_top.imps";
    {
        // Top-down chain: r0 -> {r1}, r1 -> {r2}, ..., r17 -> {r18}, r18 physical (depth 17 > 16)
        std::vector<std::vector<uint16_t>> ch(19);
        for (uint16_t k = 0; k < 18; ++k) ch[k] = { static_cast<uint16_t>(k + 1) };
        build_virtual_graph_file(vdepth_top_file, ch);
        impulse_snapshot_t* snap = impulse_snapshot_open(vdepth_top_file, &st);
        ASSERT_TRUE(snap == nullptr);
        ASSERT_EQ(st, IMPULSE_ERR_INVALID_ARGUMENT);
        std::remove(vdepth_top_file);
    }

    const char* vcycle_file = "__impulse_test_phase0_vcycle.imps";
    {
        // Mutual cycle: r0 -> {r1}, r1 -> {r0}
        std::vector<std::vector<uint16_t>> ch = { { 1 }, { 0 } };
        build_virtual_graph_file(vcycle_file, ch);
        impulse_snapshot_t* snap = impulse_snapshot_open(vcycle_file, &st);
        ASSERT_TRUE(snap == nullptr);
        ASSERT_EQ(st, IMPULSE_ERR_INVALID_ARGUMENT);
        std::remove(vcycle_file);
    }

    // 8. dir_offset < 4096 rejected with BUFFER_OVERFLOW
    const char* bad_dir_file = "__impulse_test_phase0_bad_dir.imps";
    {
        std::vector<uint8_t> buf(4096, 0);
        impulse_snapshot_header_t hdr{};
        hdr.magic = IMPULSE_MAGIC;
        hdr.version = 11;
        hdr.data_offset = 128; // inside Page 0!
        hdr.header_checksum = compute_test_hdr_crc16(&hdr);
        std::memcpy(buf.data(), &hdr, sizeof(hdr));
        std::FILE* f = std::fopen(bad_dir_file, "wb");
        ASSERT_TRUE(f != nullptr);
        ASSERT_EQ(std::fwrite(buf.data(), 1, buf.size(), f), buf.size());
        std::fclose(f);
        impulse_snapshot_t* snap = impulse_snapshot_open(bad_dir_file, &st);
        ASSERT_TRUE(snap == nullptr);
        ASSERT_EQ(st, IMPULSE_ERR_BUFFER_OVERFLOW);
        std::remove(bad_dir_file);
    }

    // 9. Flat M:1 relation is_reachable & sample_neighbors
    const char* flat_m1_file = "__impulse_test_phase0_flat_m1.imps";
    {
        const uint32_t flat_targets[3] = { 2, 0xFFFFFFFFu, 0 };
        impulse_writer_t* w = impulse_writer_create(flat_m1_file, IMPULSE_GLOBAL_FEAT_4KB_PAGE_ALIGNED);
        ASSERT_EQ(impulse_writer_add_domain(w, 0, IMPULSE_KEY_TYPE_INT32, "Node"), IMPULSE_OK);
        ASSERT_EQ(impulse_writer_set_domain_node_count(w, 0, 3), IMPULSE_OK);
        ASSERT_EQ(impulse_writer_add_relation_ex(
            w, 0, 0, IMPULSE_ENC_RAW, 4, 4, 3, 2, (1ULL << 3), // M:1 flat forward
            nullptr, 0, flat_targets, sizeof(flat_targets),
            nullptr, 0, nullptr, 0,
            nullptr, 0, nullptr, 0, nullptr, 0, 0
        ), IMPULSE_OK);
        ASSERT_EQ(impulse_writer_finalize(w), IMPULSE_OK);
        impulse_writer_destroy(w);

        impulse_snapshot_t* snap = impulse_snapshot_open(flat_m1_file, &st);
        ASSERT_EQ(st, IMPULSE_OK);
        ASSERT_TRUE(impulse_snapshot_is_reachable(snap, 0, 0, 2));
        ASSERT_TRUE(!impulse_snapshot_is_reachable(snap, 0, 1, 0xFFFFFFFFu));
        ASSERT_TRUE(impulse_snapshot_is_reachable(snap, 0, 2, 0));
        uint64_t seeds[3] = { 0, 1, 2 };
        uint64_t out_s[3] = { 0 }, out_t[3] = { 0 };
        size_t cnt = 0;
        ASSERT_EQ(impulse_snapshot_sample_neighbors(snap, 0, seeds, 3, 1, 7, out_s, out_t, 3, &cnt), IMPULSE_OK);
        ASSERT_EQ(cnt, 2u);
        ASSERT_EQ(out_s[0], 0u);
        ASSERT_EQ(out_t[0], 2u);
        ASSERT_EQ(out_s[1], 2u);
        ASSERT_EQ(out_t[1], 0u);
        impulse_snapshot_close(snap);
        std::remove(flat_m1_file);
    }

    // 10. Delta compaction tombstoning all edges on v0.11 dense_id_width=2 relation
    const char* comp_base = "__impulse_test_phase0_comp_base.imps";
    const char* comp_out = "__impulse_test_phase0_comp_out.imps";
    {
        const uint32_t rows[] = { 0, 1, 1 };
        const uint16_t cols16[] = { 1 };
        impulse_writer_t* w = impulse_writer_create(comp_base, IMPULSE_GLOBAL_FEAT_4KB_PAGE_ALIGNED);
        ASSERT_EQ(impulse_writer_add_domain(w, 0, IMPULSE_KEY_TYPE_INT32, "Small"), IMPULSE_OK);
        ASSERT_EQ(impulse_writer_set_domain_node_count(w, 0, 2), IMPULSE_OK);
        ASSERT_EQ(impulse_writer_set_domain_dense_id_width(w, 0, 2), IMPULSE_OK);
        ASSERT_EQ(impulse_writer_add_relation_ex(
            w, 0, 0, IMPULSE_ENC_RAW, 2, 4, 2, 1, 0,
            rows, sizeof(rows), cols16, sizeof(cols16),
            nullptr, 0, nullptr, 0,
            nullptr, 0, nullptr, 0, nullptr, 0, 0
        ), IMPULSE_OK);
        ASSERT_EQ(impulse_writer_finalize(w), IMPULSE_OK);
        impulse_writer_destroy(w);

        impulse_snapshot_t* base = impulse_snapshot_open(comp_base, &st);
        ASSERT_EQ(st, IMPULSE_OK);
        impulse_delta_layer_t* dl = impulse_delta_layer_create(0, 0, "");
        ASSERT_EQ(impulse_delta_layer_tombstone_edge(dl, 0, 1), IMPULSE_OK);
        ASSERT_EQ(impulse_snapshot_compact_to_file(base, &dl, 1, comp_out), IMPULSE_OK);
        impulse_delta_layer_destroy(dl);
        impulse_snapshot_close(base);

        impulse_snapshot_t* compacted = impulse_snapshot_open(comp_out, &st);
        ASSERT_EQ(st, IMPULSE_OK);
        impulse_domain_catalog_entry_t d0{};
        ASSERT_EQ(impulse_snapshot_get_domain_entry(compacted, 0, &d0, nullptr), IMPULSE_OK);
        ASSERT_EQ(d0.dense_id_width, 2);
        impulse_relation_directory_entry_t r0{};
        ASSERT_EQ(impulse_snapshot_get_relation_entry(compacted, 0, &r0), IMPULSE_OK);
        ASSERT_EQ(r0.edge_count, 0u);
        impulse_snapshot_close(compacted);
        std::remove(comp_base);
        std::remove(comp_out);
    }

    std::printf("  PASS: test_phase0_coverage_matrix\n");
}

int main() {
    std::printf("impulse_graph_test: running tests...\n\n");

    test_sha256_known_answer();
    test_round_trip_write_read();
    test_reachability();
    test_neighbor_sampler();
    test_null_arguments();
    test_stream_compaction_and_writer();
    test_decimal64_storage_and_validation();
    test_phase0_metadata_introspection();
    test_phase0_coverage_matrix();

    std::remove(TEST_FILE);
    std::printf("\nAll tests passed.\n");
    return 0;
}
