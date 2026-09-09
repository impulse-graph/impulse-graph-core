#!/usr/bin/env python3
"""
Impulse Graph Engine — Example 02: Social Graph Traversal (Python)

Demonstrates:
1. Loading social_graph.imps directly via embedded engine path resolution (IMPULSE_DATASETS_DIR).
2. Constructing multi-hop traversal bytecode using vm.QueryBuilder.
3. Executing against ImpulseVM and extracting destination node arrays.
"""

import os
import time

from impulse_graph import Snapshot, Writer, vm


def main():
    print("=" * 65)
    print(" Impulse Graph Engine — Example 02: Social Graph Traversal (Python)")
    print("=" * 65 + "\n")

    # The engine automatically resolves 'social_graph.imps' via $IMPULSE_DATASETS_DIR or local dataset paths
    is_temp = False
    temp_path = "temp_social_graph.imps"

    try:
        snap = Snapshot("social_graph.imps")
        print("[INFO] Successfully resolved and opened 'social_graph.imps'.")
    except Exception:
        print("[INFO] 'social_graph.imps' not found in $IMPULSE_DATASETS_DIR or local paths.")
        print("[INFO] Generating fallback in-memory social graph...")
        is_temp = True

        with Writer(temp_path) as writer:
            writer.add_domain(domain_id=0, key_type=4, name="User")

            # 8 Users with follow relations
            row_offsets = [0, 2, 4, 6, 8, 9, 10, 11, 11]
            col_indices = [1, 2, 2, 3, 3, 4, 4, 5, 6, 7, 0]

            writer.add_relation(
                src_domain_id=0,
                tgt_domain_id=0,
                encoding_type=0,
                node_count=8,
                edge_count=11,
                section_features=0,
                row_offsets=row_offsets,
                col_indices=col_indices,
            )
            writer.finalize()
        snap = Snapshot(temp_path)

    with snap:
        # ---------------------------------------------------------------------
        # Step 1: Construct Fluent VM Query Plan
        # ---------------------------------------------------------------------
        print("\n1. Constructing Fluent ImpulseVM Query Plan:")
        print("   Query: Seed(User 0) -> Walk(follows) -> Walk(follows) -> CollectBitset()")

        builder = vm.QueryBuilder()
        query = (
            builder.input_node(0)  # Seed User 0
            .walk_edge(relation_id=0)  # 1-hop friends
            .walk_edge(relation_id=0)  # 2-hop friends-of-friends
            .collect_bitset()  # Collect destination bitset
            .compile()
        )

        print(f"   -> Generated {query.instruction_count()} impOps bytecode instructions.")

        # ---------------------------------------------------------------------
        # Step 2: Execute Query on ImpulseVM
        # ---------------------------------------------------------------------
        print("\n2. Executing Query against ImpulseVM:")
        t0 = time.perf_counter_ns()
        result = snap.execute_query(query, input_param=0)
        elapsed_ns = time.perf_counter_ns() - t0

        if not result.is_ok():
            print(f"[ERROR] Execution failed with status code: {result.status}")
            return

        print(f"   -> Execution Time: {elapsed_ns / 1000.0:.2f} µs")
        print(f"   -> Destination Register: R{result.result_register}")
        print(f"   -> Raw Bitset Value: 0x{result.raw_value:X}")

        # ---------------------------------------------------------------------
        # Step 3: Traversal API Verification
        # ---------------------------------------------------------------------
        print("\n3. Inspecting Reachability via Friendly Traversal API:")
        traversal = snap.traverse(start_node=0).out(0)
        hop1_targets = traversal.to_list()
        print(f"   -> 1-Hop Neighbors of User 0: {hop1_targets}")

    if is_temp and os.path.exists(temp_path):
        os.remove(temp_path)

    print("\n[SUCCESS] Example 02 completed cleanly.")


if __name__ == "__main__":
    main()
