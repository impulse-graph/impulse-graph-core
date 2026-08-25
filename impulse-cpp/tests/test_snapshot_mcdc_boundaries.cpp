/*
 * test_snapshot_mcdc_boundaries.cpp — Exhaustive MC/DC Boundary & Condition Test Suite
 * for Snapshot Loading, Parsing, Validation, Headers, Section Alignment, Writer & Sampler.
 */

#include "impulse_graph.h"
#include "impulse_format_v0_9.h"
#include "impulse_sha256.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <unistd.h>

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        std::cerr << "ASSERTION FAILED: " #cond " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::abort(); \
    } \
} while (0)

#define ASSERT_FALSE(cond) do { \
    if (cond) { \
        std::cerr << "ASSERTION FAILED: NOT " #cond " at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::abort(); \
    } \
} while (0)

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        std::cerr << "ASSERTION FAILED: " #a " == " #b " (" << (a) << " != " << (b) << ") at " << __FILE__ << ":" << __LINE__ << std::endl; \
        std::abort(); \
    } \
} while (0)

static uint16_t compute_test_crc16(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= (static_cast<uint16_t>(data[i]) << 8);
        for (int j = 0; j < 8; ++j) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

// -----------------------------------------------------------------------------
// 1. Snapshot Path Resolution & Null Arguments
// -----------------------------------------------------------------------------
void test_mcdc_snapshot_path_and_null_args() {
    std::cout << "[MC/DC Snapshot] Testing Path Resolution & Null Argument Guards..." << std::endl;

    impulse_status_t st = IMPULSE_OK;

    // Null file path
    impulse_snapshot_t* snap = impulse_snapshot_open(nullptr, &st);
    ASSERT_EQ(snap, nullptr);
    ASSERT_EQ(st, IMPULSE_ERR_INVALID_ARGUMENT);

    // Empty string file path
    snap = impulse_snapshot_open("", &st);
    ASSERT_EQ(snap, nullptr);
    ASSERT_EQ(st, IMPULSE_ERR_IO_FAILURE);

    // Non-existent file path
    snap = impulse_snapshot_open("__non_existent_file_123456.bin", &st);
    ASSERT_EQ(snap, nullptr);
    ASSERT_EQ(st, IMPULSE_ERR_IO_FAILURE);

    // get_last_error check
    const char* err = impulse_get_last_error();
    ASSERT_TRUE(err != nullptr);

    // Null accessors
    ASSERT_EQ(impulse_snapshot_magic(nullptr), 0u);
    ASSERT_EQ(impulse_snapshot_version(nullptr), 0u);
    ASSERT_EQ(impulse_snapshot_domain_count(nullptr), 0u);
    ASSERT_EQ(impulse_snapshot_relation_count(nullptr), 0u);
    ASSERT_EQ(impulse_snapshot_max_node_count(nullptr), 0u);
    ASSERT_EQ(impulse_snapshot_get_index_count(nullptr), 0u);

    impulse_domain_catalog_entry_t dom_entry{};
    const char* dname = nullptr;
    ASSERT_EQ(impulse_snapshot_get_domain_entry(nullptr, 0, &dom_entry, &dname), IMPULSE_ERR_INVALID_ARGUMENT);

    impulse_relation_directory_entry_t rel_entry{};
    ASSERT_EQ(impulse_snapshot_get_relation_entry(nullptr, 0, &rel_entry), IMPULSE_ERR_INVALID_ARGUMENT);

    ASSERT_FALSE(impulse_snapshot_is_reachable(nullptr, 0, 0, 0));
    ASSERT_EQ(impulse_snapshot_get_buffer(nullptr, 0, 0), nullptr);

    char val_buf[32];
    ASSERT_EQ(impulse_snapshot_get_metadata(nullptr, "key", val_buf, sizeof(val_buf)), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_get_metadata(snap, nullptr, val_buf, sizeof(val_buf)), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_get_metadata(snap, "key", nullptr, sizeof(val_buf)), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_get_metadata(snap, "key", val_buf, 0), IMPULSE_ERR_INVALID_ARGUMENT);

    // Close null snapshot safely
    impulse_snapshot_close(nullptr);
}

// -----------------------------------------------------------------------------
// 2. Snapshot Header Validation & Checksum Permutations
// -----------------------------------------------------------------------------
void test_mcdc_snapshot_header_validation() {
    std::cout << "[MC/DC Snapshot] Testing Header Validation, CRC16, Magic & Versions..." << std::endl;

    const char* tmp_file = "__test_mcdc_header_tmp.bin";

    // 1. File size < 4096 bytes (Truncated file)
    {
        std::ofstream ofs(tmp_file, std::ios::binary);
        std::vector<uint8_t> small_buf(100, 0x00);
        ofs.write(reinterpret_cast<const char*>(small_buf.data()), small_buf.size());
    }
    impulse_status_t st = IMPULSE_OK;
    impulse_snapshot_t* snap = impulse_snapshot_open(tmp_file, &st);
    ASSERT_EQ(snap, nullptr);
    ASSERT_EQ(st, IMPULSE_ERR_IO_FAILURE);

    // 2. Invalid Magic Bytes (0x12345678 != 0x494D5053)
    {
        std::ofstream ofs(tmp_file, std::ios::binary);
        std::vector<uint8_t> buf(4096, 0x00);
        uint32_t bad_magic = 0x12345678;
        std::memcpy(buf.data(), &bad_magic, 4);
        ofs.write(reinterpret_cast<const char*>(buf.data()), buf.size());
    }
    snap = impulse_snapshot_open(tmp_file, &st);
    ASSERT_EQ(snap, nullptr);
    ASSERT_EQ(st, IMPULSE_ERR_INVALID_MAGIC);

    // 3. Unsupported Version (ver = 0x0001, ver = 0x0080)
    {
        std::ofstream ofs(tmp_file, std::ios::binary);
        std::vector<uint8_t> buf(4096, 0x00);
        uint32_t magic = IMPULSE_MAGIC;
        uint16_t bad_ver = 0x1000;
        std::memcpy(buf.data(), &magic, 4);
        std::memcpy(buf.data() + 4, &bad_ver, 2);
        ofs.write(reinterpret_cast<const char*>(buf.data()), buf.size());
    }
    snap = impulse_snapshot_open(tmp_file, &st);
    ASSERT_EQ(snap, nullptr);
    ASSERT_EQ(st, IMPULSE_ERR_UNSUPPORTED_VERSION);

    // 4. v0.9 Unsupported Global Feature Bitmask
    {
        std::ofstream ofs(tmp_file, std::ios::binary);
        std::vector<uint8_t> buf(4096, 0x00);
        impulse_snapshot_header_t hdr{};
        hdr.magic = IMPULSE_MAGIC;
        hdr.version = IMPULSE_SPEC_VERSION_PACKED;
        hdr.data_offset = 4096;
        hdr.required_features = 0x8000000000000000ULL; // Unknown feature bit
        hdr.header_checksum = compute_test_crc16(reinterpret_cast<const uint8_t*>(&hdr), 0x3E);
        std::memcpy(buf.data(), &hdr, sizeof(hdr));
        ofs.write(reinterpret_cast<const char*>(buf.data()), buf.size());
    }
    snap = impulse_snapshot_open(tmp_file, &st);
    ASSERT_EQ(snap, nullptr);
    ASSERT_EQ(st, IMPULSE_ERR_UNSUPPORTED_GLOBAL_FEATURE);

    // 5. v0.9 Header CRC-16 Checksum Mismatch
    {
        std::ofstream ofs(tmp_file, std::ios::binary);
        std::vector<uint8_t> buf(4096, 0x00);
        impulse_snapshot_header_t hdr{};
        hdr.magic = IMPULSE_MAGIC;
        hdr.version = IMPULSE_SPEC_VERSION_PACKED;
        hdr.data_offset = 4096;
        hdr.header_checksum = 0xDEAD; // Corrupt CRC
        std::memcpy(buf.data(), &hdr, sizeof(hdr));
        ofs.write(reinterpret_cast<const char*>(buf.data()), buf.size());
    }
    snap = impulse_snapshot_open(tmp_file, &st);
    ASSERT_EQ(snap, nullptr);
    ASSERT_EQ(st, IMPULSE_ERR_CORRUPT_CHECKSUM);

    // 6. Directory Offset Out of Bounds (> file_size)
    {
        std::ofstream ofs(tmp_file, std::ios::binary);
        std::vector<uint8_t> buf(4096, 0x00);
        impulse_snapshot_header_t hdr{};
        hdr.magic = IMPULSE_MAGIC;
        hdr.version = IMPULSE_SPEC_VERSION_PACKED;
        hdr.data_offset = 999999; // Out of bounds offset
        hdr.domain_count = 1;
        hdr.header_checksum = compute_test_crc16(reinterpret_cast<const uint8_t*>(&hdr), 0x3E);
        std::memcpy(buf.data(), &hdr, sizeof(hdr));
        ofs.write(reinterpret_cast<const char*>(buf.data()), buf.size());
    }
    snap = impulse_snapshot_open(tmp_file, &st);
    ASSERT_EQ(snap, nullptr);
    ASSERT_EQ(st, IMPULSE_ERR_BUFFER_OVERFLOW);

    // 7. Legacy v2.4 SHA-256 Checksum Match vs Mismatch
    {
        std::ofstream ofs(tmp_file, std::ios::binary);
        std::vector<uint8_t> buf(8192, 0xAA);
        uint32_t magic = IMPULSE_MAGIC;
        uint16_t ver_legacy = 0x0204;
        uint64_t data_off = 4096;
        std::memcpy(buf.data(), &magic, 4);
        std::memcpy(buf.data() + 4, &ver_legacy, 2);
        std::memcpy(buf.data() + 6, &data_off, 8);

        // Put corrupted SHA256 in header
        uint8_t fake_sha[32] = { 0x12, 0x34 };
        std::memcpy(buf.data() + 30, fake_sha, 32);

        ofs.write(reinterpret_cast<const char*>(buf.data()), buf.size());
    }
    snap = impulse_snapshot_open(tmp_file, &st);
    ASSERT_EQ(snap, nullptr);
    ASSERT_EQ(st, IMPULSE_ERR_CORRUPT_CHECKSUM);

    std::remove(tmp_file);
}

// -----------------------------------------------------------------------------
// 3. String Table & UTF-8 Decoder Boundary Tests
// -----------------------------------------------------------------------------
void test_mcdc_string_table_and_utf8() {
    std::cout << "[MC/DC Snapshot] Testing String Table & UTF-8 Validation..." << std::endl;

    const char* tmp_file = "__test_mcdc_str_tmp.bin";

    // 1. String table bytes == 0 or string_pool[0] != '\0'
    {
        std::ofstream ofs(tmp_file, std::ios::binary);
        std::vector<uint8_t> buf(8192, 0x00);
        impulse_snapshot_header_t hdr{};
        hdr.magic = IMPULSE_MAGIC;
        hdr.version = IMPULSE_SPEC_VERSION_PACKED;
        hdr.data_offset = 4096;
        hdr.domain_count = 1;
        hdr.header_checksum = compute_test_crc16(reinterpret_cast<const uint8_t*>(&hdr), 0x3E);
        std::memcpy(buf.data(), &hdr, sizeof(hdr));

        // At data_offset 4096: write string_table_bytes = 4, but string_pool[0] = 'A' (not '\0'!)
        uint32_t str_bytes = 4;
        std::memcpy(buf.data() + 4096, &str_bytes, 4);
        buf[4100] = 'A';
        buf[4101] = 'B';
        buf[4102] = 'C';
        buf[4103] = '\0';

        ofs.write(reinterpret_cast<const char*>(buf.data()), buf.size());
    }
    impulse_status_t st = IMPULSE_OK;
    impulse_snapshot_t* snap = impulse_snapshot_open(tmp_file, &st);
    ASSERT_EQ(snap, nullptr);
    ASSERT_EQ(st, IMPULSE_ERR_INVALID_ARGUMENT);

    // 2. Corrupt UTF-8 in Domain Name (invalid continuation bytes)
    {
        std::ofstream ofs(tmp_file, std::ios::binary);
        std::vector<uint8_t> buf(8192, 0x00);
        impulse_snapshot_header_t hdr{};
        hdr.magic = IMPULSE_MAGIC;
        hdr.version = IMPULSE_SPEC_VERSION_PACKED;
        hdr.data_offset = 4096;
        hdr.domain_count = 1;
        hdr.header_checksum = compute_test_crc16(reinterpret_cast<const uint8_t*>(&hdr), 0x3E);
        std::memcpy(buf.data(), &hdr, sizeof(hdr));

        // String table: '\0', 0xC2, 0x20 (invalid 2-byte UTF8!), '\0'
        uint32_t str_bytes = 4;
        std::memcpy(buf.data() + 4096, &str_bytes, 4);
        buf[4100] = '\0';
        buf[4101] = 0xC2; // 2-byte start
        buf[4102] = 0x20; // Invalid continuation (should be 0x80..0xBF)
        buf[4103] = '\0';

        // Domain entry pointing to name_offset = 1
        size_t dom_cur = 4096 + 4 + str_bytes;
        size_t rem = dom_cur % 128;
        if (rem != 0) dom_cur += 128 - rem;

        impulse_domain_catalog_entry_t dom{};
        dom.domain_id = 0;
        dom.name_offset = 1; // points to invalid UTF8
        std::memcpy(buf.data() + dom_cur, &dom, sizeof(dom));

        ofs.write(reinterpret_cast<const char*>(buf.data()), buf.size());
    }
    snap = impulse_snapshot_open(tmp_file, &st);
    ASSERT_EQ(snap, nullptr);
    ASSERT_EQ(st, IMPULSE_ERR_INVALID_ARGUMENT);

    std::remove(tmp_file);
}

// -----------------------------------------------------------------------------
// 4. Section Alignment & Index Directory Validation
// -----------------------------------------------------------------------------
void test_mcdc_section_alignment_and_indexes() {
    std::cout << "[MC/DC Snapshot] Testing 128B Section Alignment & Indexes..." << std::endl;

    const char* tmp_file = "__test_mcdc_align_tmp.bin";

    // 1. Unaligned CSR row offsets (e.g. offset = 4097, not divisible by 128)
    {
        std::ofstream ofs(tmp_file, std::ios::binary);
        std::vector<uint8_t> buf(8192, 0x00);
        impulse_snapshot_header_t hdr{};
        hdr.magic = IMPULSE_MAGIC;
        hdr.version = IMPULSE_SPEC_VERSION_PACKED;
        hdr.data_offset = 4096;
        hdr.domain_count = 0;
        hdr.relation_count = 1;
        hdr.header_checksum = compute_test_crc16(reinterpret_cast<const uint8_t*>(&hdr), 0x3E);
        std::memcpy(buf.data(), &hdr, sizeof(hdr));

        // String table: '\0'
        uint32_t str_bytes = 1;
        std::memcpy(buf.data() + 4096, &str_bytes, 4);
        buf[4100] = '\0';

        size_t rel_cur = 4096 + 4 + str_bytes;
        size_t rem = rel_cur % 128;
        if (rem != 0) rel_cur += 128 - rem;

        impulse_relation_directory_entry_t rel{};
        rel.csr_row_off_offset = 4097; // UNALIGNED!
        rel.csr_row_off_bytes = 32;
        rel.csr_col_idx_offset = 4224; // 128 aligned
        rel.csr_col_idx_bytes = 32;
        std::memcpy(buf.data() + rel_cur, &rel, sizeof(rel));

        ofs.write(reinterpret_cast<const char*>(buf.data()), buf.size());
    }
    impulse_status_t st = IMPULSE_OK;
    impulse_snapshot_t* snap = impulse_snapshot_open(tmp_file, &st);
    ASSERT_EQ(snap, nullptr);
    ASSERT_EQ(st, IMPULSE_ERR_UNSUPPORTED_SECTION_FEATURE);

    // 2. Unaligned Index Data Offset
    {
        std::ofstream ofs(tmp_file, std::ios::binary);
        std::vector<uint8_t> buf(8192, 0x00);
        impulse_snapshot_header_t hdr{};
        hdr.magic = IMPULSE_MAGIC;
        hdr.version = IMPULSE_SPEC_VERSION_PACKED;
        hdr.data_offset = 4096;
        hdr.domain_count = 0;
        hdr.relation_count = 0;
        hdr.index_count = 1;
        hdr.header_checksum = compute_test_crc16(reinterpret_cast<const uint8_t*>(&hdr), 0x3E);
        std::memcpy(buf.data(), &hdr, sizeof(hdr));

        // String table: '\0'
        uint32_t str_bytes = 1;
        std::memcpy(buf.data() + 4096, &str_bytes, 4);
        buf[4100] = '\0';

        size_t idx_cur = 4096 + 4 + str_bytes;
        size_t rem = idx_cur % 128;
        if (rem != 0) idx_cur += 128 - rem;

        impulse_index_directory_entry_v0_9_t idx{};
        idx.data_offset = 4099; // UNALIGNED!
        idx.data_bytes = 64;
        std::memcpy(buf.data() + idx_cur, &idx, sizeof(idx));

        ofs.write(reinterpret_cast<const char*>(buf.data()), buf.size());
    }
    snap = impulse_snapshot_open(tmp_file, &st);
    ASSERT_EQ(snap, nullptr);
    ASSERT_EQ(st, IMPULSE_ERR_UNSUPPORTED_SECTION_FEATURE);

    std::remove(tmp_file);
}

// -----------------------------------------------------------------------------
// 5. Snapshot Writer Streaming & Callback Error Handling
// -----------------------------------------------------------------------------
void test_mcdc_snapshot_writer_streaming_and_callbacks() {
    std::cout << "[MC/DC Snapshot] Testing Writer Streaming Callbacks & Errors..." << std::endl;

    // 1. Writer invalid arguments
    ASSERT_EQ(impulse_writer_create(nullptr, 0), nullptr);
    ASSERT_EQ(impulse_writer_create_stream(nullptr, nullptr, 0), nullptr);
    ASSERT_EQ(impulse_writer_finalize(nullptr), IMPULSE_ERR_INVALID_ARGUMENT);

    impulse_writer_t* writer = impulse_writer_create("__test_w.bin", 0);
    ASSERT_EQ(impulse_writer_add_domain(nullptr, 0, 0, "test"), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_writer_add_domain(writer, 0, 0, nullptr), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_writer_add_relation(nullptr, 0, 0, 0, 0, 0, 0, nullptr, 0, nullptr, 0), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_writer_add_attribute(nullptr, 0, "attr", 0, 1, nullptr, 0, nullptr, 0), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_writer_add_attribute(writer, 0, nullptr, 0, 1, nullptr, 0, nullptr, 0), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_writer_add_attribute(writer, 99, "attr", 0, 1, nullptr, 0, nullptr, 0), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_writer_add_index(nullptr, 0, 0, 0, 0, "idx", nullptr, 0, 0), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_writer_add_index(writer, 0, 0, 0, 0, nullptr, nullptr, 0, 0), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_writer_set_metadata(nullptr, "k", "v"), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_writer_set_metadata(writer, nullptr, "v"), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_writer_set_metadata(writer, "k", nullptr), IMPULSE_ERR_INVALID_ARGUMENT);

    impulse_writer_destroy(writer);

    // 2. Stream callback returning error code
    auto fail_callback = [](const void*, size_t, void*) -> int {
        return -1; // Force callback failure
    };

    impulse_writer_t* stream_writer = impulse_writer_create_stream(fail_callback, nullptr, 0);
    impulse_writer_add_domain(stream_writer, 0, IMPULSE_KEY_TYPE_INT32, "test_domain");
    impulse_status_t st = impulse_writer_finalize(stream_writer);
    ASSERT_EQ(st, IMPULSE_ERR_IO_FAILURE);
    impulse_writer_destroy(stream_writer);

    // 3. Stream callback succeeding and collecting buffer in memory
    std::vector<uint8_t> stream_buffer;
    auto success_callback = [](const void* data, size_t size, void* user_data) -> int {
        auto* buf = static_cast<std::vector<uint8_t>*>(user_data);
        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        buf->insert(buf->end(), bytes, bytes + size);
        return 0;
    };

    stream_writer = impulse_writer_create_stream(success_callback, &stream_buffer, 0);
    impulse_writer_add_domain(stream_writer, 0, IMPULSE_KEY_TYPE_INT32, "stream_domain");

    uint32_t r_offs[] = { 0, 1, 2 };
    uint32_t c_idxs[] = { 1, 0 };
    impulse_writer_add_relation(stream_writer, 0, 0, IMPULSE_ENC_RAW, 2, 2, 0, r_offs, sizeof(r_offs), c_idxs, sizeof(c_idxs));
    impulse_writer_set_metadata(stream_writer, "source", "unit_test");
    st = impulse_writer_finalize(stream_writer);
    ASSERT_EQ(st, IMPULSE_OK);
    impulse_writer_destroy(stream_writer);

    ASSERT_TRUE(stream_buffer.size() >= 4096);
}

// -----------------------------------------------------------------------------
// 6. Reachability Widths, Buffer Slices & Neighborhood Sampler Edge Cases
// -----------------------------------------------------------------------------
void test_mcdc_reachability_widths_and_sampler() {
    std::cout << "[MC/DC Snapshot] Testing Multi-Width Reachability & Sampler Boundaries..." << std::endl;

    const char* snap_path = "__test_mcdc_sampler.bin";

    // Create snapshot with attributes, 16/32/64-bit relations, and metadata
    impulse_writer_t* w = impulse_writer_create(snap_path, 0);
    impulse_writer_add_domain(w, 0, IMPULSE_KEY_TYPE_INT32, "users");
    impulse_writer_add_domain(w, 1, IMPULSE_KEY_TYPE_INT32, "posts");

    uint32_t r_offs[] = { 0, 2, 3, 3, 4 };
    uint32_t c_idxs[] = { 1, 2, 0, 1 };
    impulse_writer_add_relation(w, 0, 1, IMPULSE_ENC_RAW, 4, 4, 0, r_offs, sizeof(r_offs), c_idxs, sizeof(c_idxs));

    float weights[] = { 1.5f, 2.5f, 3.5f, 4.5f };
    impulse_writer_add_attribute(w, 0, "weight", 0x08, 1, weights, sizeof(weights), nullptr, 0);
    impulse_writer_set_metadata(w, "version", "v0.9.0-test");

    impulse_status_t st = impulse_writer_finalize(w);
    ASSERT_EQ(st, IMPULSE_OK);
    impulse_writer_destroy(w);

    impulse_snapshot_t* snap = impulse_snapshot_open(snap_path, &st);
    ASSERT_EQ(st, IMPULSE_OK);
    ASSERT_TRUE(snap != nullptr);

    // 1. Reachability boundaries
    ASSERT_TRUE(impulse_snapshot_is_reachable(snap, 0, 0, 1));
    ASSERT_TRUE(impulse_snapshot_is_reachable(snap, 0, 0, 2));
    ASSERT_FALSE(impulse_snapshot_is_reachable(snap, 0, 0, 3));
    ASSERT_FALSE(impulse_snapshot_is_reachable(snap, 0, 99, 1)); // Out of bounds src
    ASSERT_FALSE(impulse_snapshot_is_reachable(snap, 99, 0, 1)); // Out of bounds relation

    // 2. Buffer slices
    const void* buf_ptr = impulse_snapshot_get_buffer(snap, 0, 4096);
    ASSERT_TRUE(buf_ptr != nullptr);
    ASSERT_EQ(impulse_snapshot_get_buffer(snap, 999999, 100), nullptr); // Out of bounds offset

    // 3. Metadata retrieval
    char meta_out[64];
    ASSERT_EQ(impulse_snapshot_get_metadata(snap, "version", meta_out, sizeof(meta_out)), IMPULSE_OK);
    ASSERT_EQ(std::string(meta_out), "version=v0.9.0-test" == std::string("version=") ? "" : "v0.9.0-test");
    ASSERT_EQ(impulse_snapshot_get_metadata(snap, "non_existent_key", meta_out, sizeof(meta_out)), IMPULSE_ERR_INVALID_ARGUMENT);

    // 4. Neighborhood Sampler Dry Run vs Sampled Run vs Overflow
    uint64_t seeds[] = { 0, 2, 3 }; // node 0 (deg 2), node 2 (deg 0), node 3 (deg 1)
    size_t total_count = 0;

    // Dry-run mode (null out_src & out_tgt)
    st = impulse_snapshot_sample_neighbors(snap, 0, seeds, 3, 1, 42, nullptr, nullptr, 0, &total_count);
    ASSERT_EQ(st, IMPULSE_OK);
    ASSERT_EQ(total_count, 2u); // 1 from node 0 + 0 from node 2 + 1 from node 3

    // Sampled run with buffer overflow (capacity = 1 < required 2)
    uint64_t out_src[4], out_tgt[4];
    st = impulse_snapshot_sample_neighbors(snap, 0, seeds, 3, 1, 42, out_src, out_tgt, 1, &total_count);
    ASSERT_EQ(st, IMPULSE_ERR_BUFFER_OVERFLOW);

    // Full sampled run with k_samples = -1 (all neighbors)
    st = impulse_snapshot_sample_neighbors(snap, 0, seeds, 3, -1, 42, out_src, out_tgt, 4, &total_count);
    ASSERT_EQ(st, IMPULSE_OK);
    ASSERT_EQ(total_count, 3u); // 2 from node 0 + 0 from node 2 + 1 from node 3

    // 5. Relation buffers & Attribute buffers
    const uint32_t* r_off_ptr = nullptr;
    const uint32_t* c_idx_ptr = nullptr;
    uint64_t n_cnt = 0, e_cnt = 0;
    st = impulse_snapshot_get_relation_buffers(snap, 0, &r_off_ptr, &c_idx_ptr, &n_cnt, &e_cnt);
    ASSERT_EQ(st, IMPULSE_OK);
    ASSERT_EQ(n_cnt, 4u);
    ASSERT_EQ(e_cnt, 4u);

    const void* attr_data = nullptr;
    uint64_t attr_bytes = 0;
    uint8_t t_code = 0;
    uint32_t dim = 0;
    st = impulse_snapshot_get_attribute_buffers(snap, 0, 0, &attr_data, &attr_bytes, nullptr, nullptr, &t_code, &dim);
    ASSERT_EQ(st, IMPULSE_OK);
    ASSERT_EQ(t_code, 0x08);
    ASSERT_EQ(dim, 1u);

    // 6. Delta layer & Compaction
    impulse_delta_layer_t* delta = impulse_delta_layer_create(0, 1, "delta_edge");
    ASSERT_TRUE(delta != nullptr);
    impulse_delta_layer_add_edge(delta, 0, 3);
    impulse_delta_layer_tombstone_edge(delta, 0, 1);
    ASSERT_TRUE(impulse_delta_layer_is_tombstoned(delta, 0, 1));
    ASSERT_FALSE(impulse_delta_layer_is_tombstoned(delta, 0, 2));

    const char* compact_file = "__test_mcdc_compact.bin";
    impulse_delta_layer_t* deltas[] = { delta };
    st = impulse_snapshot_compact_to_file(snap, deltas, 1, compact_file);
    ASSERT_EQ(st, IMPULSE_OK);

    // Test streaming compaction
    std::vector<uint8_t> compact_stream;
    auto compact_cb = [](const void* data, size_t size, void* udata) -> int {
        auto* buf = static_cast<std::vector<uint8_t>*>(udata);
        const uint8_t* p = static_cast<const uint8_t*>(data);
        buf->insert(buf->end(), p, p + size);
        return 0;
    };
    st = impulse_snapshot_compact_to_stream(snap, deltas, 1, compact_cb, &compact_stream);
    ASSERT_EQ(st, IMPULSE_OK);
    ASSERT_TRUE(compact_stream.size() >= 4096);

    impulse_delta_layer_destroy(delta);
    impulse_snapshot_close(snap);

    std::remove(snap_path);
    std::remove(compact_file);
}

// -----------------------------------------------------------------------------
// 7. Multi-Width Node/Edge Reachability & Raw Buffers
// -----------------------------------------------------------------------------
void test_mcdc_multi_width_node_edge_relations() {
    std::cout << "[MC/DC Snapshot] Testing 16-bit, 32-bit, 64-bit Node & Edge Widths..." << std::endl;

    const char* tmp_file = "__test_mcdc_multi_width.bin";
    std::ofstream ofs(tmp_file, std::ios::binary);

    std::vector<uint8_t> buf(16384, 0x00);
    impulse_snapshot_header_t hdr{};
    hdr.magic = IMPULSE_MAGIC;
    hdr.version = IMPULSE_SPEC_VERSION_PACKED;
    hdr.data_offset = 4096;
    hdr.domain_count = 1;
    hdr.relation_count = 3; // 16-bit, 32-bit, 64-bit node relations
    hdr.header_checksum = compute_test_crc16(reinterpret_cast<const uint8_t*>(&hdr), 0x3E);
    std::memcpy(buf.data(), &hdr, sizeof(hdr));

    // String table: '\0', "domain", '\0'
    const char* str_pool = "\0domain\0";
    uint32_t str_bytes = 8;
    std::memcpy(buf.data() + 4096, &str_bytes, 4);
    std::memcpy(buf.data() + 4100, str_pool, str_bytes);

    size_t cur = 4096 + 4 + str_bytes;
    size_t rem = cur % 128;
    if (rem != 0) cur += 128 - rem;

    // Domain catalog
    impulse_domain_catalog_entry_t dom{};
    dom.domain_id = 0;
    dom.name_offset = 1;
    dom.node_count = 10;
    std::memcpy(buf.data() + cur, &dom, sizeof(dom));
    cur += sizeof(dom);

    rem = cur % 128;
    if (rem != 0) cur += 128 - rem;

    // Relation 0: 16-bit node ID width (node_id_width = 2, edge_width = 4)
    // 0 -> 1, 0 -> 2
    uint32_t r0_offs[] = { 0, 2, 2 };
    uint16_t r0_tgts[] = { 1, 2 };
    uint64_t r0_off_offset = 8192;
    uint64_t r0_tgt_offset = 8320;
    std::memcpy(buf.data() + r0_off_offset, r0_offs, sizeof(r0_offs));
    std::memcpy(buf.data() + r0_tgt_offset, r0_tgts, sizeof(r0_tgts));

    impulse_relation_directory_entry_t rel0{};
    rel0.relation_id = 0;
    rel0.src_domain_id = 0;
    rel0.tgt_domain_id = 0;
    rel0.encoding_id = IMPULSE_ENC_RAW;
    rel0.node_id_width = 2; // 16-bit
    rel0.edge_index_width = 4; // 32-bit
    rel0.node_count = 2;
    rel0.edge_count = 2;
    rel0.csr_row_off_offset = r0_off_offset;
    rel0.csr_row_off_bytes = sizeof(r0_offs);
    rel0.csr_col_idx_offset = r0_tgt_offset;
    rel0.csr_col_idx_bytes = sizeof(r0_tgts);
    std::memcpy(buf.data() + cur, &rel0, sizeof(rel0));
    cur += sizeof(rel0);

    // Relation 1: 64-bit node ID width (node_id_width = 8, edge_width = 8)
    // 0 -> 5, 0 -> 9
    uint64_t r1_offs[] = { 0, 2, 2 };
    uint64_t r1_tgts[] = { 5, 9 };
    uint64_t r1_off_offset = 8448;
    uint64_t r1_tgt_offset = 8576;
    std::memcpy(buf.data() + r1_off_offset, r1_offs, sizeof(r1_offs));
    std::memcpy(buf.data() + r1_tgt_offset, r1_tgts, sizeof(r1_tgts));

    impulse_relation_directory_entry_t rel1{};
    rel1.relation_id = 1;
    rel1.src_domain_id = 0;
    rel1.tgt_domain_id = 0;
    rel1.encoding_id = IMPULSE_ENC_RAW;
    rel1.node_id_width = 8; // 64-bit
    rel1.edge_index_width = 8; // 64-bit
    rel1.node_count = 2;
    rel1.edge_count = 2;
    rel1.csr_row_off_offset = r1_off_offset;
    rel1.csr_row_off_bytes = sizeof(r1_offs);
    rel1.csr_col_idx_offset = r1_tgt_offset;
    rel1.csr_col_idx_bytes = sizeof(r1_tgts);
    std::memcpy(buf.data() + cur, &rel1, sizeof(rel1));
    cur += sizeof(rel1);

    // Relation 2: Standard 32-bit node ID width
    uint32_t r2_offs[] = { 0, 1, 1 };
    uint32_t r2_tgts[] = { 3 };
    uint64_t r2_off_offset = 8704;
    uint64_t r2_tgt_offset = 8832;
    std::memcpy(buf.data() + r2_off_offset, r2_offs, sizeof(r2_offs));
    std::memcpy(buf.data() + r2_tgt_offset, r2_tgts, sizeof(r2_tgts));

    impulse_relation_directory_entry_t rel2{};
    rel2.relation_id = 2;
    rel2.src_domain_id = 0;
    rel2.tgt_domain_id = 0;
    rel2.encoding_id = IMPULSE_ENC_RAW;
    rel2.node_id_width = 4; // 32-bit
    rel2.edge_index_width = 4; // 32-bit
    rel2.node_count = 2;
    rel2.edge_count = 1;
    rel2.csr_row_off_offset = r2_off_offset;
    rel2.csr_row_off_bytes = sizeof(r2_offs);
    rel2.csr_col_idx_offset = r2_tgt_offset;
    rel2.csr_col_idx_bytes = sizeof(r2_tgts);
    std::memcpy(buf.data() + cur, &rel2, sizeof(rel2));

    ofs.write(reinterpret_cast<const char*>(buf.data()), buf.size());
    ofs.close();

    impulse_status_t st = IMPULSE_OK;
    impulse_snapshot_t* snap = impulse_snapshot_open(tmp_file, &st);
    ASSERT_EQ(st, IMPULSE_OK);
    ASSERT_TRUE(snap != nullptr);

    // Test reachability for 16-bit node IDs
    ASSERT_TRUE(impulse_snapshot_is_reachable(snap, 0, 0, 1));
    ASSERT_TRUE(impulse_snapshot_is_reachable(snap, 0, 0, 2));
    ASSERT_FALSE(impulse_snapshot_is_reachable(snap, 0, 0, 3));

    // Test reachability for 64-bit node IDs
    ASSERT_TRUE(impulse_snapshot_is_reachable(snap, 1, 0, 5));
    ASSERT_TRUE(impulse_snapshot_is_reachable(snap, 1, 0, 9));
    ASSERT_FALSE(impulse_snapshot_is_reachable(snap, 1, 0, 1));

    // Test raw buffer getters
    const void* raw_offs = nullptr;
    const void* raw_tgts = nullptr;
    uint64_t n_cnt = 0, e_cnt = 0;
    uint8_t n_w = 0, e_w = 0;
    st = impulse_snapshot_get_relation_raw_buffers(snap, 0, &raw_offs, &raw_tgts, &n_cnt, &e_cnt, &n_w, &e_w);
    ASSERT_EQ(st, IMPULSE_OK);
    ASSERT_EQ(n_w, 2u);
    ASSERT_EQ(e_w, 4u);

    st = impulse_snapshot_get_relation_raw_buffers(snap, 1, &raw_offs, &raw_tgts, &n_cnt, &e_cnt, &n_w, &e_w);
    ASSERT_EQ(st, IMPULSE_OK);
    ASSERT_EQ(n_w, 8u);
    ASSERT_EQ(e_w, 8u);

    impulse_snapshot_close(snap);
    std::remove(tmp_file);
}

// -----------------------------------------------------------------------------
// 8. Legacy Snapshot v2.4 Format & Auxiliary Section Types
// -----------------------------------------------------------------------------
void test_mcdc_legacy_snapshot_v24() {
    std::cout << "[MC/DC Snapshot] Testing Legacy v2.4 Format & Auxiliary Sections..." << std::endl;

    const char* tmp_file = "__test_mcdc_legacy_v24.bin";
    std::ofstream ofs(tmp_file, std::ios::binary);

    std::vector<uint8_t> buf(16384, 0x00);
    uint32_t magic = IMPULSE_MAGIC;
    uint16_t ver_legacy = 0x0204;
    uint32_t data_off = 4096;
    uint16_t dom_cnt = 1;
    uint16_t rel_cnt = 1;
    std::memcpy(buf.data(), &magic, 4);
    std::memcpy(buf.data() + 4, &ver_legacy, 2);
    std::memcpy(buf.data() + 6, &data_off, 4);
    std::memcpy(buf.data() + 10, &dom_cnt, 2);
    std::memcpy(buf.data() + 12, &rel_cnt, 2);

    // Legacy domain catalog at 4096 (64 bytes)
    size_t cur = 4096;
    uint16_t dom_id = 0;
    uint8_t key_type = IMPULSE_KEY_TYPE_INT32;
    uint32_t name_off = 4096 + 64 + 128; // Name after domain & relation entries
    uint16_t name_len = 6;
    std::memcpy(buf.data() + cur, &dom_id, 2);
    buf[cur + 2] = key_type;
    std::memcpy(buf.data() + cur + 44, &name_off, 4);
    std::memcpy(buf.data() + cur + 48, &name_len, 2);
    cur += 64;

    // Legacy relation directory entry at 4160 (128 bytes)
    uint16_t src_dom = 0, tgt_dom = 0;
    uint8_t enc = IMPULSE_ENC_RAW;
    uint64_t node_c = 2, edge_c = 2;
    uint64_t csr_r_off = 8192, csr_r_bytes = 12;
    uint64_t csr_c_off = 8320, csr_c_bytes = 8;
    uint64_t aux_off = 8448, aux_bytes = 48; // 2 auxiliary entries: 0x0001 (CSC rows), 0x0002 (CSC cols)

    std::memcpy(buf.data() + cur, &src_dom, 2);
    std::memcpy(buf.data() + cur + 2, &tgt_dom, 2);
    buf[cur + 4] = enc;
    std::memcpy(buf.data() + cur + 5, &node_c, 8);
    std::memcpy(buf.data() + cur + 13, &edge_c, 8);
    std::memcpy(buf.data() + cur + 37, &csr_r_off, 8);
    std::memcpy(buf.data() + cur + 45, &csr_r_bytes, 8);
    std::memcpy(buf.data() + cur + 53, &csr_c_off, 8);
    std::memcpy(buf.data() + cur + 61, &csr_c_bytes, 8);
    std::memcpy(buf.data() + cur + 69, &aux_off, 8);
    std::memcpy(buf.data() + cur + 77, &aux_bytes, 8);
    cur += 128;

    // Write name "legacy" at name_off
    std::memcpy(buf.data() + name_off, "legacy", 6);

    // CSR buffers
    uint32_t r_offs[] = { 0, 1, 2 };
    uint32_t c_tgts[] = { 1, 0 };
    std::memcpy(buf.data() + csr_r_off, r_offs, sizeof(r_offs));
    std::memcpy(buf.data() + csr_c_off, c_tgts, sizeof(c_tgts));

    // Aux entry 1: 0x0001 CSC row offsets at 8576 (12 bytes)
    uint16_t aux1_type = 0x0001;
    uint64_t aux1_off = 8576, aux1_sz = 12;
    std::memcpy(buf.data() + aux_off, &aux1_type, 2);
    std::memcpy(buf.data() + aux_off + 8, &aux1_off, 8);
    std::memcpy(buf.data() + aux_off + 16, &aux1_sz, 8);
    std::memcpy(buf.data() + aux1_off, r_offs, sizeof(r_offs));

    // Aux entry 2: 0x0002 CSC col targets at 8704 (8 bytes)
    uint16_t aux2_type = 0x0002;
    uint64_t aux2_off = 8704, aux2_sz = 8;
    std::memcpy(buf.data() + aux_off + 24, &aux2_type, 2);
    std::memcpy(buf.data() + aux_off + 32, &aux2_off, 8);
    std::memcpy(buf.data() + aux_off + 40, &aux2_sz, 8);
    std::memcpy(buf.data() + aux2_off, c_tgts, sizeof(c_tgts));

    ofs.write(reinterpret_cast<const char*>(buf.data()), buf.size());
    ofs.close();

    impulse_status_t st = IMPULSE_OK;
    impulse_snapshot_t* snap = impulse_snapshot_open(tmp_file, &st);
    ASSERT_EQ(st, IMPULSE_OK);
    ASSERT_TRUE(snap != nullptr);

    // Check CSC buffers parsed from aux table
    const uint32_t* csc_r = nullptr;
    const uint32_t* csc_c = nullptr;
    uint64_t csc_rc = 0, csc_ec = 0;
    st = impulse_snapshot_get_relation_csc_buffers(snap, 0, &csc_r, &csc_c, &csc_rc, &csc_ec);
    ASSERT_EQ(st, IMPULSE_OK);
    ASSERT_TRUE(csc_r != nullptr);
    ASSERT_TRUE(csc_c != nullptr);

    impulse_snapshot_close(snap);
    std::remove(tmp_file);
}

// -----------------------------------------------------------------------------
// 9. Snapshot Edge Cases: Env Path Resolution, UTF-8 Depth, Key Resolution & Bad Pointers
// -----------------------------------------------------------------------------
void test_mcdc_snapshot_edge_cases_and_decisions() {
    std::cout << "[MC/DC Snapshot] Testing Env Path Resolution, UTF-8 Depth & Key Resolution..." << std::endl;

    // 1. Path Resolution with Env Vars
    const char* env_dir_path = "/tmp/__impulse_env_test_dir";
    ::mkdir(env_dir_path, 0777);
    const char* env_file = "/tmp/__impulse_env_test_dir/env_snap.bin";

    // Write a valid snapshot in env_dir_path
    {
        std::ofstream ofs(env_file, std::ios::binary);
        std::vector<uint8_t> buf(8192, 0x00);
        impulse_snapshot_header_t hdr{};
        hdr.magic = IMPULSE_MAGIC;
        hdr.version = IMPULSE_SPEC_VERSION_PACKED;
        hdr.data_offset = 4096;
        hdr.header_checksum = compute_test_crc16(reinterpret_cast<const uint8_t*>(&hdr), 0x3E);
        std::memcpy(buf.data(), &hdr, sizeof(hdr));

        uint32_t str_bytes = 1;
        std::memcpy(buf.data() + 4096, &str_bytes, 4);
        buf[4100] = '\0';

        ofs.write(reinterpret_cast<const char*>(buf.data()), buf.size());
    }

    // Set IMPULSE_DATASETS_DIR
    ::setenv("IMPULSE_DATASETS_DIR", env_dir_path, 1);
    impulse_status_t st = IMPULSE_OK;
    impulse_snapshot_t* snap = impulse_snapshot_open("env_snap.bin", &st);
    ASSERT_EQ(st, IMPULSE_OK);
    ASSERT_TRUE(snap != nullptr);
    impulse_snapshot_close(snap);

    // Set IMPULSEGRAPH_DATA_DIR
    ::unsetenv("IMPULSE_DATASETS_DIR");
    ::setenv("IMPULSEGRAPH_DATA_DIR", "/tmp/__impulse_env_test_dir/", 1); // with trailing slash
    snap = impulse_snapshot_open("env_snap.bin", &st);
    ASSERT_EQ(st, IMPULSE_OK);
    ASSERT_TRUE(snap != nullptr);
    impulse_snapshot_close(snap);

    // Set IMPULSE_DATA_DIR
    ::unsetenv("IMPULSEGRAPH_DATA_DIR");
    ::setenv("IMPULSE_DATA_DIR", env_dir_path, 1);
    snap = impulse_snapshot_open("env_snap.bin", &st);
    ASSERT_EQ(st, IMPULSE_OK);
    ASSERT_TRUE(snap != nullptr);
    impulse_snapshot_close(snap);
    ::unsetenv("IMPULSE_DATA_DIR");

    std::remove(env_file);
    ::rmdir(env_dir_path);

    // 2. UTF-8 multi-byte valid & invalid sequences in String Table
    const char* utf8_tmp = "__test_mcdc_utf8_all.bin";
    {
        std::ofstream ofs(utf8_tmp, std::ios::binary);
        std::vector<uint8_t> buf(8192, 0x00);
        impulse_snapshot_header_t hdr{};
        hdr.magic = IMPULSE_MAGIC;
        hdr.version = IMPULSE_SPEC_VERSION_PACKED;
        hdr.data_offset = 4096;
        hdr.domain_count = 3; // 2-byte, 3-byte, 4-byte UTF-8
        hdr.header_checksum = compute_test_crc16(reinterpret_cast<const uint8_t*>(&hdr), 0x3E);
        std::memcpy(buf.data(), &hdr, sizeof(hdr));

        // String pool:
        // offset 0: '\0'
        // offset 1: "\xC3\xA9\0" (2-byte 'é')
        // offset 4: "\xE2\x9C\x93\0" (3-byte '✓')
        // offset 8: "\xF0\x9F\x9A\x80\0" (4-byte '🚀')
        uint8_t pool[] = {
            '\0',
            0xC3, 0xA9, '\0',
            0xE2, 0x9C, 0x93, '\0',
            0xF0, 0x9F, 0x9A, 0x80, '\0'
        };
        uint32_t str_bytes = sizeof(pool);
        std::memcpy(buf.data() + 4096, &str_bytes, 4);
        std::memcpy(buf.data() + 4100, pool, sizeof(pool));

        size_t cur = 4096 + 4 + str_bytes;
        size_t rem = cur % 128;
        if (rem != 0) cur += 128 - rem;

        impulse_domain_catalog_entry_t d0{0, 1, 0, 1, 0};
        impulse_domain_catalog_entry_t d1{1, 1, 0, 4, 0};
        impulse_domain_catalog_entry_t d2{2, 1, 0, 8, 0};
        std::memcpy(buf.data() + cur, &d0, sizeof(d0));
        std::memcpy(buf.data() + cur + sizeof(d0), &d1, sizeof(d1));
        std::memcpy(buf.data() + cur + sizeof(d0) * 2, &d2, sizeof(d2));

        ofs.write(reinterpret_cast<const char*>(buf.data()), buf.size());
    }
    snap = impulse_snapshot_open(utf8_tmp, &st);
    ASSERT_EQ(st, IMPULSE_OK);
    ASSERT_TRUE(snap != nullptr);

    const char* dname0 = nullptr;
    const char* dname1 = nullptr;
    const char* dname2 = nullptr;
    impulse_domain_catalog_entry_t d_ent{};
    impulse_snapshot_get_domain_entry(snap, 0, &d_ent, &dname0);
    impulse_snapshot_get_domain_entry(snap, 1, &d_ent, &dname1);
    impulse_snapshot_get_domain_entry(snap, 2, &d_ent, &dname2);
    ASSERT_TRUE(dname0 != nullptr);
    ASSERT_TRUE(dname1 != nullptr);
    ASSERT_TRUE(dname2 != nullptr);
    impulse_snapshot_close(snap);
    std::remove(utf8_tmp);

    // 3. Null Pointer & Missing Buffer Accessors
    snap = impulse_snapshot_open("__impulse_test_snapshot_v09.bin", &st);
    if (!snap) {
        // Create small snapshot if not present
        impulse_writer_t* w = impulse_writer_create("__impulse_test_snapshot_v09.bin", 0);
        impulse_writer_add_domain(w, 0, 3, "nodes");
        uint32_t r_offs[] = { 0, 1, 2 };
        uint32_t c_idxs[] = { 1, 0 };
        impulse_writer_add_relation(w, 0, 0, 0, 2, 2, 0, r_offs, sizeof(r_offs), c_idxs, sizeof(c_idxs));
        impulse_writer_finalize(w);
        impulse_writer_destroy(w);
        snap = impulse_snapshot_open("__impulse_test_snapshot_v09.bin", &st);
    }
    ASSERT_TRUE(snap != nullptr);

    // Test accessors with null output pointers (checking early returns)
    ASSERT_EQ(impulse_snapshot_get_domain_entry(snap, 0, nullptr, nullptr), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_get_domain_entry(snap, 99, &d_ent, nullptr), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_get_relation_entry(snap, 0, nullptr), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_get_relation_entry(snap, 99, nullptr), IMPULSE_ERR_INVALID_ARGUMENT);

    const uint32_t* o_ptr = nullptr;
    const uint32_t* t_ptr = nullptr;
    ASSERT_EQ(impulse_snapshot_get_relation_buffers(snap, 99, &o_ptr, &t_ptr, nullptr, nullptr), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_get_relation_buffers(snap, 0, nullptr, &t_ptr, nullptr, nullptr), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_get_relation_buffers(snap, 0, &o_ptr, nullptr, nullptr, nullptr), IMPULSE_ERR_INVALID_ARGUMENT);

    const void* ro_ptr = nullptr;
    const void* rt_ptr = nullptr;
    ASSERT_EQ(impulse_snapshot_get_relation_raw_buffers(snap, 99, &ro_ptr, &rt_ptr, nullptr, nullptr, nullptr, nullptr), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_get_relation_raw_buffers(snap, 0, nullptr, &rt_ptr, nullptr, nullptr, nullptr, nullptr), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_get_relation_raw_buffers(snap, 0, &ro_ptr, nullptr, nullptr, nullptr, nullptr, nullptr), IMPULSE_ERR_INVALID_ARGUMENT);

    ASSERT_EQ(impulse_snapshot_get_attribute_buffers(snap, 99, 0, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_get_attribute_buffers(snap, 0, 99, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr), IMPULSE_ERR_INVALID_ARGUMENT);

    ASSERT_EQ(impulse_snapshot_get_relation_csc_buffers(snap, 99, &o_ptr, &t_ptr, nullptr, nullptr), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_get_relation_csc_buffers(snap, 0, nullptr, &t_ptr, nullptr, nullptr), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_get_relation_csc_buffers(snap, 0, &o_ptr, nullptr, nullptr, nullptr), IMPULSE_ERR_INVALID_ARGUMENT);

    ASSERT_EQ(impulse_snapshot_get_relation_csc_raw_buffers(snap, 99, &ro_ptr, &rt_ptr, nullptr, nullptr, nullptr, nullptr), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_get_relation_csc_raw_buffers(snap, 0, nullptr, &rt_ptr, nullptr, nullptr, nullptr, nullptr), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_get_relation_csc_raw_buffers(snap, 0, &ro_ptr, nullptr, nullptr, nullptr, nullptr, nullptr), IMPULSE_ERR_INVALID_ARGUMENT);

    // Key resolution guards
    uint32_t out_nid = 0;
    ASSERT_EQ(impulse_snapshot_resolve_key(nullptr, 0, "key", 3, &out_nid), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_resolve_key(snap, 0, nullptr, 3, &out_nid), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_resolve_key(snap, 0, "key", 3, nullptr), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_resolve_key(snap, 99, "key", 3, &out_nid), IMPULSE_ERR_INVALID_ARGUMENT);

    const void* out_kbytes = nullptr;
    size_t out_klen = 0;
    ASSERT_EQ(impulse_snapshot_resolve_dense_id(nullptr, 0, 0, &out_kbytes, &out_klen), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_resolve_dense_id(snap, 0, 0, nullptr, &out_klen), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_resolve_dense_id(snap, 0, 0, &out_kbytes, nullptr), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_resolve_dense_id(snap, 99, 0, &out_kbytes, &out_klen), IMPULSE_ERR_INVALID_ARGUMENT);

    // Sampler null argument permutations
    uint64_t s_nodes[] = { 0 };
    size_t s_count = 0;
    ASSERT_EQ(impulse_snapshot_sample_neighbors(nullptr, 0, s_nodes, 1, 1, 0, nullptr, nullptr, 0, &s_count), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_sample_neighbors(snap, 0, nullptr, 1, 1, 0, nullptr, nullptr, 0, &s_count), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_sample_neighbors(snap, 0, s_nodes, 1, 1, 0, nullptr, nullptr, 0, nullptr), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_sample_neighbors(snap, 99, s_nodes, 1, 1, 0, nullptr, nullptr, 0, &s_count), IMPULSE_ERR_INVALID_ARGUMENT);

    // Compaction null arguments
    ASSERT_EQ(impulse_snapshot_compact_to_file(nullptr, nullptr, 0, "out.bin"), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_compact_to_file(snap, nullptr, 0, nullptr), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_compact_to_stream(nullptr, nullptr, 0, nullptr, nullptr), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_compact_to_stream(snap, nullptr, 0, nullptr, nullptr), IMPULSE_ERR_INVALID_ARGUMENT);

    impulse_snapshot_close(snap);
}






void test_mcdc_snapshot_pass8_deep_decisions() {
    std::cout << "[MC/DC Snapshot] Testing Snapshot Pass 8 Deep Decisions & Boundary Permutations..." << std::endl;

    // 1. resolve_snapshot_path variations
    setenv("IMPULSE_DATASETS_DIR", "", 1);
    setenv("IMPULSEGRAPH_DATA_DIR", "/tmp/impulse_test_dir", 1);
    setenv("IMPULSE_DATA_DIR", "/tmp/impulse_data_dir", 1);
    impulse_status_t st;
    impulse_snapshot_t* snap_env = impulse_snapshot_open("nonexistent_test.imps", &st);
    ASSERT_EQ(snap_env, nullptr);

    unsetenv("IMPULSEGRAPH_DATA_DIR");
    snap_env = impulse_snapshot_open("nonexistent_test.imps", &st);
    ASSERT_EQ(snap_env, nullptr);
    unsetenv("IMPULSE_DATA_DIR");

    // 2. String Table Multi-byte UTF-8 boundary permutations
    std::string test_utf8_file = "/tmp/test_mcdc_utf8_perm.imps";
    {
        std::vector<uint8_t> buf(8192, 0);
        impulse_snapshot_header_v0_9_t hdr{};
        hdr.magic = IMPULSE_MAGIC;
        hdr.version = IMPULSE_SPEC_VERSION_PACKED;
                hdr.data_offset = 4096;
        hdr.domain_count = 1;
        hdr.relation_count = 0;
        hdr.header_checksum = compute_test_crc16(reinterpret_cast<const uint8_t*>(&hdr), 62);
        std::memcpy(buf.data(), &hdr, sizeof(hdr));

        uint8_t str_data[] = { 0, (uint8_t)0xC0, 0x00, (uint8_t)0xE0, (uint8_t)0x80, 0x00, (uint8_t)0xF0, (uint8_t)0x80, (uint8_t)0x80, 0x00 };
        uint32_t str_len = sizeof(str_data);
        std::memcpy(buf.data() + 4096, &str_len, 4);
        std::memcpy(buf.data() + 4100, str_data, sizeof(str_data));

        size_t dom_cur = 4096 + 4 + str_len;
        size_t rem = dom_cur % 128;
        if (rem != 0) dom_cur += (128 - rem);

        impulse_domain_catalog_entry_v0_9_t dom{};
        dom.domain_id = 0;
        dom.name_offset = 1; // points to 0xC0 0x00
        std::memcpy(buf.data() + dom_cur, &dom, sizeof(dom));

        std::ofstream f(test_utf8_file, std::ios::binary);
        f.write(reinterpret_cast<const char*>(buf.data()), buf.size());
    }
    impulse_snapshot_t* snap_utf8 = impulse_snapshot_open(test_utf8_file.c_str(), &st);
    ASSERT_EQ(snap_utf8, nullptr);
    ASSERT_EQ(st, IMPULSE_ERR_INVALID_ARGUMENT);
    std::remove(test_utf8_file.c_str());

    // 3. Unaligned index data offset (must be 128B aligned)
    std::string test_idx_file = "/tmp/test_mcdc_unaligned_idx.imps";
    {
        std::vector<uint8_t> buf(8192, 0);
        impulse_snapshot_header_v0_9_t hdr{};
        hdr.magic = IMPULSE_MAGIC;
        hdr.version = IMPULSE_SPEC_VERSION_PACKED;
                hdr.data_offset = 4096;
        hdr.domain_count = 0;
        hdr.relation_count = 0;
        hdr.index_count = 1;
        hdr.header_checksum = compute_test_crc16(reinterpret_cast<const uint8_t*>(&hdr), 62);
        std::memcpy(buf.data(), &hdr, sizeof(hdr));

        uint32_t str_len = 1;
        std::memcpy(buf.data() + 4096, &str_len, 4);
        buf[4100] = 0;

        impulse_index_directory_entry_v0_9_t idx{};
        idx.domain_id = 0;
        idx.index_type = 1;
        idx.data_offset = 5001; // unaligned
        idx.data_bytes = 100;
        std::memcpy(buf.data() + 4224, &idx, sizeof(idx));

        std::ofstream f(test_idx_file, std::ios::binary);
        f.write(reinterpret_cast<const char*>(buf.data()), buf.size());
    }
    impulse_snapshot_t* snap_idx = impulse_snapshot_open(test_idx_file.c_str(), &st);
    ASSERT_EQ(snap_idx, nullptr);
    ASSERT_EQ(st, IMPULSE_ERR_UNSUPPORTED_SECTION_FEATURE);
    std::remove(test_idx_file.c_str());

    // 4. Create a valid complete snapshot using the C-ABI Writer
    std::string test_valid_file = "/tmp/test_mcdc_valid_full.imps";
    {
        impulse_writer_t* w = impulse_writer_create(test_valid_file.c_str(), 0);
        ASSERT_TRUE(w != nullptr);

        ASSERT_EQ(impulse_writer_add_domain(w, 0, 0, "User"), IMPULSE_OK);

        uint32_t row_off[] = { 0, 1, 2, 3 };
        uint16_t col_idx[] = { 1, 2, 0 };
        ASSERT_EQ(impulse_writer_add_relation(w, 0, 0, 0, 3, 3, 0, row_off, sizeof(row_off), col_idx, sizeof(col_idx)), IMPULSE_OK);

        float weights[] = { 1.0f, 2.0f, 3.0f };
        ASSERT_EQ(impulse_writer_add_attribute(w, 0, "weight", 2, 1, weights, sizeof(weights), nullptr, 0), IMPULSE_OK);

        uint32_t idx_data[] = { 0, 0,  1, 1,  2, 2 };
        ASSERT_EQ(impulse_writer_add_index(w, 0, 0xFFFF, 0, 4, "mph_idx", idx_data, sizeof(idx_data), 0), IMPULSE_OK);

        ASSERT_EQ(impulse_writer_set_metadata(w, "author", "impulse"), IMPULSE_OK);

        ASSERT_EQ(impulse_writer_finalize(w), IMPULSE_OK);
        impulse_writer_destroy(w);
    }

    impulse_snapshot_t* snap = impulse_snapshot_open(test_valid_file.c_str(), &st);
    ASSERT_TRUE(snap != nullptr);
    ASSERT_EQ(st, IMPULSE_OK);

    // 5. Test raw pointer accessor
    ASSERT_EQ(impulse_snapshot_get_buffer(nullptr, 0, 10), nullptr);
    ASSERT_EQ(impulse_snapshot_get_buffer(snap, 99999999, 10), nullptr);
    ASSERT_EQ(impulse_snapshot_get_buffer(snap, 100, 99999999), nullptr);
    ASSERT_TRUE(impulse_snapshot_get_buffer(snap, 0, 64) != nullptr);

    // 6. Test metadata accessor
    char meta_buf[64];
    ASSERT_EQ(impulse_snapshot_get_metadata(nullptr, "author", meta_buf, sizeof(meta_buf)), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_get_metadata(snap, nullptr, meta_buf, sizeof(meta_buf)), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_get_metadata(snap, "author", nullptr, sizeof(meta_buf)), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_get_metadata(snap, "author", meta_buf, 0), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_get_metadata(snap, "nonexistent", meta_buf, sizeof(meta_buf)), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_get_metadata(snap, "author", meta_buf, sizeof(meta_buf)), IMPULSE_OK);
    ASSERT_EQ(std::string(meta_buf), "impulse");

    // 7. Test relation CSR & CSC accessor permutation branches
    const uint32_t* u32_offsets = nullptr;
    const uint32_t* u32_targets = nullptr;
    const void* raw_offsets = nullptr;
    const void* raw_targets = nullptr;
    uint64_t node_cnt = 0;
    uint64_t edge_cnt = 0;
    uint8_t n_w = 0;
    uint8_t e_w = 0;

    ASSERT_EQ(impulse_snapshot_get_relation_buffers(nullptr, 0, &u32_offsets, &u32_targets, &node_cnt, &edge_cnt), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_get_relation_buffers(snap, 99, &u32_offsets, &u32_targets, &node_cnt, &edge_cnt), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_get_relation_buffers(snap, 0, nullptr, &u32_targets, &node_cnt, &edge_cnt), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_get_relation_buffers(snap, 0, &u32_offsets, nullptr, &node_cnt, &edge_cnt), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_get_relation_buffers(snap, 0, &u32_offsets, &u32_targets, nullptr, nullptr), IMPULSE_OK);

    ASSERT_EQ(impulse_snapshot_get_relation_raw_buffers(nullptr, 0, &raw_offsets, &raw_targets, &node_cnt, &edge_cnt, &n_w, &e_w), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_get_relation_raw_buffers(snap, 99, &raw_offsets, &raw_targets, &node_cnt, &edge_cnt, &n_w, &e_w), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_get_relation_raw_buffers(snap, 0, nullptr, &raw_targets, &node_cnt, &edge_cnt, &n_w, &e_w), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_get_relation_raw_buffers(snap, 0, &raw_offsets, nullptr, &node_cnt, &edge_cnt, &n_w, &e_w), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_get_relation_raw_buffers(snap, 0, &raw_offsets, &raw_targets, nullptr, nullptr, &n_w, &e_w), IMPULSE_OK);

    ASSERT_EQ(impulse_snapshot_get_relation_csc_buffers(nullptr, 0, &u32_offsets, &u32_targets, &node_cnt, &edge_cnt), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_get_relation_csc_buffers(snap, 99, &u32_offsets, &u32_targets, &node_cnt, &edge_cnt), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_get_relation_csc_buffers(snap, 0, nullptr, &u32_targets, &node_cnt, &edge_cnt), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_get_relation_csc_buffers(snap, 0, &u32_offsets, nullptr, &node_cnt, &edge_cnt), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_get_relation_csc_buffers(snap, 0, &u32_offsets, &u32_targets, nullptr, nullptr), IMPULSE_OK);

    ASSERT_EQ(impulse_snapshot_get_relation_csc_raw_buffers(nullptr, 0, &raw_offsets, &raw_targets, &node_cnt, &edge_cnt, &n_w, &e_w), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_get_relation_csc_raw_buffers(snap, 99, &raw_offsets, &raw_targets, &node_cnt, &edge_cnt, &n_w, &e_w), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_get_relation_csc_raw_buffers(snap, 0, nullptr, &raw_targets, &node_cnt, &edge_cnt, &n_w, &e_w), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_get_relation_csc_raw_buffers(snap, 0, &raw_offsets, nullptr, &node_cnt, &edge_cnt, &n_w, &e_w), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_get_relation_csc_raw_buffers(snap, 0, &raw_offsets, &raw_targets, nullptr, nullptr, &n_w, &e_w), IMPULSE_OK);

    // 8. Test relation attributes accessor
    const void* attr_data_ptr = nullptr;
    uint64_t attr_data_bytes = 0;
    const void* attr_off_ptr = nullptr;
    uint64_t attr_off_bytes = 0;
    uint8_t attr_type = 0;
    uint32_t attr_dim = 0;
    ASSERT_EQ(impulse_snapshot_get_attribute_buffers(nullptr, 0, 0, &attr_data_ptr, &attr_data_bytes, &attr_off_ptr, &attr_off_bytes, &attr_type, &attr_dim), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_get_attribute_buffers(snap, 99, 0, &attr_data_ptr, &attr_data_bytes, &attr_off_ptr, &attr_off_bytes, &attr_type, &attr_dim), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_get_attribute_buffers(snap, 0, 99, &attr_data_ptr, &attr_data_bytes, &attr_off_ptr, &attr_off_bytes, &attr_type, &attr_dim), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_get_attribute_buffers(snap, 0, 0, &attr_data_ptr, &attr_data_bytes, &attr_off_ptr, &attr_off_bytes, &attr_type, &attr_dim), IMPULSE_OK);
    ASSERT_EQ(attr_type, 2); // float
    ASSERT_EQ(attr_dim, 1);

    // 9. Test index entry accessor
    uint32_t idx_id = 0;
    uint16_t d_id = 0;
    uint16_t r_id = 0;
    uint16_t a_idx = 0;
    uint8_t i_type = 0;
    const char* i_name = nullptr;
    const void* i_data = nullptr;
    uint64_t i_bytes = 0;
    ASSERT_EQ(impulse_snapshot_get_index(nullptr, 0, &idx_id, &d_id, &r_id, &a_idx, &i_type, &i_name, &i_data, &i_bytes), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_get_index(snap, 99, &idx_id, &d_id, &r_id, &a_idx, &i_type, &i_name, &i_data, &i_bytes), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_get_index(snap, 0, &idx_id, &d_id, &r_id, &a_idx, &i_type, &i_name, &i_data, &i_bytes), IMPULSE_ERR_INVALID_ARGUMENT);
    // checked

    // 10. Secondary index node lookup by key and reverse lookup
    uint32_t found_node = 0;
    ASSERT_EQ(impulse_snapshot_resolve_key(nullptr, 0, "User", 4, &found_node), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_resolve_key(snap, 99, "User", 4, &found_node), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_resolve_key(snap, 0, nullptr, 4, &found_node), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_resolve_key(snap, 0, "User", 4, nullptr), IMPULSE_ERR_INVALID_ARGUMENT);
    impulse_snapshot_resolve_key(snap, 0, "User", 4, &found_node);

    const void* out_k_bytes = nullptr;
    size_t out_k_len = 0;
    ASSERT_EQ(impulse_snapshot_resolve_dense_id(nullptr, 0, 0, &out_k_bytes, &out_k_len), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_resolve_dense_id(snap, 99, 0, &out_k_bytes, &out_k_len), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_resolve_dense_id(snap, 0, 0, nullptr, &out_k_len), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_snapshot_resolve_dense_id(snap, 0, 0, &out_k_bytes, nullptr), IMPULSE_ERR_INVALID_ARGUMENT);
    impulse_snapshot_resolve_dense_id(snap, 0, 0, &out_k_bytes, &out_k_len);
    impulse_snapshot_resolve_dense_id(snap, 0, 999, &out_k_bytes, &out_k_len);

    // 11. Sampler deterministic vs randomized branch execution
    uint64_t src_nodes[] = { 0, 1 };
    uint64_t sampled_src[10];
    uint64_t sampled_tgt[10];
    size_t out_sampled = 0;

    ASSERT_EQ(impulse_snapshot_sample_neighbors(snap, 0, src_nodes, 2, -1, 42, sampled_src, sampled_tgt, 10, &out_sampled), IMPULSE_OK);
    ASSERT_EQ(impulse_snapshot_sample_neighbors(snap, 0, src_nodes, 2, 1, 42, sampled_src, sampled_tgt, 10, &out_sampled), IMPULSE_OK);
    ASSERT_EQ(impulse_snapshot_sample_neighbors(snap, 0, src_nodes, 2, 1, 42, nullptr, nullptr, 0, &out_sampled), IMPULSE_OK);

    // 12. Writer null argument permutations
    uint32_t row_off[] = { 0, 1 };
    uint16_t col_idx[] = { 0 };
    impulse_writer_t* w_dummy = impulse_writer_create("/tmp/test_dummy_w.bin", 0);
    ASSERT_EQ(impulse_writer_add_relation(nullptr, 0, 0, 2, 1, 1, 0, row_off, sizeof(row_off), col_idx, sizeof(col_idx)), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_writer_add_relation(w_dummy, 0, 0, 2, 1, 1, 0, nullptr, sizeof(row_off), col_idx, sizeof(col_idx)), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_writer_add_relation(w_dummy, 0, 0, 2, 1, 1, 0, row_off, sizeof(row_off), nullptr, sizeof(col_idx)), IMPULSE_ERR_INVALID_ARGUMENT);

    ASSERT_EQ(impulse_writer_add_index(nullptr, 0, 0, 0, 0, "idx", nullptr, 0, 0), IMPULSE_ERR_INVALID_ARGUMENT);
    ASSERT_EQ(impulse_writer_add_index(w_dummy, 0, 0, 0, 0, nullptr, nullptr, 0, 0), IMPULSE_ERR_INVALID_ARGUMENT);
    impulse_writer_destroy(w_dummy);
    std::remove("/tmp/test_dummy_w.bin");

    // 13. Snapshot close null check
    impulse_snapshot_close(nullptr);
    impulse_snapshot_close(snap);
    std::remove(test_valid_file.c_str());
}

int main() {
    std::cout << "================================================================" << std::endl;
    std::cout << " Impulse Snapshot MC/DC Condition Independence & Boundary Suite" << std::endl;
    std::cout << "================================================================" << std::endl;

    test_mcdc_snapshot_path_and_null_args();
    test_mcdc_snapshot_header_validation();
    test_mcdc_string_table_and_utf8();
    test_mcdc_section_alignment_and_indexes();
    test_mcdc_snapshot_writer_streaming_and_callbacks();
    test_mcdc_reachability_widths_and_sampler();
    test_mcdc_multi_width_node_edge_relations();
    test_mcdc_legacy_snapshot_v24();
    test_mcdc_snapshot_edge_cases_and_decisions();
    test_mcdc_snapshot_pass8_deep_decisions();

    std::cout << "================================================================" << std::endl;
    std::cout << " ALL SNAPSHOT MC/DC CONDITION INDEPENDENCE TESTS PASSED!" << std::endl;
    std::cout << "================================================================" << std::endl;
    return 0;
}
