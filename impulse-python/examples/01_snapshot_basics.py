#!/usr/bin/env python3
"""
Impulse Graph Engine — Example 01: Snapshot Basics (Python)

Demonstrates:
1. Programmatic creation of an immutable binary snapshot (.imps) using Writer.
2. Opening the snapshot via zero-copy memory mapping with Snapshot.
3. Inspecting domains, relations, degrees, and point reachability.
"""

import os
import numpy as np
from impulse_graph import Writer, Snapshot

def main():
    print("=" * 65)
    print(" Impulse Graph Engine — Example 01: Snapshot Basics (Python)")
    print("=" * 65 + "\n")

    snapshot_path = "sample_basics.imps"

    # -------------------------------------------------------------------------
    # Step 1: Create a Snapshot using Writer
    # -------------------------------------------------------------------------
    print(f"1. Creating binary snapshot: {snapshot_path}...")
    
    with Writer(snapshot_path) as writer:
        writer.add_domain(domain_id=0, key_type=4, name="User") # Key Type: INT64

        # Topology (CSR): 4 Users (0, 1, 2, 3)
        # Node 0 -> [1, 2]
        # Node 1 -> [2, 3]
        # Node 2 -> [3]
        # Node 3 -> []
        row_offsets = [0, 2, 4, 5, 5]
        col_indices = [1, 2, 2, 3, 3]

        writer.add_relation(
            src_domain_id=0,
            tgt_domain_id=0,
            encoding_type=0, # RAW CSR
            node_count=4,
            edge_count=5,
            section_features=0,
            row_offsets=row_offsets,
            col_indices=col_indices,
        )
        writer.finalize()

    print(f"   -> Successfully wrote snapshot ({os.path.getsize(snapshot_path)} bytes).\n")

    # -------------------------------------------------------------------------
    # Step 2: Open Snapshot via zero-copy mmap
    # -------------------------------------------------------------------------
    print("2. Opening snapshot via zero-copy mmap...")
    with Snapshot(snapshot_path) as snap:
        print(f"   -> Domains:   {snap.domain_count()}")
        print(f"   -> Relations: {snap.relation_count()}\n")

        # ---------------------------------------------------------------------
        # Step 3: Inspect Relation Directory & Degrees
        # ---------------------------------------------------------------------
        print("3. Inspecting relation 0 topology:")
        rel_info = snap.get_relation(0)
        print(f"   -> Node Count: {rel_info.get('node_count', 4)}")
        print(f"   -> Edge Count: {rel_info.get('edge_count', 5)}")

        row_arr = snap.get_row_offsets_array(0)
        col_arr = snap.get_col_indices_array(0)
        for node_id in range(rel_info.get("node_count", 4)):
            start = row_arr[node_id]
            end = row_arr[node_id + 1]
            neighbors = col_arr[start:end]
            print(f"   -> Node {node_id} out-degree: {len(neighbors)} edges (neighbors: {neighbors.tolist()})")

        print("\n4. Direct Point Reachability Queries:")
        print(f"   -> Node 0 -> Node 1 reachable? {snap.is_reachable(0, 0, 1)}")
        print(f"   -> Node 0 -> Node 3 direct reachable? {snap.is_reachable(0, 0, 3)}")

    # Cleanup temporary snapshot
    if os.path.exists(snapshot_path):
        os.remove(snapshot_path)

    print("\n[SUCCESS] Example 01 completed cleanly.")

if __name__ == "__main__":
    main()
