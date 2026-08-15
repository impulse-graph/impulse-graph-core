/**
 * @file main.cpp
 * @brief Impulse Graph Engine — Example 03: Relationship-Based Access Control (ReBAC)
 *
 * Demonstrates:
 * 1. Loading rbac_snapshot.imps directly via embedded engine path resolution (IMPULSE_DATASETS_DIR).
 * 2. Evaluating multi-domain authorization queries:
 *    User -> assigned_role -> Role -> role_perm -> Permission
 * 3. Fast boolean check via ImpulseVM bitset intersection.
 */

#include "impulse_graph.h"
#include "impulse_vm_fluent.hpp"

#include <iostream>
#include <vector>
#include <string>
#include <chrono>

int main() {
    std::cout << "===============================================================\n";
    std::cout << " Impulse Graph Engine — Example 03: ReBAC Authorization (C++)\n";
    std::cout << "===============================================================\n\n";

    impulse_status_t status = IMPULSE_OK;

    // The engine automatically resolves 'rbac_snapshot.imps' via $IMPULSE_DATASETS_DIR or local dataset paths
    auto* snap = impulse_snapshot_open("rbac_snapshot.imps", &status);

    bool is_temp = false;
    std::string temp_path = "temp_rbac_snapshot.imps";

    if (!snap || status != IMPULSE_OK) {
        std::cout << "[INFO] 'rbac_snapshot.imps' not found in $IMPULSE_DATASETS_DIR or local paths.\n";
        std::cout << "[INFO] Generating fallback local ReBAC snapshot...\n";
        is_temp = true;

        impulse_writer_t* writer = impulse_writer_create(temp_path.c_str(), 0);
        impulse_writer_add_domain(writer, 0, IMPULSE_KEY_TYPE_STRING, "User");
        impulse_writer_add_domain(writer, 1, IMPULSE_KEY_TYPE_STRING, "Role");
        impulse_writer_add_domain(writer, 2, IMPULSE_KEY_TYPE_STRING, "Permission");

        // Relation 0: User -> Role (User 0 is Admin(0) and Editor(1))
        std::vector<uint32_t> u_r_off = {0, 2, 3, 4};
        std::vector<uint32_t> u_r_tgt = {0, 1,  1,  2};
        impulse_writer_add_relation(writer, 0, 1, IMPULSE_ENC_RAW, 3, 4, 0,
                                    u_r_off.data(), u_r_off.size() * sizeof(uint32_t),
                                    u_r_tgt.data(), u_r_tgt.size() * sizeof(uint32_t));

        // Relation 1: Role -> Permission (Admin(0) -> [Read(0), Write(1), Delete(2)])
        std::vector<uint32_t> r_p_off = {0, 3, 4, 5};
        std::vector<uint32_t> r_p_tgt = {0, 1, 2,  0,  0};
        impulse_writer_add_relation(writer, 1, 2, IMPULSE_ENC_RAW, 3, 5, 0,
                                    r_p_off.data(), r_p_off.size() * sizeof(uint32_t),
                                    r_p_tgt.data(), r_p_tgt.size() * sizeof(uint32_t));

        impulse_writer_finalize(writer);
        impulse_writer_destroy(writer);

        snap = impulse_snapshot_open(temp_path.c_str(), &status);
        if (!snap || status != IMPULSE_OK) {
            std::cerr << "Failed to open snapshot: " << status << "\n";
            return 1;
        }
    } else {
        std::cout << "[INFO] Successfully resolved and opened 'rbac_snapshot.imps'.\n";
    }

    std::cout << "\n1. ReBAC Policy: Check if User(0) has Permission(2) (e.g. 'DELETE')\n";

    using namespace impulse::vm;

    // Build ReBAC reachability query: User(0) -> assigned_role(0) -> role_perm(1)
    QueryBuilder builder;
    CompiledQuery query = builder
        .inputNode(0)            // R0: Seed User 0
        .walkEdge(0)             // R1: Roles assigned to User 0
        .walkEdge(1)             // R2: Permissions granted by those Roles
        .collectBitset()         // R3: Destination Permissions Bitset
        .compile();

    auto t0 = std::chrono::high_resolution_clock::now();
    QueryResult result = query.execute(snap, /*input_param=*/0);
    auto t1 = std::chrono::high_resolution_clock::now();

    auto elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();

    if (!result.isOk()) {
        std::cerr << "ReBAC query execution failed\n";
        impulse_snapshot_close(snap);
        if (is_temp) std::remove(temp_path.c_str());
        return 1;
    }

    std::cout << "   -> ReBAC Evaluation Latency: " << elapsed_ns << " ns\n";

    // Check permissions
    std::cout << "\n2. Effective Permissions for User 0:\n";
    const char* perm_names[] = {"READ (0)", "WRITE (1)", "DELETE (2)"};
    for (uint64_t p = 0; p < 3; ++p) {
        bool allowed = (result.raw_value & (1ULL << p)) != 0;
        std::cout << "   -> " << perm_names[p] << ": " 
                  << (allowed ? "ALLOWED [✓]" : "DENIED  [✗]") << "\n";
    }

    impulse_snapshot_close(snap);
    if (is_temp) std::remove(temp_path.c_str());

    std::cout << "\n[SUCCESS] Example 03 completed cleanly.\n";
    return 0;
}
