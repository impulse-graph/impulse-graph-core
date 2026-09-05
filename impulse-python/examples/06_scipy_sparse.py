#!/usr/bin/env python3
"""
Impulse Graph Engine — Example 06: SciPy Sparse & GraphBLAS Interop
Demonstrates using .imps snapshots as an off-heap storage format for SciPy sparse matrices,
alongside side-by-side ImpulseVM ImpK matrix-vector equivalence.
"""

import time

import numpy as np
import scipy.sparse as sp
from scipy.sparse.csgraph import connected_components, dijkstra

from impulse_graph import Snapshot, Writer, vm


def main():
    print("===============================================================")
    print(" Impulse Graph Engine — Example 06: SciPy Sparse Matrix Interop")
    print("===============================================================\n")

    # ------------------------------------------------------------------------
    # Step 1: Compiling SciPy Matrix into .imps via Writer.from_scipy
    # ------------------------------------------------------------------------
    print("1. Compiling SciPy CSR Matrix into .imps snapshot:")
    # Build a directed graph with 6 nodes
    adj_data = np.ones(8, dtype=np.float32)
    adj_indices = np.array([1, 2, 2, 3, 3, 4, 4, 5], dtype=np.uint32)
    adj_indptr = np.array([0, 2, 4, 6, 7, 8, 8], dtype=np.uint32)
    scipy_mat = sp.csr_matrix((adj_data, adj_indices, adj_indptr), shape=(6, 6))

    snapshot_path = "temp_scipy_graph.imps"
    Writer.from_scipy(snapshot_path, scipy_mat, domain_name="Node")
    print(f"   -> Successfully compiled {scipy_mat.shape} matrix into '{snapshot_path}'.")

    # ------------------------------------------------------------------------
    # Step 2: Zero-Copy Reading into SciPy
    # ------------------------------------------------------------------------
    print("\n2. Zero-Copy Loading Snapshot into SciPy CSR Matrix:")
    with Snapshot(snapshot_path) as snap:
        t0 = time.perf_counter()
        mat = snap.to_scipy_csr(relation_index=0)
        t_load = (time.perf_counter() - t0) * 1e6

        print(f"   -> Loading Latency:    {t_load:.2f} µs")
        print(f"   -> Matrix Shape:       {mat.shape}")
        print(f"   -> Non-Zero Elements:  {mat.nnz}")

        # --------------------------------------------------------------------
        # Step 3: Running SciPy Graph Algorithms
        # --------------------------------------------------------------------
        print("\n3. Executing SciPy csgraph Algorithms:")
        t0 = time.perf_counter()
        dist_matrix = dijkstra(mat, directed=True, indices=0)
        t_dijk = (time.perf_counter() - t0) * 1e6

        n_comp, labels = connected_components(mat, directed=False)

        print(f"   -> Dijkstra Latency:   {t_dijk:.2f} µs")
        print(f"   -> Distances from 0:   {dist_matrix.tolist()}")
        print(f"   -> Connected Comps:    {n_comp} components (labels: {labels.tolist()})")

        # --------------------------------------------------------------------
        # Step 4: Side-by-Side ImpulseVM ImpK Matrix Math Equivalence
        # --------------------------------------------------------------------
        print("\n4. Side-by-Side ImpulseVM Matrix-Vector Equivalence:")
        print("   SciPy Approach:     sp.csr_matrix.dot(x) in Python runtime")
        print("   ImpulseVM 1-Liner:  OP_MXV GraphBLAS kernel executed directly in C++ SIMD")

        # Compile VM query for 1-hop matrix-vector frontier expansion
        query = vm.QueryBuilder().input_node(0).walk_edge(0, 1).collect_bitset().compile()

        t0 = time.perf_counter()
        res = snap.execute_query(query, input_param=0)
        t_vm = (time.perf_counter() - t0) * 1e6

        print(f"   -> ImpulseVM Latency:  {t_vm:.2f} µs")
        print(f"   -> Execution Status:   {'OK' if res.is_ok() else 'ERROR'}")

    print("\n[SUCCESS] Example 06 completed cleanly.")


if __name__ == "__main__":
    main()
