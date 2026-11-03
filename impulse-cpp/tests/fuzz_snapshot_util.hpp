/**
 * @file fuzz_snapshot_util.hpp
 * @brief Runtime generation of baseline .imps snapshots through the official C-ABI writer
 *        (AGENTS.md 3.6/3.17: no binary snapshots in Git, no ad-hoc byte writers).
 */

#ifndef IMPULSE_FUZZ_SNAPSHOT_UTIL_HPP
#define IMPULSE_FUZZ_SNAPSHOT_UTIL_HPP

#include "fuzz_driver.hpp"
#include "impulse_graph.h"

#include <unistd.h>

namespace impfuzz {

inline std::string tmp_path(const char* tag) {
    const char* d = std::getenv("TMPDIR");
    std::string dir = d && *d ? d : "/tmp";
    if (dir.back() == '/') dir.pop_back();
    return dir + "/impulse_fuzz_" + tag + "_" + std::to_string(static_cast<long>(::getpid())) + ".imps";
}

/// variant 0: one domain, one relation (3-cycle) + float edge attribute + MPH index + metadata.
/// variant 1: two domains (User 4 nodes -> Item 4 nodes), two relations (one cross-domain), attribute.
/// Returns true and fills `out` with the file bytes. Deletes the temp file.
inline bool build_baseline_snapshot(int variant, const std::string& path, Bytes* out) {
    impulse_writer_t* w = impulse_writer_create(path.c_str(), 0);
    if (!w) return false;
    bool ok = impulse_writer_add_domain(w, 0, 0, "User") == IMPULSE_OK;
    if (variant == 0) {
        uint32_t row_off[] = {0, 1, 2, 3};
        uint16_t col_idx[] = {1, 2, 0};
        ok = ok && impulse_writer_add_relation(w, 0, 0, 0, 3, 3, 0, row_off, sizeof(row_off), col_idx,
                                               sizeof(col_idx)) == IMPULSE_OK;
        float weights[] = {1.0f, 2.0f, 3.0f};
        ok = ok && impulse_writer_add_attribute(w, 0, "weight", 2, 1, weights, sizeof(weights), nullptr, 0) ==
                       IMPULSE_OK;
        uint32_t idx_data[] = {0, 0, 1, 1, 2, 2};
        ok = ok && impulse_writer_add_index(w, 0, 0xFFFF, 0, 4, "mph_idx", idx_data, sizeof(idx_data), 0) ==
                       IMPULSE_OK;
        ok = ok && impulse_writer_set_metadata(w, "author", "impulse") == IMPULSE_OK;
    } else {
        ok = ok && impulse_writer_add_domain(w, 1, 0, "Item") == IMPULSE_OK;
        uint32_t row_off_a[] = {0, 2, 3, 4, 6};
        uint16_t col_a[] = {1, 2, 3, 0, 1, 2};
        ok = ok && impulse_writer_add_relation(w, 0, 0, 0, 4, 6, 0, row_off_a, sizeof(row_off_a), col_a,
                                               sizeof(col_a)) == IMPULSE_OK;
        uint32_t row_off_b[] = {0, 1, 2, 4, 4};
        uint16_t col_b[] = {0, 1, 2, 3};
        ok = ok && impulse_writer_add_relation(w, 0, 1, 0, 4, 4, 0, row_off_b, sizeof(row_off_b), col_b,
                                               sizeof(col_b)) == IMPULSE_OK;
        float weights[] = {0.5f, 1.5f, 2.5f, 3.5f, 4.5f, 5.5f};
        ok = ok && impulse_writer_add_attribute(w, 0, "w", 2, 1, weights, sizeof(weights), nullptr, 0) ==
                       IMPULSE_OK;
    }
    ok = ok && impulse_writer_finalize(w) == IMPULSE_OK;
    impulse_writer_destroy(w);
    bool read_ok = ok && (!out || read_file(path, *out));
    ::unlink(path.c_str());
    return read_ok;
}

/// Builds a baseline snapshot, opens it (mmap) and unlinks the file. Caller owns the handle
/// (never closed by the fuzz harnesses; lives for the process lifetime). Returns nullptr on failure.
inline impulse_snapshot_t* open_baseline_snapshot(int variant, const char* tag) {
    std::string path = tmp_path(tag);
    // build_baseline_snapshot unlinks, so build via writer again keeping the file for open().
    Bytes bytes;
    if (!build_baseline_snapshot(variant, path, &bytes)) return nullptr;
    if (!write_file(path, bytes.data(), bytes.size())) return nullptr;
    impulse_status_t st = IMPULSE_OK;
    impulse_snapshot_t* snap = impulse_snapshot_open(path.c_str(), &st);
    ::unlink(path.c_str());
    return snap;
}

}  // namespace impfuzz

#endif
