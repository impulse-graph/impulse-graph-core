#!/usr/bin/env python3
"""
Impulse Graph Engine — Example 05: PyTorch & PyG Zero-Copy Interop
Demonstrates using .imps snapshots as a high-performance off-heap storage format for PyTorch & PyG,
alongside side-by-side ImpulseVM equivalence snippets.
"""

import time

import torch

from impulse_graph import Snapshot, Writer


def main():
    print("===============================================================")
    print(" Impulse Graph Engine — Example 05: PyTorch & PyG Interop")
    print("===============================================================\n")

    snapshot_path = "social_graph.imps"
    try:
        snap = Snapshot(snapshot_path)
        print(f"[INFO] Successfully opened '{snapshot_path}'.")
    except Exception:
        print(f"[INFO] '{snapshot_path}' not found in $IMPULSE_DATASETS_DIR.")
        print("[INFO] Generating sample social graph via Writer.from_torch()...")

        # Create a sample PyTorch COO edge_index
        sample_edge_index = torch.tensor(
            [[0, 0, 1, 1, 2, 2, 3, 4, 5, 6], [1, 2, 2, 3, 3, 4, 4, 5, 6, 7]], dtype=torch.int64
        )

        snapshot_path = "temp_torch_social.imps"
        Writer.from_torch(snapshot_path, sample_edge_index, num_nodes=8)
        snap = Snapshot(snapshot_path)

    # ------------------------------------------------------------------------
    # Step 1: Zero-Copy PyTorch Sparse CSR Tensor
    # ------------------------------------------------------------------------
    print("\n1. Converting Snapshot CSR Topology to PyTorch Sparse CSR Tensor:")
    t0 = time.perf_counter()
    torch_csr = snap.to_torch_csr(relation_index=0)
    t_csr = (time.perf_counter() - t0) * 1e6

    print(f"   -> Conversion Latency: {t_csr:.2f} µs")
    print(f"   -> Tensor Type:        {type(torch_csr)}")
    print(f"   -> Shape:              {torch_csr.shape}")
    print(f"   -> Is Sparse CSR:      {torch_csr.is_sparse_csr}")

    # ------------------------------------------------------------------------
    # Step 2: PyTorch Geometric (PyG) COO edge_index Tensor
    # ------------------------------------------------------------------------
    print("\n2. Converting to PyTorch Geometric COO edge_index Tensor:")
    t0 = time.perf_counter()
    edge_index = snap.to_torch_edge_index(relation_index=0)
    t_ei = (time.perf_counter() - t0) * 1e6

    print(f"   -> Conversion Latency: {t_ei:.2f} µs")
    print(f"   -> Edge Index Shape:   {edge_index.shape} (2 x {edge_index.shape[1]})")
    print(f"   -> Edges:              {edge_index[:, :5].tolist()} ...")

    # ------------------------------------------------------------------------
    # Step 3: GNN Mini-Batch Neighborhood Sampling
    # ------------------------------------------------------------------------
    print("\n3. Zero-Copy SIMD GNN Neighborhood Sampling:")
    seed_nodes = [0, 1]
    k_samples = 3
    t0 = time.perf_counter()
    src_nodes, tgt_nodes = snap.sample_neighbors(
        relation_index=0, nodes=seed_nodes, k_samples=k_samples
    )
    t_sample = (time.perf_counter() - t0) * 1e6

    print(f"   -> Sampling Latency:   {t_sample:.2f} µs")
    print(f"   -> Seed Nodes:         {seed_nodes}")
    print(f"   -> Sampled Edges:      {list(zip(src_nodes.tolist(), tgt_nodes.tolist()))}")

    # ------------------------------------------------------------------------
    # Step 4: Side-by-Side ImpulseVM Equivalence (1-Liner)
    # ------------------------------------------------------------------------
    print("\n4. Side-by-Side ImpulseVM Traversal Equivalence:")
    print("   PyTorch/PyG Approach: Sample neighbors, materialize subgraphs, execute sparse MM")
    print("   ImpulseVM 1-Liner:    snap.traverse(start_node=0).out(0).out(0).to_list()")

    t0 = time.perf_counter()
    impulse_2hop = snap.traverse(start_node=0).out(0).out(0).to_list()
    t_imp = (time.perf_counter() - t0) * 1e6

    print(f"   -> ImpulseVM Latency:  {t_imp:.2f} µs")
    print(f"   -> 2-Hop Reachable:    {impulse_2hop}")

    snap.close()
    print("\n[SUCCESS] Example 05 completed cleanly.")


if __name__ == "__main__":
    main()
