/*
 * test_vector_suite.cpp — Spec v0.9.0 Test Suite for Impulse C++ Core Engine.
 */

#include "impulse_graph.h"
#include "impulse_format_v0_9.h"

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <iostream>
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

static void test_all_30_spec_v0_9_test_vectors() {
    fs::path spec_dir("/Users/jesse/impulse/impulse-graph-spec/test-vectors");
    if (!fs::exists(spec_dir)) {
        spec_dir = "../../impulse-graph-spec/test-vectors";
    }
    if (!fs::exists(spec_dir)) {
        std::cout << "[SKIP] test-vectors directory not found at " << spec_dir << "\n";
        return;
    }

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
        if (manifest_content.find("\"spec_version\": \"0.9.0\"") == std::string::npos) {
            continue;
        }

        bool is_rejection = manifest_content.find("\"REJECT_") != std::string::npos ||
                           manifest_content.find("\"corrupt_") != std::string::npos ||
                           (manifest_content.find("\"SUCCESS\"") == std::string::npos &&
                            manifest_content.find("\"IMPULSE_VM_OK\"") == std::string::npos);

        impulse_status_t status = IMPULSE_OK;
        impulse_snapshot_t* snap = impulse_snapshot_open(imps_file.string().c_str(), &status);

        if (is_rejection) {
            if (snap != nullptr || status == IMPULSE_OK) {
                std::fprintf(stderr, "FAIL: Vector %s should be REJECTED, but open succeeded! (snap=%p, status=%d)\n", folder_name.c_str(), (void*)snap, status);
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
            ASSERT_EQ(magic, IMPULSE_MAGIC);
            ASSERT_TRUE(version == IMPULSE_SPEC_VERSION_PACKED || version == 9 || version == 2 || version == 0x0204);

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

    ASSERT_TRUE(count >= 5);
    std::printf("\nSpec v0.9.0 C++ Compatibility Results: %d total (%d valid passed, %d rejection passed)\n",
                count, passed_valid, passed_rejected);
}

int main() {
    std::printf("--- Impulse C++ Core Engine Spec v0.9.0 Test Battery ---\n\n");
    test_all_30_spec_v0_9_test_vectors();
    return 0;
}
