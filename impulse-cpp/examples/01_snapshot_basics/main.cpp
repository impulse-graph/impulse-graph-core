/**
 * @file main.cpp
 * @brief Impulse Graph Engine — Example 01: Snapshot Creation & Zero-Copy Reading
 *
 * Demonstrates:
 * 1. Creating a binary snapshot (.imps) programmatically via impulse_writer.
 * 2. Opening the snapshot with zero-copy OS memory mapping (mmap).
 * 3. Inspecting domain metadata, node counts, and CSR topology.
 */

#include "impulse_graph.h"

#include <iostream>
#include <vector>
#include <string>
#include <filesystem>
#include <cstdio>

int main() {
    std::cout << "===============================================================\n";
    std::cout << " Impulse Graph Engine — Example 01: Snapshot Basics (C++)\n";
    std::cout << "===============================================================\n\n";

    const std::string snapshot_path = "sample_basics.imps";

    // ------------------------------------------------------------------------
    // Step 1: Programmatic Snapshot Creation with impulse_writer
    // ------------------------------------------------------------------------
    std::cout << "1. Creating binary snapshot file: " << snapshot_path << "...\n";

    impulse_writer_t* writer = impulse_writer_create(snapshot_path.c_str(), 0);
    if (!writer) {
        std::cerr << "Failed to create writer for " << snapshot_path << "\n";
        return 1;
    }

    // Define Domain 0: "User" (Key Type: INT64)
    impulse_writer_add_domain(writer, 0, IMPULSE_KEY_TYPE_INT64, "User");

    // Define Graph Topology (CSR): 4 Users (0, 1, 2, 3)
    // Node 0 -> [1, 2] (User 0 follows 1 and 2)
    // Node 1 -> [2, 3] (User 1 follows 2 and 3)
    // Node 2 -> [3]    (User 2 follows 3)
    // Node 3 -> []     (User 3 has no outgoing edges)
    std::vector<uint32_t> row_offsets = {0, 2, 4, 5, 5};
    std::vector<uint32_t> col_indices = {1, 2, 2, 3, 3};

    impulse_status_t status = impulse_writer_add_relation(
        writer,
        /*src_domain=*/0,
        /*tgt_domain=*/0,
        /*encoding_type=*/IMPULSE_ENC_RAW,
        /*node_count=*/4,
        /*edge_count=*/5,
        /*section_features=*/0,
        row_offsets.data(), row_offsets.size() * sizeof(uint32_t),
        col_indices.data(), col_indices.size() * sizeof(uint32_t)
    );

    if (status != IMPULSE_OK) {
        std::cerr << "Failed to add relation: " << status << "\n";
        impulse_writer_destroy(writer);
        return 1;
    }

    status = impulse_writer_finalize(writer);
    impulse_writer_destroy(writer);

    if (status != IMPULSE_OK) {
        std::cerr << "Failed to finalize snapshot: " << status << "\n";
        return 1;
    }

    std::cout << "   -> Successfully wrote snapshot (" 
              << std::filesystem::file_size(snapshot_path) << " bytes).\n\n";

    // ------------------------------------------------------------------------
    // Step 2: Zero-Copy Memory-Mapped Reading with impulse_snapshot
    // ------------------------------------------------------------------------
    std::cout << "2. Opening snapshot via zero-copy mmap...\n";
    impulse_snapshot_t* snap = impulse_snapshot_open(snapshot_path.c_str(), &status);
    if (!snap || status != IMPULSE_OK) {
        std::cerr << "Failed to open snapshot: " << status << "\n";
        std::remove(snapshot_path.c_str());
        return 1;
    }

    std::cout << "   -> Magic:     0x" << std::hex << impulse_snapshot_magic(snap) << std::dec << " ('IMPS')\n";
    std::cout << "   -> Version:   " << impulse_snapshot_version(snap) << "\n";
    std::cout << "   -> Domains:   " << impulse_snapshot_domain_count(snap) << "\n";
    std::cout << "   -> Relations: " << impulse_snapshot_relation_count(snap) << "\n\n";

    // ------------------------------------------------------------------------
    // Step 3: Inspect Relation Directory & Point Reachability
    // ------------------------------------------------------------------------
    std::cout << "3. Inspecting relation 0 topology:\n";
    impulse_relation_directory_entry_t rel_entry{};
    if (impulse_snapshot_get_relation_entry(snap, 0, &rel_entry) == IMPULSE_OK) {
        std::cout << "   -> Node Count: " << rel_entry.node_count << "\n";
        std::cout << "   -> Edge Count: " << rel_entry.edge_count << "\n";
    }

    const uint32_t* offsets = nullptr;
    const uint32_t* targets = nullptr;
    uint64_t node_count = 0, edge_count = 0;
    if (impulse_snapshot_get_relation_buffers(snap, 0, &offsets, &targets, &node_count, &edge_count) == IMPULSE_OK) {
        for (uint64_t node = 0; node < node_count; ++node) {
            uint32_t deg = offsets[node + 1] - offsets[node];
            std::cout << "   -> Node " << node << " out-degree: " << deg << " edges (";
            for (uint32_t i = offsets[node]; i < offsets[node + 1]; ++i) {
                std::cout << targets[i] << (i + 1 < offsets[node + 1] ? ", " : "");
            }
            std::cout << ")\n";
        }
    }

    std::cout << "\n4. Direct Point Reachability Queries:\n";
    std::cout << "   -> Node 0 -> Node 1 reachable? " 
              << (impulse_snapshot_is_reachable(snap, 0, 0, 1) ? "YES" : "NO") << "\n";
    std::cout << "   -> Node 0 -> Node 3 reachable? " 
              << (impulse_snapshot_is_reachable(snap, 0, 0, 3) ? "YES (direct)" : "NO (multi-hop path)") << "\n";

    // Cleanup
    impulse_snapshot_close(snap);
    std::remove(snapshot_path.c_str());
    std::cout << "\n[SUCCESS] Example 01 completed cleanly.\n";
    return 0;
}
