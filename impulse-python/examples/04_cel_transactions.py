#!/usr/bin/env python3
"""
Impulse Graph Engine — Example 04: Financial Transactions & Zero-Copy NumPy (Python)

Demonstrates:
1. Loading financial_transactions.imps directly via embedded engine path resolution (IMPULSE_DATASETS_DIR).
2. Direct zero-copy memoryviews into NumPy arrays for topology and attributes.
3. High-speed vectorized NumPy degree calculations and multi-hop traversal.
"""

import os
import numpy as np
from impulse_graph import Snapshot, Writer

def main():
    print("=" * 70)
    print(" Impulse Graph Engine — Example 04: Transactions & Zero-Copy NumPy (Python)")
    print("=" * 70 + "\n")

    # The engine automatically resolves 'financial_transactions.imps' via $IMPULSE_DATASETS_DIR or local dataset paths
    is_temp = False
    temp_path = "temp_transactions.imps"

    try:
        snap = Snapshot("financial_transactions.imps")
        print("[INFO] Successfully resolved and opened 'financial_transactions.imps'.")
    except Exception:
        print("[INFO] 'financial_transactions.imps' not found in $IMPULSE_DATASETS_DIR or local paths.")
        print("[INFO] Generating sample transaction snapshot...")
        is_temp = True

        with Writer(temp_path) as writer:
            writer.add_domain(0, 1, "Account")
            
            # 5 Accounts, 6 Transfer Edges
            # Account 0 -> [1, 2, 3]
            # Account 1 -> [3]
            # Account 2 -> [0, 4]
            writer.add_relation(
                src_domain_id=0, tgt_domain_id=0, encoding_type=0,
                node_count=5, edge_count=6, section_features=0,
                row_offsets=[0, 3, 4, 6, 6, 6],
                col_indices=[1, 2, 3, 3, 0, 4]
            )
            writer.finalize()
        snap = Snapshot(temp_path)

    with snap:
        # ---------------------------------------------------------------------
        # Step 1: Zero-Copy NumPy Array Extraction
        # ---------------------------------------------------------------------
        print("\n1. Extracting Zero-Copy NumPy Arrays from Memory-Mapped Snapshot:")
        row_offsets = snap.get_row_offsets_array(0)
        col_indices = snap.get_col_indices_array(0)

        print(f"   -> Row Offsets Array (dtype={row_offsets.dtype}, shape={row_offsets.shape}): {row_offsets}")
        print(f"   -> Col Indices Array (dtype={col_indices.dtype}, shape={col_indices.shape}): {col_indices}")
        print(f"   -> NumPy memory flags: OWNDATA = {col_indices.flags.owndata} (Zero-Copy Off-Heap Buffer!)")

        # ---------------------------------------------------------------------
        # Step 2: High-Speed NumPy Degree Analysis
        # ---------------------------------------------------------------------
        print("\n2. Vectorized Degree Calculations via NumPy Slicing:")
        degrees = np.diff(row_offsets)
        print(f"   -> Account Out-Degrees: {degrees.tolist()}")
        print(f"   -> Max Degree Account:  Account {np.argmax(degrees)} ({np.max(degrees)} outgoing transfers)")
        print(f"   -> Total Graph Edges:   {np.sum(degrees)}")

        # ---------------------------------------------------------------------
        # Step 3: Traversal Query Execution
        # ---------------------------------------------------------------------
        print("\n3. Executing Multi-Hop Transfer Traversal:")
        traversal = snap.traverse(start_node=0).out(0)
        recipients = traversal.to_list()
        print(f"   -> Immediate Transfer Recipients from Account 0: {recipients}")

    if is_temp and os.path.exists(temp_path):
        os.remove(temp_path)

    print("\n[SUCCESS] Example 04 completed cleanly.")

if __name__ == "__main__":
    main()
