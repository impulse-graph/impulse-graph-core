#!/usr/bin/env python3
"""
Impulse Graph Engine — Example 03: Relationship-Based Access Control (ReBAC) (Python)

Demonstrates:
1. Loading rbac_snapshot.imps directly via embedded engine path resolution (IMPULSE_DATASETS_DIR).
2. Multi-domain authorization query:
   User -> assigned_role -> Role -> role_perm -> Permission
3. Evaluating effective permissions for a user via ImpulseVM bitsets.
"""

import os
import time

from impulse_graph import Snapshot, Writer, vm


def main():
    print("=" * 65)
    print(" Impulse Graph Engine — Example 03: ReBAC Authorization (Python)")
    print("=" * 65 + "\n")

    # The engine automatically resolves 'rbac_snapshot.imps' via $IMPULSE_DATASETS_DIR or local dataset paths
    is_temp = False
    temp_path = "temp_rbac_snapshot.imps"

    try:
        snap = Snapshot("rbac_snapshot.imps")
        print("[INFO] Successfully resolved and opened 'rbac_snapshot.imps'.")
    except Exception:
        print("[INFO] 'rbac_snapshot.imps' not found in $IMPULSE_DATASETS_DIR or local paths.")
        print("[INFO] Generating fallback ReBAC snapshot...")
        is_temp = True

        with Writer(temp_path) as writer:
            writer.add_domain(0, 1, "User")
            writer.add_domain(1, 1, "Role")
            writer.add_domain(2, 1, "Permission")

            # Relation 0: User -> Role (User 0 is Admin(0) and Editor(1))
            writer.add_relation(
                src_domain_id=0,
                tgt_domain_id=1,
                encoding_type=0,
                node_count=3,
                edge_count=4,
                section_features=0,
                row_offsets=[0, 2, 3, 4],
                col_indices=[0, 1, 1, 2],
            )

            # Relation 1: Role -> Permission (Admin(0) -> [Read(0), Write(1), Delete(2)])
            writer.add_relation(
                src_domain_id=1,
                tgt_domain_id=2,
                encoding_type=0,
                node_count=3,
                edge_count=5,
                section_features=0,
                row_offsets=[0, 3, 4, 5],
                col_indices=[0, 1, 2, 0, 0],
            )
            writer.finalize()
        snap = Snapshot(temp_path)

    with snap:
        # ---------------------------------------------------------------------
        # Step 1: ReBAC Multi-Hop Query Compilation
        # ---------------------------------------------------------------------
        print("\n1. ReBAC Policy: Check permissions for User 0 (User -> Role -> Permission):")

        builder = vm.QueryBuilder()
        query = (
            builder.input_node(0)  # Seed User 0
            .walk_edge(relation_id=0)  # Walk User -> Role
            .walk_edge(relation_id=1)  # Walk Role -> Permission
            .collect_bitset()  # Output effective permissions bitset
            .compile()
        )

        # ---------------------------------------------------------------------
        # Step 2: Query Execution on Snapshot
        # ---------------------------------------------------------------------
        t0 = time.perf_counter_ns()
        result = snap.execute_query(query, input_param=0)
        elapsed_ns = time.perf_counter_ns() - t0

        if not result.is_ok():
            print(f"[ERROR] Query execution failed: {result.status}")
            return

        print(f"   -> ReBAC Evaluation Latency: {elapsed_ns} ns")
        print(f"   -> Permissions Bitset: 0x{result.raw_value:X}")

        perm_labels = ["READ (0)", "WRITE (1)", "DELETE (2)"]
        print("\n2. Effective Permissions Evaluation for User 0:")
        for idx, label in enumerate(perm_labels):
            allowed = (result.raw_value & (1 << idx)) != 0
            status_str = "ALLOWED [✓]" if allowed else "DENIED  [✗]"
            print(f"   -> Permission {label}: {status_str}")

    if is_temp and os.path.exists(temp_path):
        os.remove(temp_path)

    print("\n[SUCCESS] Example 03 completed cleanly.")


if __name__ == "__main__":
    main()
