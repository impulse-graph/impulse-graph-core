/**
 * @file main.cpp
 * @brief Impulse Graph Engine — Example 02: Multi-Hop Social Graph Traversal
 *
 * Demonstrates:
 * 1. Loading social_graph.imps directly via embedded engine path resolution (IMPULSE_DATASETS_DIR).
 * 2. Building a 2-hop traversal pipeline via QueryBuilder.
 * 3. Executing the query against ImpulseVM and inspecting reachable bitsets.
 */

#include "impulse_graph.h"
#include "impulse_vm_fluent.hpp"

#include <iostream>
#include <vector>
#include <string>
#include <chrono>

int main() {
    std::cout << "===============================================================\n";
    std::cout << " Impulse Graph Engine — Example 02: Social Graph Traversal (C++)\n";
    std::cout << "===============================================================\n\n";

    impulse_status_t status = IMPULSE_OK;
    
    // The engine automatically resolves 'social_graph.imps' via $IMPULSE_DATASETS_DIR or local dataset paths
    auto* snap = impulse_snapshot_open("social_graph.imps", &status);

    bool is_temp = false;
    std::string temp_path = "temp_social_graph.imps";

    if (!snap || status != IMPULSE_OK) {
        std::cout << "[INFO] 'social_graph.imps' not found in $IMPULSE_DATASETS_DIR or local paths.\n";
        std::cout << "[INFO] Generating fallback in-memory sample social graph...\n";
        is_temp = true;

        impulse_writer_t* writer = impulse_writer_create(temp_path.c_str(), 0);
        impulse_writer_add_domain(writer, 0, IMPULSE_KEY_TYPE_INT64, "User");

        // 8 Users with follow relations
        std::vector<uint32_t> row_offsets = {0, 2, 4, 6, 8, 9, 10, 11, 11};
        std::vector<uint32_t> col_indices = {1, 2,  2, 3,  3, 4,  4, 5,  6, 7, 0};
        impulse_writer_add_relation(writer, 0, 0, IMPULSE_ENC_RAW, 8, 11, 0,
                                    row_offsets.data(), row_offsets.size() * sizeof(uint32_t),
                                    col_indices.data(), col_indices.size() * sizeof(uint32_t));
        impulse_writer_finalize(writer);
        impulse_writer_destroy(writer);

        snap = impulse_snapshot_open(temp_path.c_str(), &status);
        if (!snap || status != IMPULSE_OK) {
            std::cerr << "Failed to open snapshot: " << status << "\n";
            return 1;
        }
    } else {
        std::cout << "[INFO] Successfully resolved and opened 'social_graph.imps'.\n";
    }

    std::cout << "\n1. Constructing Fluent ImpulseVM Query Plan:\n";
    std::cout << "   Query: Seed(User 0) -> Walk(follows) -> Walk(follows) -> CollectBitset()\n";

    using namespace impulse::vm;

    QueryBuilder builder;
    CompiledQuery query = builder
        .inputNode(0)            // R0: Seed node ID 0
        .walkEdge(0)             // R1: 1-hop friends
        .walkEdge(0)             // R2: 2-hop friends-of-friends
        .collectBitset()         // R3: Collect destination frontier
        .compile();

    std::cout << "   -> Generated " << query.instructionCount() 
              << " impOps bytecode instructions.\n";

    std::cout << "\n2. Executing Query against ImpulseVM:\n";
    auto t0 = std::chrono::high_resolution_clock::now();
    QueryResult result = query.execute(snap, /*input_param=*/0);
    auto t1 = std::chrono::high_resolution_clock::now();

    auto elapsed_us = std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count();

    if (!result.isOk()) {
        std::cerr << "Query execution failed with status: " << result.status << "\n";
        impulse_snapshot_close(snap);
        if (is_temp) std::remove(temp_path.c_str());
        return 1;
    }

    std::cout << "   -> Execution Time: " << elapsed_us << " µs\n";
    std::cout << "   -> Destination Bitset Register: R" << result.result_register << "\n";

    // Inspect reachable nodes
    std::cout << "\n3. Traversal Results (2-hop Friends-of-Friends from User 0):\n";
    uint64_t node_count = impulse_snapshot_relation_count(snap) > 0 ? 8 : 4;
    for (uint64_t u = 0; u < node_count; ++u) {
        std::cout << "   -> User " << u << ": reachable = " 
                  << (result.raw_value & (1ULL << u) ? "YES" : "NO") << "\n";
    }

    impulse_snapshot_close(snap);
    if (is_temp) std::remove(temp_path.c_str());

    std::cout << "\n[SUCCESS] Example 02 completed cleanly.\n";
    return 0;
}
