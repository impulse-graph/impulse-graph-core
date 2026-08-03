/*
 * test_vector_suite.cpp — Spec v2.4 30-Vector Test Suite for Impulse C++ Core Engine.
 */

#include "impulse_graph.h"
#include "impulse_format_v2_4.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

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

static std::string read_file_string(const fs::path& p) {
    std::ifstream ifs(p, std::ios::binary);
    if (!ifs.is_open()) return "";
    return std::string((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
}

static void test_all_30_spec_v2_4_test_vectors() {
    fs::path spec_dir("/Users/jesse/impulse/impulse-graph-spec/test-vectors");
    ASSERT_TRUE(fs::exists(spec_dir));

    int count = 0;
    int passed_valid = 0;
    int passed_rejected = 0;

    for (const auto& entry : fs::directory_iterator(spec_dir)) {
        if (!entry.is_directory()) continue;
        fs::path folder = entry.path();
        fs::path imps_file = folder / "snapshot.imps";
        fs::path manifest_file = folder / "manifest.json";

        if (!fs::exists(imps_file) || !fs::exists(manifest_file)) continue;

        std::string folder_name = folder.filename().string();
        std::string manifest_content = read_file_string(manifest_file);
        bool is_rejection = manifest_content.find("\"REJECT_") != std::string::npos ||
                           manifest_content.find("\"corrupt_") != std::string::npos ||
                           manifest_content.find("\"SUCCESS\"") == std::string::npos;

        impulse_status_t status = IMPULSE_OK;
        impulse_snapshot_t* snap = impulse_snapshot_open(imps_file.c_str(), &status);

        if (is_rejection) {
            if (snap != nullptr || status == IMPULSE_OK) {
                std::fprintf(stderr, "FAIL: Vector %s should be REJECTED, but open succeeded!\n", folder_name.c_str());
                std::abort();
            }
            std::printf("  [PASS] Test Vector Correct Rejection: %s (status=%d)\n", folder_name.c_str(), status);
            passed_rejected++;
        } else {
            if (snap == nullptr || status != IMPULSE_OK) {
                std::fprintf(stderr, "FAIL: Vector %s should LOAD cleanly, but got status %d (%s)\n",
                             folder_name.c_str(), status, impulse_get_last_error());
                std::abort();
            }

            uint32_t magic = impulse_snapshot_magic(snap);
            uint16_t version = impulse_snapshot_version(snap);
            ASSERT_EQ(magic, IMPULSE_SPEC_MAGIC_V2_4);
            ASSERT_TRUE(version == 0x0204 || version == 2);

            uint16_t rel_count = impulse_snapshot_relation_count(snap);
            for (uint16_t r = 0; r < rel_count; ++r) {
                impulse_relation_directory_entry_t rel_entry;
                impulse_status_t r_st = impulse_snapshot_get_relation_entry(snap, r, &rel_entry);
                ASSERT_EQ(r_st, IMPULSE_OK);
            }

            // Test 1: Zero-Delta Compaction
            std::string comp_file = "__temp_compacted_" + folder_name + ".imps";
            impulse_status_t c_st = impulse_snapshot_compact_to_file(snap, nullptr, 0, comp_file.c_str());
            ASSERT_EQ(c_st, IMPULSE_OK);

            impulse_snapshot_t* comp_snap = impulse_snapshot_open(comp_file.c_str(), &c_st);
            ASSERT_EQ(c_st, IMPULSE_OK);
            ASSERT_TRUE(comp_snap != nullptr);
            impulse_snapshot_close(comp_snap);

            // Cross-verify zero-delta compacted file with Rust impulse-graph validate tool
            std::string cli_cmd1 = "/Users/jesse/impulse/impulse-graph-tooling/target/release/impulse-graph validate " + comp_file;
            int ret1 = std::system(cli_cmd1.c_str());
            if (ret1 != 0) {
                std::fprintf(stderr, "FAIL: impulse-graph validate failed on zero-delta compacted vector %s (ret=%d)\n", folder_name.c_str(), ret1);
                std::abort();
            }
            std::remove(comp_file.c_str());

            // Test 2: Live Delta Layer Compaction (with additions and tombstones)
            std::string delta_file = "__temp_delta_" + folder_name + ".imps";
            std::vector<impulse_delta_layer_t*> deltas;
            for (uint16_t r = 0; r < rel_count; ++r) {
                impulse_relation_directory_entry_t rentry;
                impulse_snapshot_get_relation_entry(snap, r, &rentry);
                impulse_delta_layer_t* d = impulse_delta_layer_create(rentry.src_domain_id, rentry.tgt_domain_id, "rel");
                if (rentry.node_count > 0) {
                    impulse_delta_layer_add_edge(d, 0, 9999);
                    impulse_delta_layer_tombstone_edge(d, 0, 0);
                }
                deltas.push_back(d);
            }

            c_st = impulse_snapshot_compact_to_file(snap, deltas.data(), deltas.size(), delta_file.c_str());
            ASSERT_EQ(c_st, IMPULSE_OK);

            impulse_snapshot_t* delta_snap = impulse_snapshot_open(delta_file.c_str(), &c_st);
            ASSERT_EQ(c_st, IMPULSE_OK);
            ASSERT_TRUE(delta_snap != nullptr);
            impulse_snapshot_close(delta_snap);

            // Cross-verify live delta compacted file with Rust impulse-graph validate tool
            std::string cli_cmd2 = "/Users/jesse/impulse/impulse-graph-tooling/target/release/impulse-graph validate " + delta_file;
            int ret2 = std::system(cli_cmd2.c_str());
            if (ret2 != 0) {
                std::fprintf(stderr, "FAIL: impulse-graph validate failed on live delta compacted vector %s (ret=%d)\n", folder_name.c_str(), ret2);
                std::abort();
            }
            std::remove(delta_file.c_str());

            for (auto* d : deltas) {
                impulse_delta_layer_destroy(d);
            }

            impulse_snapshot_close(snap);
            std::printf("  [PASS] Test Vector Load & Compaction SUCCESS: %s\n", folder_name.c_str());
            passed_valid++;
        }
        count++;
    }

    ASSERT_TRUE(count >= 30);
    std::printf("\nSpec v2.4 C++ Compatibility Results: %d total (%d valid passed, %d rejection passed)\n",
                count, passed_valid, passed_rejected);
}

int main() {
    std::printf("--- Impulse C++ Core Engine Spec v2.4 Test Battery ---\n\n");
    test_all_30_spec_v2_4_test_vectors();
    return 0;
}
