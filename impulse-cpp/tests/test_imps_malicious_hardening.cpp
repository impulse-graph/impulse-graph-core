/**
 * @file test_imps_malicious_hardening.cpp
 * @brief Adversarial & Malformed .imps Binary Snapshot Hardening Suite (SQLite-style).
 *
 * Validates that untrusted, truncated, overflowed, cyclic, unaligned, and byte-fuzzed
 * .imps snapshots and secondary index payloads fail gracefully with well-defined
 * impulse_status_t / impulse_vm_status_t error codes without crashes, OOB reads,
 * integer wrap-arounds, or unhandled C++ exceptions across extern "C" boundaries.
 */

#include "impulse_graph.h"
#include "impulse_index.h"
#include "impulse_vm.h"
#include "impulse_assert.h"

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <unistd.h>
#include <vector>

#define HARDEN_CHECK(cond, msg)                                                                   \
    do {                                                                                          \
        if (!(cond)) {                                                                            \
            std::cerr << "[FAIL] " << __FILE__ << ":" << __LINE__ << " — " << (msg) << std::endl; \
            std::exit(1);                                                                         \
        }                                                                                         \
    } while (0)

namespace {

uint16_t crc16_ccitt(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFFu;
    for (size_t i = 0; i < len; ++i) {
        crc ^= static_cast<uint16_t>(data[i]) << 8;
        for (int b = 0; b < 8; ++b) {
            if (crc & 0x8000u) {
                crc = static_cast<uint16_t>((crc << 1) ^ 0x1021u);
            } else {
                crc = static_cast<uint16_t>(crc << 1);
            }
        }
    }
    return crc;
}

void stamp_crc16(std::vector<uint8_t>& buf) {
    HARDEN_CHECK(buf.size() >= 64, "Buffer too small for header CRC16");
    uint16_t crc = crc16_ccitt(buf.data(), 0x3Eu);
    std::memcpy(buf.data() + 0x3Eu, &crc, sizeof(uint16_t));
}

void align_vec(std::vector<uint8_t>& buf, size_t align) {
    size_t rem = buf.size() % align;
    if (rem != 0) {
        buf.resize(buf.size() + (align - rem), 0u);
    }
}

std::string temp_imps_path(const char* tag) {
    return std::string("/tmp/impulse_malicious_") + tag + "_" + std::to_string(::getpid()) + ".imps";
}

void write_bytes(const std::string& path, const std::vector<uint8_t>& bytes) {
    std::ofstream ofs(path, std::ios::binary | std::ios::trunc);
    HARDEN_CHECK(ofs.good(), "Failed to open temp file for write");
    if (!bytes.empty()) {
        ofs.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    ofs.close();
}

struct ScopedTempFile {
    std::string path;
    explicit ScopedTempFile(const char* tag, const std::vector<uint8_t>& bytes) : path(temp_imps_path(tag)) {
        write_bytes(path, bytes);
    }
    ~ScopedTempFile() { ::unlink(path.c_str()); }
};

/**
 * Builds a clean, valid Spec v0.10.0 multi-section .imps snapshot in memory:
 *  - 1 domain ("Bus", 4 nodes, primary key VarString attr 0, nullable Float32 attr 1)
 *  - 1 CSR/CSC relation ("Line", 4 nodes, 4 edges, Float32 weight attr 0)
 *  - Footer trailer with 1 metadata KV pair ("generator" -> "hardening_suite")
 */
std::vector<uint8_t> build_valid_v10_snapshot() {
    std::vector<uint8_t> buf(4096, 0u);

    // String pool: "\0Bus\0id\0vm_pu\0Line\0r_ohm\0"
    const char kPool[] = "\0Bus\0id\0vm_pu\0Line\0r_ohm\0";
    const uint32_t pool_len = sizeof(kPool) - 1u; // includes trailing '\0' of "r_ohm\0"
    const uint32_t off_bus   = 1u;
    const uint32_t off_id    = 5u;
    const uint32_t off_vmpu  = 8u;
    const uint32_t off_line  = 14u;
    const uint32_t off_rohm  = 19u;

    // Layout data sections starting at 8192 (4KB page aligned & 128B aligned)
    std::vector<uint8_t> data_slab;
    auto alloc_section = [&](const void* src, size_t len) -> std::pair<uint64_t, uint64_t> {
        align_vec(data_slab, 128);
        uint64_t off = 8192ULL + data_slab.size();
        const auto* p = static_cast<const uint8_t*>(src);
        data_slab.insert(data_slab.end(), p, p + len);
        return {off, static_cast<uint64_t>(len)};
    };

    // Domain 0 Attr 0: VarString primary key ("n0\0", "n1\0", "n2\0", "n3\0")
    const char pk_chars[] = "n0\0n1\0n2\0n3\0";
    const uint32_t pk_offs[5] = {0u, 3u, 6u, 9u, 12u};
    auto [pk_data_off, pk_data_bytes] = alloc_section(pk_chars, 12u);
    auto [pk_offs_off, pk_offs_bytes] = alloc_section(pk_offs, sizeof(pk_offs));

    // Domain 0 Attr 1: Nullable Float32 (128B validity bitmap + 4 floats)
    std::vector<uint8_t> vmpu_slab(128 + 16, 0u);
    vmpu_slab[0] = 0x0BDu & 0x0Fu; // bits 0, 2, 3 valid; node 1 NULL
    vmpu_slab[0] = 0b00001101u;
    const float vmpu_vals[4] = {1.02f, 0.0f, 0.99f, 1.05f};
    std::memcpy(vmpu_slab.data() + 128, vmpu_vals, sizeof(vmpu_vals));
    auto [vmpu_off, vmpu_bytes] = alloc_section(vmpu_slab.data(), vmpu_slab.size());

    // Relation 0 CSR & CSC: 0->1, 1->2, 2->3, 3->0
    const uint32_t csr_row[5] = {0u, 1u, 2u, 3u, 4u};
    const uint32_t csr_col[4] = {1u, 2u, 3u, 0u};
    const uint32_t csc_row[5] = {0u, 1u, 2u, 3u, 4u};
    const uint32_t csc_col[4] = {3u, 0u, 1u, 2u};
    const float edge_w[4]     = {0.01f, 0.02f, 0.03f, 0.04f};
    auto [csr_r_off, csr_r_bytes] = alloc_section(csr_row, sizeof(csr_row));
    auto [csr_c_off, csr_c_bytes] = alloc_section(csr_col, sizeof(csr_col));
    auto [csc_r_off, csc_r_bytes] = alloc_section(csc_row, sizeof(csc_row));
    auto [csc_c_off, csc_c_bytes] = alloc_section(csc_col, sizeof(csc_col));
    auto [ew_off, ew_bytes]       = alloc_section(edge_w, sizeof(edge_w));

    // Build directory at offset 4096
    std::vector<uint8_t> dir;
    dir.resize(4);
    std::memcpy(dir.data(), &pool_len, 4);
    dir.insert(dir.end(), reinterpret_cast<const uint8_t*>(kPool), reinterpret_cast<const uint8_t*>(kPool) + pool_len);
    align_vec(dir, 128);

    impulse_domain_catalog_entry_v0_10_t dom{};
    dom.domain_id = 0;
    dom.key_type = 1;
    dom.name_offset = off_bus;
    dom.node_count = 4;
    dom.attr_count = 2;
    dom.key_attr_index = 0;
    size_t pos = dir.size();
    dir.resize(pos + sizeof(dom));
    std::memcpy(dir.data() + pos, &dom, sizeof(dom));

    impulse_attribute_descriptor_t dattr0{};
    dattr0.name_offset = off_id;
    dattr0.type_code = 0x0Bu; // VarString
    dattr0.dimension = 1u;
    dattr0.data_offset = pk_data_off;
    dattr0.data_bytes = pk_data_bytes;
    dattr0.offsets_offset = pk_offs_off;
    dattr0.offsets_bytes = pk_offs_bytes;
    pos = dir.size();
    dir.resize(pos + sizeof(dattr0));
    std::memcpy(dir.data() + pos, &dattr0, sizeof(dattr0));

    impulse_attribute_descriptor_t dattr1{};
    dattr1.name_offset = off_vmpu;
    dattr1.type_code = 0x86u; // Nullable Float32
    dattr1.dimension = 1u;
    dattr1.data_offset = vmpu_off;
    dattr1.data_bytes = vmpu_bytes;
    pos = dir.size();
    dir.resize(pos + sizeof(dattr1));
    std::memcpy(dir.data() + pos, &dattr1, sizeof(dattr1));

    align_vec(dir, 128);

    impulse_relation_directory_entry_t rel{};
    rel.relation_id = 0;
    rel.src_domain_id = 0;
    rel.tgt_domain_id = 0;
    rel.encoding_id = IMPULSE_ENC_RAW;
    rel.node_id_width = 4;
    rel.edge_index_width = 4;
    rel.name_offset = off_line;
    rel.node_count = 4;
    rel.edge_count = 4;
    rel.csr_row_off_offset = csr_r_off;
    rel.csr_row_off_bytes = csr_r_bytes;
    rel.csr_col_idx_offset = csr_c_off;
    rel.csr_col_idx_bytes = csr_c_bytes;
    rel.csc_row_off_offset = csc_r_off;
    rel.csc_row_off_bytes = csc_r_bytes;
    rel.csc_col_idx_offset = csc_c_off;
    rel.csc_col_idx_bytes = csc_c_bytes;
    rel.attr_count = 1;
    pos = dir.size();
    dir.resize(pos + sizeof(rel));
    std::memcpy(dir.data() + pos, &rel, sizeof(rel));

    impulse_attribute_descriptor_t rattr0{};
    rattr0.name_offset = off_rohm;
    rattr0.type_code = 0x06u; // Float32
    rattr0.dimension = 1u;
    rattr0.data_offset = ew_off;
    rattr0.data_bytes = ew_bytes;
    pos = dir.size();
    dir.resize(pos + sizeof(rattr0));
    std::memcpy(dir.data() + pos, &rattr0, sizeof(rattr0));

    align_vec(dir, 128);
    HARDEN_CHECK(4096u + dir.size() <= 8192u, "Directory fits before offset 8192");

    buf.insert(buf.end(), dir.begin(), dir.end());
    buf.resize(8192u, 0u);
    buf.insert(buf.end(), data_slab.begin(), data_slab.end());
    align_vec(buf, 128);

    // Append metadata footer + 16B trailer
    std::vector<uint8_t> footer;
    uint32_t kv_count = 1u;
    uint16_t klen = 9u;
    const char* kstr = "generator";
    uint32_t vlen = 15u;
    const char* vstr = "hardening_suite";
    footer.resize(4 + 2 + klen + 4 + vlen);
    size_t fcur = 0;
    std::memcpy(footer.data() + fcur, &kv_count, 4); fcur += 4;
    std::memcpy(footer.data() + fcur, &klen, 2); fcur += 2;
    std::memcpy(footer.data() + fcur, kstr, klen); fcur += klen;
    std::memcpy(footer.data() + fcur, &vlen, 4); fcur += 4;
    std::memcpy(footer.data() + fcur, vstr, vlen); fcur += vlen;

    impulse_footer_trailer_t tr{};
    tr.footer_length = footer.size() + sizeof(tr);
    tr.spec_version = 10;
    tr.footer_magic = IMPULSE_MAGIC;
    buf.insert(buf.end(), footer.begin(), footer.end());
    pos = buf.size();
    buf.resize(pos + sizeof(tr));
    std::memcpy(buf.data() + pos, &tr, sizeof(tr));

    // Populate Page 0 Header
    impulse_snapshot_header_t hdr{};
    hdr.magic = IMPULSE_MAGIC;
    hdr.version = 10;
    hdr.data_offset = 4096;
    hdr.domain_count = 1;
    hdr.relation_count = 1;
    hdr.timestamp_ms = 1760000000000ULL;
    hdr.required_features = 0ULL;
    hdr.footer_directory_offset = 0ULL;
    hdr.footer_directory_bytes = 0ULL;
    hdr.index_count = 0;
    std::memcpy(buf.data(), &hdr, sizeof(hdr));
    stamp_crc16(buf);
    return buf;
}

void test_header_and_filesystem_attacks() {
    std::cout << "  [1/8] Header, truncated file, & special-file attacks..." << std::endl;

    // 1a. Directory path must be rejected cleanly (not mmap'd)
    {
        impulse_status_t st = IMPULSE_OK;
        impulse_snapshot_t* snap = impulse_snapshot_open("/tmp", &st);
        HARDEN_CHECK(snap == nullptr && st == IMPULSE_ERR_IO_FAILURE, "Opening directory /tmp must fail with IO_FAILURE");
    }

    // 1b. Truncated sizes: 0B, 63B, 64B, 4095B
    for (size_t sz : {size_t(0), size_t(63), size_t(64), size_t(4095)}) {
        std::vector<uint8_t> tiny(sz, 0x49u);
        ScopedTempFile tf("trunc", tiny);
        impulse_status_t st = IMPULSE_OK;
        impulse_snapshot_t* snap = impulse_snapshot_open(tf.path.c_str(), &st);
        HARDEN_CHECK(snap == nullptr && st == IMPULSE_ERR_IO_FAILURE, "Sub-4KB file must fail cleanly");
    }

    // 1c. Bad magic, bad version, unknown required_features, corrupt CRC-16, OOB footer_directory_offset
    {
        auto base = build_valid_v10_snapshot();
        {
            ScopedTempFile ok_f("valid_base", base);
            impulse_status_t st = IMPULSE_ERR_IO_FAILURE;
            impulse_snapshot_t* snap = impulse_snapshot_open(ok_f.path.c_str(), &st);
            HARDEN_CHECK(snap != nullptr && st == IMPULSE_OK, "Baseline valid v0.10 snapshot must open");
            impulse_snapshot_close(snap);
        }
        {
            auto bad = base;
            bad[0] = 'X';
            ScopedTempFile tf("bad_magic", bad);
            impulse_status_t st = IMPULSE_OK;
            HARDEN_CHECK(impulse_snapshot_open(tf.path.c_str(), &st) == nullptr && st == IMPULSE_ERR_INVALID_MAGIC,
                         "Bad magic rejected");
        }
        {
            auto bad = base;
            uint16_t v = 99;
            std::memcpy(bad.data() + 4, &v, 2);
            stamp_crc16(bad);
            ScopedTempFile tf("bad_ver", bad);
            impulse_status_t st = IMPULSE_OK;
            HARDEN_CHECK(impulse_snapshot_open(tf.path.c_str(), &st) == nullptr && st == IMPULSE_ERR_UNSUPPORTED_VERSION,
                         "Unsupported version rejected");
        }
        {
            auto bad = base;
            uint64_t feat = (1ULL << 62);
            std::memcpy(bad.data() + 22, &feat, 8);
            stamp_crc16(bad);
            ScopedTempFile tf("bad_feat", bad);
            impulse_status_t st = IMPULSE_OK;
            HARDEN_CHECK(impulse_snapshot_open(tf.path.c_str(), &st) == nullptr && st == IMPULSE_ERR_UNSUPPORTED_GLOBAL_FEATURE,
                         "Unknown required_features rejected");
        }
        {
            auto bad = base;
            bad[0x3E] ^= 0xFFu;
            ScopedTempFile tf("bad_crc", bad);
            impulse_status_t st = IMPULSE_OK;
            HARDEN_CHECK(impulse_snapshot_open(tf.path.c_str(), &st) == nullptr && st == IMPULSE_ERR_CORRUPT_CHECKSUM,
                         "Bad CRC16 rejected");
        }
        {
            auto bad = base;
            uint64_t fdo = UINT64_MAX - 64ULL;
            std::memcpy(bad.data() + 30, &fdo, 8);
            stamp_crc16(bad);
            ScopedTempFile tf("bad_fdo", bad);
            impulse_status_t st = IMPULSE_OK;
            HARDEN_CHECK(impulse_snapshot_open(tf.path.c_str(), &st) == nullptr && st == IMPULSE_ERR_BUFFER_OVERFLOW,
                         "OOB footer_directory_offset must not silently fall back to data_offset");
        }
    }
}

void test_utf8_and_string_pool_attacks() {
    std::cout << "  [2/8] Strict RFC-3629 UTF-8 & Section 2 string pool attacks..." << std::endl;

    auto make_snap_with_domain_name = [](const std::vector<uint8_t>& raw_name) {
        std::vector<uint8_t> buf(4096, 0u);
        std::vector<uint8_t> pool;
        pool.push_back(0u);
        pool.insert(pool.end(), raw_name.begin(), raw_name.end());
        pool.push_back(0u);

        uint32_t pool_len = static_cast<uint32_t>(pool.size());
        std::vector<uint8_t> dir(4);
        std::memcpy(dir.data(), &pool_len, 4);
        dir.insert(dir.end(), pool.begin(), pool.end());
        align_vec(dir, 128);

        impulse_domain_catalog_entry_v0_10_t dom{};
        dom.domain_id = 0;
        dom.key_type = 1;
        dom.name_offset = 1u;
        dom.node_count = 0;
        dom.attr_count = 0;
        dom.key_attr_index = 0xFFFFu;
        size_t pos = dir.size();
        dir.resize(pos + sizeof(dom));
        std::memcpy(dir.data() + pos, &dom, sizeof(dom));
        align_vec(dir, 128);

        buf.insert(buf.end(), dir.begin(), dir.end());
        impulse_snapshot_header_t hdr{};
        hdr.magic = IMPULSE_MAGIC;
        hdr.version = 10;
        hdr.data_offset = 4096;
        hdr.domain_count = 1;
        hdr.relation_count = 0;
        std::memcpy(buf.data(), &hdr, sizeof(hdr));
        stamp_crc16(buf);
        return buf;
    };

    // Overlong 2-byte '/', overlong 3-byte, UTF-16 surrogate U+D800, 4-byte overlong, > U+10FFFF
    const std::vector<std::vector<uint8_t>> bad_utf8_cases = {
        {0xC0u, 0xAFu},                         // 2-byte overlong '/'
        {0xC1u, 0xBFu},                         // 2-byte overlong
        {0xE0u, 0x80u, 0xAFu},                  // 3-byte overlong
        {0xEDu, 0xA0u, 0x80u},                  // UTF-16 high surrogate U+D800
        {0xEDu, 0xBFu, 0xBFu},                  // UTF-16 low surrogate U+DFFF
        {0xF0u, 0x80u, 0x80u, 0xAFu},           // 4-byte overlong
        {0xF4u, 0x90u, 0x80u, 0x80u},           // U+110000 (> U+10FFFF)
        {0xF5u, 0x80u, 0x80u, 0x80u},           // Invalid 0xF5 lead byte
    };

    for (size_t i = 0; i < bad_utf8_cases.size(); ++i) {
        auto bad = make_snap_with_domain_name(bad_utf8_cases[i]);
        ScopedTempFile tf("bad_utf8", bad);
        impulse_status_t st = IMPULSE_OK;
        impulse_snapshot_t* snap = impulse_snapshot_open(tf.path.c_str(), &st);
        HARDEN_CHECK(snap == nullptr && st == IMPULSE_ERR_INVALID_ARGUMENT,
                     "Invalid/overlong/surrogate UTF-8 sequence must be rejected");
    }

    // Corrupt relation name_offset (previously ignored!)
    {
        auto bad = build_valid_v10_snapshot();
        // Relation entry starts after: 4096 + 128 (string pool) + 128 (dom + 2*44 = 120 -> 128) = 4096 + 256 = 4352
        uint32_t oob_name = 999999u;
        std::memcpy(bad.data() + 4352 + offsetof(impulse_relation_directory_entry_t, name_offset), &oob_name, 4);
        ScopedTempFile tf("bad_rel_name", bad);
        impulse_status_t st = IMPULSE_OK;
        HARDEN_CHECK(impulse_snapshot_open(tf.path.c_str(), &st) == nullptr && st == IMPULSE_ERR_BUFFER_OVERFLOW,
                     "OOB relation name_offset must fail");
    }
}

void test_integer_overflow_attacks() {
    std::cout << "  [3/8] 64-bit integer wrap-around attacks (UINT64_MAX + 1, mul overflow)..." << std::endl;

    // 3a. rel.node_count = UINT64_MAX -> (node_count + 1) * 4 wraps to 0 without safe_add!
    {
        auto bad = build_valid_v10_snapshot();
        uint64_t umax = UINT64_MAX;
        std::memcpy(bad.data() + 4352 + offsetof(impulse_relation_directory_entry_t, node_count), &umax, 8);
        ScopedTempFile tf("nc_umax", bad);
        impulse_status_t st = IMPULSE_OK;
        HARDEN_CHECK(impulse_snapshot_open(tf.path.c_str(), &st) == nullptr && st == IMPULSE_ERR_BUFFER_OVERFLOW,
                     "rel.node_count = UINT64_MAX wrap-around must fail");
    }

    // 3b. dom.node_count = UINT64_MAX -> CSC (tgt_node_count + 1) * 4 and nullable bitmap wrap-around!
    {
        auto bad = build_valid_v10_snapshot();
        uint64_t umax = UINT64_MAX;
        // Domain entry starts at 4096 + 128 = 4224; node_count is at offset 8 inside v0.10 domain entry
        std::memcpy(bad.data() + 4224 + offsetof(impulse_domain_catalog_entry_v0_10_t, node_count), &umax, 8);
        ScopedTempFile tf("dom_umax", bad);
        impulse_status_t st = IMPULSE_OK;
        HARDEN_CHECK(impulse_snapshot_open(tf.path.c_str(), &st) == nullptr && st == IMPULSE_ERR_BUFFER_OVERFLOW,
                     "dom.node_count = UINT64_MAX wrap-around must fail");
    }

    // 3c. rel.edge_count = 0x4000000000000001ULL -> edge_count * 4 wraps to 4!
    {
        auto bad = build_valid_v10_snapshot();
        uint64_t huge_edges = 0x4000000000000001ULL;
        std::memcpy(bad.data() + 4352 + offsetof(impulse_relation_directory_entry_t, edge_count), &huge_edges, 8);
        ScopedTempFile tf("ec_wrap", bad);
        impulse_status_t st = IMPULSE_OK;
        HARDEN_CHECK(impulse_snapshot_open(tf.path.c_str(), &st) == nullptr && st != IMPULSE_OK,
                     "rel.edge_count * 4 wrap-around must fail");
    }

    // 3d. Footer metadata vlen = 0xFFFFFFFFu must break cleanly without 64-bit wrap or OOB read
    {
        auto bad = build_valid_v10_snapshot();
        // Mutate vlen right before the 15B "hardening_suite" + 16B trailer
        size_t vlen_pos = bad.size() - 16 - 15 - 4;
        uint32_t bad_vlen = 0xFFFFFFFFu;
        std::memcpy(bad.data() + vlen_pos, &bad_vlen, 4);
        ScopedTempFile tf("bad_footer_vlen", bad);
        impulse_status_t st = IMPULSE_ERR_IO_FAILURE;
        impulse_snapshot_t* snap = impulse_snapshot_open(tf.path.c_str(), &st);
        HARDEN_CHECK(snap != nullptr && st == IMPULSE_OK, "Truncated/malformed optional metadata KV skipped safely");
        char val_buf[64]{};
        HARDEN_CHECK(impulse_snapshot_get_metadata(snap, "generator", val_buf, sizeof(val_buf)) != IMPULSE_OK,
                     "Malformed KV entry must not be inserted");
        impulse_snapshot_close(snap);
    }
}

void test_attribute_and_domain_reference_attacks() {
    std::cout << "  [4/8] Nullable validity bitmap, non-monotonic VarString, & domain ID attacks..." << std::endl;

    // 4a. Relation src_domain_id out of bounds
    {
        auto bad = build_valid_v10_snapshot();
        uint16_t bad_dom = 7;
        std::memcpy(bad.data() + 4352 + offsetof(impulse_relation_directory_entry_t, src_domain_id), &bad_dom, 2);
        ScopedTempFile tf("bad_src_dom", bad);
        impulse_status_t st = IMPULSE_OK;
        HARDEN_CHECK(impulse_snapshot_open(tf.path.c_str(), &st) == nullptr && st == IMPULSE_ERR_INVALID_ARGUMENT,
                     "OOB src_domain_id rejected");
    }

    // 4b. Unsupported node_id_width = 3
    {
        auto bad = build_valid_v10_snapshot();
        uint8_t bad_w = 3;
        std::memcpy(bad.data() + 4352 + offsetof(impulse_relation_directory_entry_t, node_id_width), &bad_w, 1);
        ScopedTempFile tf("bad_nw", bad);
        impulse_status_t st = IMPULSE_OK;
        HARDEN_CHECK(impulse_snapshot_open(tf.path.c_str(), &st) == nullptr && st == IMPULSE_ERR_UNSUPPORTED_SECTION_FEATURE,
                     "Invalid node_id_width=3 rejected");
    }

    // 4c. Non-monotonic VarString offsets (offsets[2] < offsets[1])
    {
        auto bad = build_valid_v10_snapshot();
        // pk_offs is the 2nd allocated section in data_slab: 8192 + 128 = 8320
        uint32_t non_mono[5] = {0u, 6u, 3u, 9u, 12u};
        std::memcpy(bad.data() + 8320, non_mono, sizeof(non_mono));
        ScopedTempFile tf("non_mono_varstr", bad);
        impulse_status_t st = IMPULSE_OK;
        HARDEN_CHECK(impulse_snapshot_open(tf.path.c_str(), &st) == nullptr && st == IMPULSE_ERR_INVALID_ARGUMENT,
                     "Non-monotonic VarString offsets rejected");
    }

    // 4d. Nullable Float32 data_bytes truncated below 128B bitmap + 16B payload
    {
        auto bad = build_valid_v10_snapshot();
        // dattr1 starts at 4224 + 32 (dom) + 44 (dattr0) = 4300
        uint64_t tiny_bytes = 64ULL; // < 128B padded validity bitmap!
        std::memcpy(bad.data() + 4300 + offsetof(impulse_attribute_descriptor_t, data_bytes), &tiny_bytes, 8);
        ScopedTempFile tf("trunc_null_bm", bad);
        impulse_status_t st = IMPULSE_OK;
        HARDEN_CHECK(impulse_snapshot_open(tf.path.c_str(), &st) == nullptr && st == IMPULSE_ERR_BUFFER_OVERFLOW,
                     "Nullable attribute smaller than 128B validity bitmap rejected");
    }
}

void test_virtual_super_relation_cycles() {
    std::cout << "  [5/8] Virtual super-relation OOB child, self-loop, & 2-cycle attacks..." << std::endl;

    auto make_two_virtual_rels = [](uint16_t r0_child, uint16_t r1_child) {
        std::vector<uint8_t> buf(4096, 0u);
        const char kPool[] = "\0Bus\0V0\0V1\0";
        uint32_t pool_len = sizeof(kPool) - 1u;
        std::vector<uint8_t> dir(4);
        std::memcpy(dir.data(), &pool_len, 4);
        dir.insert(dir.end(), reinterpret_cast<const uint8_t*>(kPool), reinterpret_cast<const uint8_t*>(kPool) + pool_len);
        align_vec(dir, 128);

        impulse_domain_catalog_entry_v0_10_t dom{};
        dom.domain_id = 0;
        dom.key_type = 1;
        dom.name_offset = 1u;
        dom.node_count = 4;
        dom.attr_count = 0;
        dom.key_attr_index = 0xFFFFu;
        size_t pos = dir.size();
        dir.resize(pos + sizeof(dom));
        std::memcpy(dir.data() + pos, &dom, sizeof(dom));
        align_vec(dir, 128);

        for (int r = 0; r < 2; ++r) {
            impulse_relation_directory_entry_t rel{};
            rel.relation_id = static_cast<uint16_t>(r);
            rel.src_domain_id = 0;
            rel.tgt_domain_id = 0;
            rel.node_id_width = 4;
            rel.edge_index_width = 4;
            rel.name_offset = (r == 0) ? 5u : 8u;
            rel.section_features = 0x04ULL; // virtual super-relation
            uint16_t comp_cnt = 1u;
            uint16_t child_id = (r == 0) ? r0_child : r1_child;
            std::memcpy(&rel.reserved2[0], &comp_cnt, 2);
            std::memcpy(&rel.reserved2[2], &child_id, 2);
            pos = dir.size();
            dir.resize(pos + sizeof(rel));
            std::memcpy(dir.data() + pos, &rel, sizeof(rel));
        }
        align_vec(dir, 128);
        buf.insert(buf.end(), dir.begin(), dir.end());

        impulse_snapshot_header_t hdr{};
        hdr.magic = IMPULSE_MAGIC;
        hdr.version = 10;
        hdr.data_offset = 4096;
        hdr.domain_count = 1;
        hdr.relation_count = 2;
        std::memcpy(buf.data(), &hdr, sizeof(hdr));
        stamp_crc16(buf);
        return buf;
    };

    // Self-loop R0 -> R0
    {
        ScopedTempFile tf("virt_self", make_two_virtual_rels(0, 0));
        impulse_status_t st = IMPULSE_OK;
        HARDEN_CHECK(impulse_snapshot_open(tf.path.c_str(), &st) == nullptr && st == IMPULSE_ERR_INVALID_ARGUMENT,
                     "Virtual self-loop R0->R0 rejected");
    }
    // OOB child R0 -> 99
    {
        ScopedTempFile tf("virt_oob", make_two_virtual_rels(99, 0));
        impulse_status_t st = IMPULSE_OK;
        HARDEN_CHECK(impulse_snapshot_open(tf.path.c_str(), &st) == nullptr && st == IMPULSE_ERR_INVALID_ARGUMENT,
                     "Virtual OOB child R0->99 rejected");
    }
    // 2-cycle R0 -> R1 -> R0
    {
        ScopedTempFile tf("virt_cycle", make_two_virtual_rels(1, 0));
        impulse_status_t st = IMPULSE_OK;
        HARDEN_CHECK(impulse_snapshot_open(tf.path.c_str(), &st) == nullptr && st == IMPULSE_ERR_INVALID_ARGUMENT,
                     "Virtual 2-cycle R0->R1->R0 rejected");
    }
}

void test_secondary_index_and_vamana_attacks() {
    std::cout << "  [6/8] Secondary index (ZoneMap 32-bit wrap, Temporal 64-bit wrap, Bitset align, Vamana overflow)..." << std::endl;

    // 6a. ZoneMap page_count = 0xFFFFFFFFu (previously wrapped (page_count + 63) / 64 to 0 in uint32_t!)
    {
        std::vector<uint8_t> zm(64, 0u);
        uint32_t bad_pages = 0xFFFFFFFFu;
        std::memcpy(zm.data() + 12, &bad_pages, 4); // ZoneMapHeader::page_count at offset 12
        uint64_t mask[4]{};
        size_t elig = 0;
        impulse_status_t st = impulse_index_zone_map_filter(zm.data(), zm.size(), 0.0, 1.0, mask, 4, &elig);
        HARDEN_CHECK(st == IMPULSE_ERR_BUFFER_OVERFLOW, "ZoneMap page_count=0xFFFFFFFF overflow rejected");
    }

    // 6b. TemporalInterval count = 0x1000000000000001ULL (64-bit count * 20 overflow!)
    {
        std::vector<uint8_t> ti(64, 0u);
        uint64_t bad_cnt = 0x1000000000000001ULL;
        std::memcpy(ti.data(), &bad_cnt, 8);
        uint32_t out_ids[4]{};
        size_t out_cnt = 0;
        impulse_status_t st = impulse_index_temporal_interval_query(ti.data(), ti.size(), 100ULL, out_ids, 4, &out_cnt);
        HARDEN_CHECK(st == IMPULSE_ERR_BUFFER_OVERFLOW, "TemporalInterval 64-bit count*20 wrap rejected");
    }

    // 6c. InvertedBitset unaligned bitset_offset rejected cleanly (prevents misaligned uint64_t* UBSan trap)
    {
        const char* keys[2] = {"alpha", "beta"};
        void* raw_idx = nullptr;
        size_t idx_sz = 0;
        HARDEN_CHECK(impulse_index_build_inverted_bitset(keys, 2, &raw_idx, &idx_sz) == IMPULSE_OK, "Build valid inverted bitset");
        const uint64_t* words = nullptr;
        size_t nwords = 0;
        HARDEN_CHECK(impulse_index_inverted_bitset_lookup(raw_idx, idx_sz, "alpha", &words, &nwords) == IMPULSE_OK && words != nullptr,
                     "Aligned inverted bitset lookup succeeds");
        HARDEN_CHECK((reinterpret_cast<uintptr_t>(words) & 7ULL) == 0ULL, "Returned bitset pointer is 8B aligned");

        // Corrupt bitset_offset of entry 0 to an unaligned offset (+1)
        std::vector<uint8_t> corrupt(static_cast<const uint8_t*>(raw_idx), static_cast<const uint8_t*>(raw_idx) + idx_sz + 8);
        uint32_t str_bytes = 0;
        std::memcpy(&str_bytes, corrupt.data() + 12, 4);
        size_t entry0_bitset_off_pos = 32 + str_bytes + 4;
        uint64_t orig_off = 0;
        std::memcpy(&orig_off, corrupt.data() + entry0_bitset_off_pos, 8);
        uint64_t unaligned_off = orig_off + 1ULL;
        std::memcpy(corrupt.data() + entry0_bitset_off_pos, &unaligned_off, 8);
        impulse_status_t st = impulse_index_inverted_bitset_lookup(corrupt.data(), corrupt.size(), "alpha", &words, &nwords);
        impulse_status_t st2 = impulse_index_inverted_bitset_lookup(corrupt.data(), corrupt.size(), "beta", &words, &nwords);
        HARDEN_CHECK(st != IMPULSE_OK || st2 != IMPULSE_OK, "Unaligned InvertedBitset bitset_offset rejected");
        impulse_index_free(raw_idx);
    }

    // 6d. Vamana OP_VECTOR_ANN_SEARCH (0xCA) with overflowing v_node_count in 64B header
    {
        auto snap_bytes = build_valid_v10_snapshot();
        align_vec(snap_bytes, 128);
        uint64_t vamana_hdr_off = snap_bytes.size();
        HARDEN_CHECK(vamana_hdr_off >= 4096ULL && vamana_hdr_off <= 0xFFFFULL && (vamana_hdr_off % 128ULL) == 0ULL,
                     "Vamana header offset fits 16-bit immediate");
        std::vector<uint8_t> vhdr(128, 0u);
        const uint32_t vmagic = 0x56414D41u; // OP_CA_VAMANA_MAGIC
        const uint16_t vver = 1u;
        const uint8_t vmetric = 0u;
        const uint8_t vquant = 0u;
        const uint16_t vdom = 0u;
        const uint16_t vattr = 1u;
        const uint32_t vdim = 4u;
        const uint64_t vnodes = 0x2000000000000000ULL; // overflows vnodes * (32 + 1) * 4!
        const uint32_t vmax_r = 32u;
        const uint32_t vmedoid = 0u;
        const uint32_t vef = 16u;
        const uint64_t vadj_off = 8192ULL;
        const uint64_t vadj_bytes = 128ULL;
        std::memcpy(vhdr.data() + 0, &vmagic, 4);
        std::memcpy(vhdr.data() + 4, &vver, 2);
        std::memcpy(vhdr.data() + 6, &vmetric, 1);
        std::memcpy(vhdr.data() + 7, &vquant, 1);
        std::memcpy(vhdr.data() + 8, &vdom, 2);
        std::memcpy(vhdr.data() + 10, &vattr, 2);
        std::memcpy(vhdr.data() + 12, &vdim, 4);
        std::memcpy(vhdr.data() + 16, &vnodes, 8);
        std::memcpy(vhdr.data() + 24, &vmax_r, 4);
        std::memcpy(vhdr.data() + 28, &vmedoid, 4);
        std::memcpy(vhdr.data() + 36, &vef, 4);
        std::memcpy(vhdr.data() + 40, &vadj_off, 8);
        std::memcpy(vhdr.data() + 48, &vadj_bytes, 8);
        snap_bytes.insert(snap_bytes.end(), vhdr.begin(), vhdr.end());

        ScopedTempFile tf("vamana_ovf", snap_bytes);
        impulse_status_t st = IMPULSE_OK;
        impulse_snapshot_t* snap = impulse_snapshot_open(tf.path.c_str(), &st);
        HARDEN_CHECK(snap != nullptr && st == IMPULSE_OK, "Snapshot opens with raw Vamana bytes");

        impulse_vm_context_t* ctx = impulse_vm_context_create(snap);
        HARDEN_CHECK(ctx != nullptr, "VM context created");
        int h_q = impulse_vm_context_acquire_float_vector(ctx);
        HARDEN_CHECK(h_q >= 0, "Acquired float vector handle");
        impulse_vm_state_t vm{};
        vm.query_context = ctx;
        vm.registers[1] = static_cast<uint64_t>(h_q);
        vm.register_types[1] = TYPE_FLOAT_VECTOR;

        // 0xCA OP_VECTOR_ANN_SEARCH: dst=0, q_reg=1 (low8), top_k=2 (byte1), vhdr_off (high16)
        impulse_instruction_t prog[2]{};
        prog[0] = {0xCAu, 0u, 0u, static_cast<uint32_t>(1u | (2u << 8) | (static_cast<uint32_t>(vamana_hdr_off) << 16))};
        prog[1] = {0xFFu, 0u, 0u, 0u};

        impulse_vm_status_t vst = impulse_vm_execute(prog, 2, &vm, 0);
        HARDEN_CHECK(vst == IMPULSE_VM_ERR_OUT_OF_BOUNDS, "Overflowing Vamana v_node_count returns VM_ERR_OUT_OF_BOUNDS");

        impulse_vm_context_destroy(ctx);
        impulse_snapshot_close(snap);
    }
}

void test_astronomical_vm_context_guard() {
    std::cout << "  [7/8] Astronomical empty-domain node_count guard in impulse_vm_context_create..." << std::endl;

    // A valid empty-attribute domain can legally declare 1ULL << 50 dense IDs without payload bytes,
    // which must NOT cause std::bad_alloc abort inside impulse_vm_context_create!
    std::vector<uint8_t> buf(4096, 0u);
    const char kPool[] = "\0Hyperscale\0";
    uint32_t pool_len = sizeof(kPool) - 1u;
    std::vector<uint8_t> dir(4);
    std::memcpy(dir.data(), &pool_len, 4);
    dir.insert(dir.end(), reinterpret_cast<const uint8_t*>(kPool), reinterpret_cast<const uint8_t*>(kPool) + pool_len);
    align_vec(dir, 128);

    impulse_domain_catalog_entry_v0_10_t dom{};
    dom.domain_id = 0;
    dom.key_type = 1;
    dom.name_offset = 1u;
    dom.node_count = (1ULL << 50);
    dom.attr_count = 0;
    dom.key_attr_index = 0xFFFFu;
    size_t pos = dir.size();
    dir.resize(pos + sizeof(dom));
    std::memcpy(dir.data() + pos, &dom, sizeof(dom));
    align_vec(dir, 128);
    buf.insert(buf.end(), dir.begin(), dir.end());

    impulse_snapshot_header_t hdr{};
    hdr.magic = IMPULSE_MAGIC;
    hdr.version = 10;
    hdr.data_offset = 4096;
    hdr.domain_count = 1;
    hdr.relation_count = 0;
    std::memcpy(buf.data(), &hdr, sizeof(hdr));
    stamp_crc16(buf);

    ScopedTempFile tf("astro_nodes", buf);
    impulse_status_t st = IMPULSE_OK;
    impulse_snapshot_t* snap = impulse_snapshot_open(tf.path.c_str(), &st);
    HARDEN_CHECK(snap != nullptr && st == IMPULSE_OK, "Empty-attr domain opens");
    impulse_vm_context_t* ctx = impulse_vm_context_create(snap);
    HARDEN_CHECK(ctx == nullptr, "impulse_vm_context_create cleanly returns nullptr instead of std::bad_alloc abort");
    impulse_snapshot_close(snap);
}

void test_deterministic_500_mutation_fuzz() {
    std::cout << "  [8/8] 500-iteration deterministic PRNG corruption fuzzer (open + accessors + VM)..." << std::endl;

    const auto valid = build_valid_v10_snapshot();
    const std::string path = temp_imps_path("fuzz500");

    uint64_t rng = 0x9E3779B97F4A7C15ULL;
    auto next_u64 = [&]() -> uint64_t {
        rng ^= rng >> 12;
        rng ^= rng << 25;
        rng ^= rng >> 27;
        return rng * 2685821657736338717ULL;
    };

    size_t opened_ok = 0;
    size_t rejected_cleanly = 0;

    for (int iter = 0; iter < 500; ++iter) {
        auto mutated = valid;
        int num_flips = 1 + static_cast<int>(next_u64() % 4ULL);
        for (int f = 0; f < num_flips; ++f) {
            size_t off = 0;
            uint64_t region_pick = next_u64() & 3ULL;
            if (region_pick == 0ULL && mutated.size() >= 64) {
                // Active Page 0 header (bytes 0..63)
                off = static_cast<size_t>(next_u64() % 64ULL);
            } else if (region_pick == 1ULL && mutated.size() >= 4608) {
                // Active Page 1 directory & string pool (bytes 4096..4607)
                off = 4096ULL + static_cast<size_t>(next_u64() % 512ULL);
            } else if (region_pick == 2ULL && mutated.size() > 8192) {
                // Active data sections & footer directory (bytes 8192..end)
                off = 8192ULL + static_cast<size_t>(next_u64() % (mutated.size() - 8192ULL));
            } else {
                // Uniform full-file offset (including 4KB page alignment padding)
                off = static_cast<size_t>(next_u64() % mutated.size());
            }
            if ((next_u64() & 3ULL) == 0ULL && off + 8 <= mutated.size()) {
                uint64_t nasty = (next_u64() & 1ULL) ? UINT64_MAX : 0x8000000000000001ULL;
                std::memcpy(mutated.data() + off, &nasty, 8);
            } else {
                mutated[off] ^= static_cast<uint8_t>(1u + (next_u64() % 255ULL));
            }
        }
        // Re-stamp header CRC-16 half the time so fuzzing reaches deep directory & section parsers!
        if ((iter & 1) == 0 && mutated.size() >= 64) {
            stamp_crc16(mutated);
        }

        write_bytes(path, mutated);
        impulse_status_t st = IMPULSE_OK;
        impulse_snapshot_t* snap = impulse_snapshot_open(path.c_str(), &st);
        if (!snap) {
            HARDEN_CHECK(st != IMPULSE_OK, "Null snapshot must report non-OK impulse_status_t");
            ++rejected_cleanly;
            continue;
        }
        ++opened_ok;

        // Exercise public C API & VM context on every accepted fuzzed snapshot
        (void)impulse_snapshot_is_reachable(snap, 0, 0, 1);
        uint64_t src_nodes[2] = {0, 2};
        uint64_t out_s[4]{}, out_t[4]{};
        size_t sampled = 0;
        (void)impulse_snapshot_sample_neighbors(snap, 0, src_nodes, 2, 2, 42ULL, out_s, out_t, 4, &sampled);
        uint32_t nid = 0;
        (void)impulse_snapshot_resolve_key(snap, 0, "n0", 2, &nid);
        const void* kbytes = nullptr;
        size_t klen = 0;
        (void)impulse_snapshot_resolve_dense_id(snap, 0, 0, &kbytes, &klen);

        impulse_vm_context_t* ctx = impulse_vm_context_create(snap);
        if (ctx) {
            impulse_vm_context_destroy(ctx);
        }
        impulse_snapshot_close(snap);
    }

    ::unlink(path.c_str());
    std::cout << "        Fuzz summary: " << rejected_cleanly << " rejected cleanly, "
              << opened_ok << " benign mutations opened & queried with 0 crashes." << std::endl;
    HARDEN_CHECK(rejected_cleanly > 100, "Fuzzer must exercise error paths");
}

} // namespace

int main() {
    // Untrusted/malicious inputs MUST NOT trip IMPULSE_WELL_FORMED_ASSERT when NEVER_CORRUPT=0
    impulse_test_control(IMPULSE_TESTCTRL_NEVER_CORRUPT, 0);

    std::cout << "=== Running Malicious / Malformed .imps Binary Hardening Suite ===" << std::endl;
    test_header_and_filesystem_attacks();
    test_utf8_and_string_pool_attacks();
    test_integer_overflow_attacks();
    test_attribute_and_domain_reference_attacks();
    test_virtual_super_relation_cycles();
    test_secondary_index_and_vamana_attacks();
    test_astronomical_vm_context_guard();
    test_deterministic_500_mutation_fuzz();

    // Also verify NEVER_CORRUPT=1 succeeds cleanly on well-formed v0.10.0 snapshot
    {
        impulse_test_control(IMPULSE_TESTCTRL_NEVER_CORRUPT, 1);
        ScopedTempFile ok_f("well_formed_audit", build_valid_v10_snapshot());
        impulse_status_t st = IMPULSE_ERR_IO_FAILURE;
        impulse_snapshot_t* snap = impulse_snapshot_open(ok_f.path.c_str(), &st);
        HARDEN_CHECK(snap != nullptr && st == IMPULSE_OK, "Well-formed snapshot passes NEVER_CORRUPT=1");
        impulse_snapshot_close(snap);
        impulse_test_control(IMPULSE_TESTCTRL_NEVER_CORRUPT, 0);
    }

    std::cout << "[PASS] All 8 malicious .imps binary hardening suites passed!" << std::endl;
    return 0;
}
