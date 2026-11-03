/**
 * @file fuzz_snapshot.cpp
 * @brief Fuzz target: .imps binary snapshot reader (impulse_snapshot_open + accessors + sampler).
 *
 * The C-ABI has no open-from-buffer entry point, so each input is written to a per-process temp
 * file and opened via impulse_snapshot_open() (mmap path). Half of the inputs (selected by the
 * parity of the input size) get their header CRC-16 repaired so mutations reach the directory /
 * section parsing code instead of being rejected at the checksum.
 *
 * Env IMPULSE_FUZZ_SNAPSHOT_TRAVERSE=1 additionally runs traversal/sampling APIs on the (possibly
 * payload-corrupted) snapshot; see traverse_payload() below.
 *
 * Invariant: open/accessors must never crash or trip ASan/UBSan; invalid input -> error status.
 */

#include "fuzz_snapshot_util.hpp"
#include "impulse_format_v0_9.h"

#include <algorithm>

namespace {

uint16_t crc16(const uint8_t* p, size_t n) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < n; ++i) {
        crc ^= static_cast<uint16_t>(p[i]) << 8;
        for (int j = 0; j < 8; ++j) crc = (crc & 0x8000) ? static_cast<uint16_t>((crc << 1) ^ 0x1021)
                                                         : static_cast<uint16_t>(crc << 1);
    }
    return crc;
}

const std::string& snap_path() {
    static const std::string p = impfuzz::tmp_path("snap");
    return p;
}

// Traversal APIs (is_reachable, sample_neighbors) dereference CSR row-offset/column payload data that
// snapshot open does NOT validate (O(N) scan skipped by design for cold start), so payload-level
// corruption can fault there. They are only exercised when IMPULSE_FUZZ_SNAPSHOT_TRAVERSE=1.
bool traverse_payload() {
    static const bool on = [] {
        const char* e = std::getenv("IMPULSE_FUZZ_SNAPSHOT_TRAVERSE");
        return e && e[0] == '1';
    }();
    return on;
}

void exercise(const impulse_snapshot_t* snap) {
    (void)impulse_snapshot_magic(snap);
    (void)impulse_snapshot_version(snap);
    uint16_t nd = std::min<uint16_t>(impulse_snapshot_domain_count(snap), 32);
    for (uint16_t d = 0; d < nd; ++d) {
        impulse_domain_catalog_entry_t e{};
        const char* name = nullptr;
        if (impulse_snapshot_get_domain_entry(snap, d, &e, &name) == IMPULSE_OK && name) (void)std::strlen(name);
        uint32_t nid = 0;
        const uint8_t key[4] = {1, 2, 3, 4};
        (void)impulse_snapshot_resolve_key(snap, d, key, sizeof(key), &nid);
        const void* kb = nullptr;
        size_t kl = 0;
        (void)impulse_snapshot_resolve_dense_id(snap, d, 0, &kb, &kl);
        (void)impulse_snapshot_resolve_dense_id(snap, d, 0xFFFFFFFFu, &kb, &kl);
    }
    uint16_t nr = std::min<uint16_t>(impulse_snapshot_relation_count(snap), 32);
    (void)impulse_snapshot_max_node_count(snap);
    for (uint16_t r = 0; r < nr; ++r) {
        impulse_relation_directory_entry_t e{};
        (void)impulse_snapshot_get_relation_entry(snap, r, &e);
        const uint32_t *o32 = nullptr, *t32 = nullptr;
        uint64_t a = 0, b = 0;
        (void)impulse_snapshot_get_relation_buffers(snap, r, &o32, &t32, &a, &b);
        (void)impulse_snapshot_get_relation_csc_buffers(snap, r, &o32, &t32, &a, &b);
        const void *ro = nullptr, *rt = nullptr;
        uint8_t nw = 0, ew = 0;
        (void)impulse_snapshot_get_relation_raw_buffers(snap, r, &ro, &rt, &a, &b, &nw, &ew);
        (void)impulse_snapshot_get_relation_csc_raw_buffers(snap, r, &ro, &rt, &a, &b, &nw, &ew);
        for (uint16_t ai = 0; ai < 3; ++ai) {
            const void *ad = nullptr, *ao = nullptr;
            uint64_t adb = 0, aob = 0;
            uint8_t tc = 0;
            uint32_t dim = 0;
            (void)impulse_snapshot_get_attribute_buffers(snap, r, ai, &ad, &adb, &ao, &aob, &tc, &dim);
        }
        if (traverse_payload()) {
            (void)impulse_snapshot_is_reachable(snap, r, 0, 0);
            (void)impulse_snapshot_is_reachable(snap, r, 1, 2);
            (void)impulse_snapshot_is_reachable(snap, r, 0xFFFFFFFFull, 0xFFFFFFFFull);
            uint64_t seeds[4] = {0, 1, 2, 0xFFFFFFFFull};
            uint64_t os[64], ot[64];
            size_t cnt = 0;
            (void)impulse_snapshot_sample_neighbors(snap, r, seeds, 4, 4, 42, os, ot, 64, &cnt);
            (void)impulse_snapshot_sample_neighbors(snap, r, seeds, 4, -1, 7, os, ot, 64, &cnt);
        }
    }
    uint16_t ni = std::min<uint16_t>(impulse_snapshot_get_index_count(snap), 32);
    for (uint16_t i = 0; i < ni; ++i) {
        uint32_t id = 0;
        uint16_t dom = 0, rel = 0, attr = 0;
        uint8_t ty = 0;
        const char* nm = nullptr;
        const void* data = nullptr;
        uint64_t bytes = 0;
        if (impulse_snapshot_get_index(snap, i, &id, &dom, &rel, &attr, &ty, &nm, &data, &bytes) == IMPULSE_OK && nm)
            (void)std::strlen(nm);
    }
    char meta[64];
    (void)impulse_snapshot_get_metadata(snap, "author", meta, sizeof(meta));
    (void)impulse_snapshot_get_buffer(snap, 0, 64);
    (void)impulse_snapshot_get_buffer(snap, 4096, 128);
}

}  // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (!data || size == 0 || size > (1u << 20)) return 0;
    impfuzz::Bytes buf(data, data + size);
    if (size >= 0x40 && (size & 1) == 0) {
        uint16_t c = crc16(buf.data(), 0x3E);
        std::memcpy(&buf[0x3E], &c, 2);
    }
    if (!impfuzz::write_file(snap_path(), buf.data(), buf.size())) return 0;
    impulse_status_t st = IMPULSE_OK;
    impulse_snapshot_t* snap = impulse_snapshot_open(snap_path().c_str(), &st);
    if (snap) {
        exercise(snap);
        impulse_snapshot_close(snap);
    }
    return 0;
}

static std::vector<impfuzz::Bytes> make_seeds() {
    std::vector<impfuzz::Bytes> seeds;
    for (int v = 0; v < 2; ++v) {
        impfuzz::Bytes b;
        if (impfuzz::build_baseline_snapshot(v, impfuzz::tmp_path("seedgen"), &b)) {
            seeds.push_back(b);
            // Header-only truncation seed: stresses bounds checks on directory reads.
            seeds.emplace_back(b.begin(), b.begin() + std::min<size_t>(b.size(), 4096 + 64));
        }
    }
    impfuzz::Bytes junk(64, 0);
    seeds.push_back(junk);
    return seeds;
}

IMPULSE_FUZZ_MAIN("fuzz_snapshot", make_seeds, 16384)
