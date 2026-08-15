#!/usr/bin/env python3
"""
===============================================================================
IMPULSE GRAPH vs HETNETPY (OFFICIAL HETIONET AUTHOR PYTHON ENGINE)
===============================================================================
Compares Impulse Graph against hetnetpy (Daniel Himmelstein et al., eLife 2017)
  1. Cold Start Ingestion & Memory Footprint (Building Graph + Adjacency Tables)
  2. 4-Hop Drug Repurposing Metapath Traversal (DaG - GpPW - GpPW - CbG)
  3. 2-Hop Expression Counteraction Metapath (DdG - CuG)
  4. 2-Hop Chemical Resemblance Metapath (CtD - CrC)
  5. 3-Hop Anatomy Pathology Metapath (DlA - AeG - CbG)
  6. Whole-Cohort All-Diseases Screening Sweep (All 137 Diseases)
===============================================================================
"""

import os
import sys
import time
import resource
import gc
from typing import List, Dict, Set
import numpy as np

# Ensure impulse_graph is in path
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "impulse-python")))
from impulse_graph import Snapshot, vm

import hetnetpy.hetnet as hn
import hetnetpy.matrix as hnm

HETIONET_DIR = "/Users/jesse/impulse/datasets/hetionet"
NODES_PATH = os.path.join(HETIONET_DIR, "nodes.tsv")
EDGES_PATH = os.path.join(HETIONET_DIR, "edges.sif")
IMPS_PATH = os.path.join(HETIONET_DIR, "hetionet.v09.imps")


def get_process_memory_mb() -> float:
    return resource.getrusage(resource.RUSAGE_SELF).ru_maxrss / (1024.0 * 1024.0)


def main():
    print("\n" + "=" * 115)
    print("   HEAD-TO-HEAD BENCHMARK: IMPULSE GRAPH vs HETNETPY (OFFICIAL HETIONET PYTHON ENGINE)")
    print("   Dataset: Hetionet v1.0 (47,031 Nodes | 11 Domains | 24 Metaedges | 2,250,197 Edges)")
    print("=" * 115 + "\n")

    if not os.path.exists(NODES_PATH) or not os.path.exists(EDGES_PATH) or not os.path.exists(IMPS_PATH):
        print(f"[!] Required files not found in {HETIONET_DIR}")
        return

    # -------------------------------------------------------------------------
    # 0. Load Node Mappings
    # -------------------------------------------------------------------------
    node_to_id: Dict[str, int] = {}
    id_to_node: Dict[int, str] = {}
    node_kinds: Dict[str, str] = {}
    node_names: Dict[str, str] = {}
    disease_ids: List[int] = []

    with open(NODES_PATH, "r") as f:
        next(f)
        for idx, line in enumerate(f):
            parts = line.strip().split("\t")
            if len(parts) >= 3:
                nid, name, kind = parts[0], parts[1], parts[2]
                node_to_id[nid] = idx
                id_to_node[idx] = nid
                node_kinds[nid] = kind
                node_names[nid] = name
                if kind == "Disease":
                    disease_ids.append(idx)

    num_nodes = len(node_to_id)
    seed_key = "Disease::DOID:10652" if "Disease::DOID:10652" in node_to_id else id_to_node[disease_ids[0]]
    seed_node = node_to_id[seed_key]
    print(f"[*] Target Seed Disease: {seed_key} (Dense ID: {seed_node})\n")

    # =========================================================================
    # 1. IMPULSE GRAPH: Zero-Copy Off-Heap Snapshot
    # =========================================================================
    print("-" * 115)
    print(" [1] Impulse Graph Engine (Zero-Copy Memory-Mapped .imps Snapshot)")
    print("-" * 115)
    gc.collect()
    mem_before = get_process_memory_mb()
    t0 = time.perf_counter()
    imp_snap = Snapshot(IMPS_PATH)
    t_load_imp = (time.perf_counter() - t0) * 1000.0
    mem_imp = get_process_memory_mb() - mem_before
    print(f"  -> Cold Start / Open Time:       {t_load_imp:8.3f} ms")
    print(f"  -> Physical Memory Delta:        {mem_imp:8.2f} MB")

    # =========================================================================
    # 2. HETNETPY: Build MetaGraph & Graph & Matrix Representations
    # =========================================================================
    print("\n" + "-" * 115)
    print(" [2] hetnetpy Engine (Official Hetionet Metagraph, Object Graph & Sparse Adjacency)")
    print("-" * 115)
    gc.collect()
    mem_before = get_process_memory_mb()
    t0 = time.perf_counter()

    # Build hetnetpy MetaGraph
    metagraph = hn.MetaGraph()
    distinct_kinds = sorted(list(set(node_kinds.values())))
    for k in distinct_kinds:
        metagraph.add_node(k)

    # Ingest edges and infer metaedges
    edges_raw: List[tuple] = []
    metaedge_defs = {}
    with open(EDGES_PATH, "r") as f:
        next(f)
        for line in f:
            src, rel, tgt = line.strip().split("\t")[:3]
            if src in node_kinds and tgt in node_kinds:
                src_k = node_kinds[src]
                tgt_k = node_kinds[tgt]
                metaedge_tuple = (src_k, tgt_k, rel, "both" if src_k == tgt_k and rel in ("CrC", "GiG", "GcG", "Gr>G") else "forward")
                metaedge_defs[metaedge_tuple] = rel
                edges_raw.append((src, rel, tgt, src_k, tgt_k))

    for metaedge_tuple in metaedge_defs.keys():
        try:
            metagraph.add_edge(metaedge_tuple)
        except Exception:
            pass

    # Build hetnetpy Graph
    het_graph = hn.Graph(metagraph)
    for nid, kind in node_kinds.items():
        het_graph.add_node(kind, nid, node_names.get(nid, ""))

    for src, rel, tgt, src_k, tgt_k in edges_raw:
        try:
            het_graph.add_edge((src_k, src), (tgt_k, tgt), rel, "forward")
        except Exception:
            pass

    t_load_hn = (time.perf_counter() - t0) * 1000.0
    mem_hn = get_process_memory_mb() - mem_before
    print(f"  -> Ingestion & Graph Build Time: {t_load_hn:8.3f} ms")
    print(f"  -> Heap RAM Memory Footprint:    {mem_hn:8.2f} MB\n")

    # Build adjacency matrices
    hn_matrices = {}
    for metaedge in metagraph.get_edges():
        try:
            rows, cols, mat = hnm.metaedge_to_adjacency_matrix(het_graph, metaedge, dtype=bool)
            hn_matrices[metaedge.kind] = (rows, cols, mat)
        except Exception:
            pass

    # -------------------------------------------------------------------------
    # Helper to run side-by-side query comparison
    # -------------------------------------------------------------------------
    def compare_query(name: str, metapath: str, imp_query_fn, hn_matrix_fn, runs=2000):
        print("=" * 115)
        print(f" {name}")
        print(f" Metapath: {metapath}")
        print("=" * 115)

        # 1. Impulse Graph (Off-Heap SIMD)
        t_imp = imp_query_fn(imp_snap, seed_node)
        compiled_imp = t_imp.compile()
        ctx = vm.VmContext(imp_snap)
        state = vm.VmState()

        for _ in range(100):
            res = compiled_imp.execute_with_context(ctx, state, seed_node)
            if res.is_ok() and res.result_type == vm.RegisterType.TYPE_BITSET_HANDLE:
                ctx.release_bitset(res.raw_value)

        lats_imp = []
        for _ in range(runs):
            t0 = time.perf_counter_ns()
            res = compiled_imp.execute_with_context(ctx, state, seed_node)
            t1 = time.perf_counter_ns()
            lats_imp.append((t1 - t0) / 1000.0)
            if res.is_ok() and res.result_type == vm.RegisterType.TYPE_BITSET_HANDLE:
                ctx.release_bitset(res.raw_value)

        # 2. hetnetpy Matrix Engine
        for _ in range(100):
            hn_matrix_fn(seed_node)

        lats_hn_mat = []
        for _ in range(runs):
            t0 = time.perf_counter_ns()
            hn_matrix_fn(seed_node)
            t1 = time.perf_counter_ns()
            lats_hn_mat.append((t1 - t0) / 1000.0)

        p50_imp, mean_imp, p99_imp = np.median(lats_imp), np.mean(lats_imp), np.percentile(lats_imp, 99)
        qps_imp = int(1_000_000.0 / mean_imp)

        p50_mat, mean_mat, p99_mat = np.median(lats_hn_mat), np.mean(lats_hn_mat), np.percentile(lats_hn_mat, 99)
        qps_mat = int(1_000_000.0 / mean_mat)

        print(f"{'Engine / Method':<32} | {'P50 (Median)':<14} | {'Mean Latency':<14} | {'P99 Latency':<14} | {'Throughput (QPS)':<16} | {'Speedup'}")
        print("-" * 115)
        print(f"{'Impulse Graph (Zero-Copy VM)':<32} | {p50_imp:10.3f} µs | {mean_imp:10.3f} µs | {p99_imp:10.3f} µs | {qps_imp:12,} QPS | 1.0x (BASELINE)")
        print(f"{'hetnetpy (Matrix Operations)':<32} | {p50_mat:10.3f} µs | {mean_mat:10.3f} µs | {p99_mat:10.3f} µs | {qps_mat:12,} QPS | {mean_mat/mean_imp:8.1f}x slower")
        print("-" * 115 + "\n")
        ctx.destroy()

    # Pre-extract matrices
    M_dag = hn_matrices.get("DaG", (None, None, None))[2]
    M_gppw = hn_matrices.get("GpPW", (None, None, None))[2]
    M_cbg = hn_matrices.get("CbG", (None, None, None))[2]
    M_ddg = hn_matrices.get("DdG", (None, None, None))[2]
    M_cug = hn_matrices.get("CuG", (None, None, None))[2]
    M_ctd = hn_matrices.get("CtD", (None, None, None))[2]
    M_crc = hn_matrices.get("CrC", (None, None, None))[2]
    M_dla = hn_matrices.get("DlA", (None, None, None))[2]
    M_aeg = hn_matrices.get("AeG", (None, None, None))[2]

    # Map seed disease to row index in M_dag
    rows_dag = hn_matrices.get("DaG", ([],))[0]
    disease_node_obj = het_graph.node_dict.get(("Disease", seed_key))
    seed_dag_idx = rows_dag.index(disease_node_obj) if disease_node_obj in rows_dag else 0

    # -------------------------------------------------------------------------
    # Q1: 4-Hop Pathway Drug Repurposing
    # -------------------------------------------------------------------------
    compare_query(
        name="CYPHER Q1: 4-Hop Pathway Drug Repurposing (CbGpPWpD)",
        metapath="(Disease)-[:DaG]->(Gene)-[:GpPW]->(Pathway)<-[:GpPW]-(Gene)<-[:CbG]-(Compound)",
        imp_query_fn=lambda snap, s: snap.traverse(start_node=s, catalog="hetionet").out("DaG").out("GpPW").in_("GpPW").in_("CbG"),
        hn_matrix_fn=lambda s: np.flatnonzero(((((np.eye(1, M_dag.shape[0], seed_dag_idx, dtype=bool) @ M_dag) > 0) @ M_gppw > 0) @ M_gppw.T > 0) @ M_cbg.T > 0),
    )

    # -------------------------------------------------------------------------
    # Q2: 2-Hop MoA Expression Counteraction
    # -------------------------------------------------------------------------
    rows_ddg = hn_matrices.get("DdG", ([],))[0]
    seed_ddg_idx = rows_ddg.index(disease_node_obj) if disease_node_obj in rows_ddg else 0

    compare_query(
        name="CYPHER Q2: 2-Hop Expression Counteraction / MoA (CuG<rGaD)",
        metapath="(Disease)-[:DdG]->(Gene)<-[:CuG]-(Compound)",
        imp_query_fn=lambda snap, s: snap.traverse(start_node=s, catalog="hetionet").out("DdG").in_("CuG"),
        hn_matrix_fn=lambda s: np.flatnonzero(((np.eye(1, M_ddg.shape[0], seed_ddg_idx, dtype=bool) @ M_ddg) > 0) @ M_cug.T > 0),
    )

    # -------------------------------------------------------------------------
    # Q3: 2-Hop Chemical Resemblance
    # -------------------------------------------------------------------------
    cols_ctd = hn_matrices.get("CtD", ([], []))[1]
    seed_ctd_idx = cols_ctd.index(disease_node_obj) if disease_node_obj in cols_ctd else 0

    compare_query(
        name="CYPHER Q3: 2-Hop Chemical Resemblance Transitivity (CrCtD)",
        metapath="(Disease)<-[:CtD]-(Compound)-[:CrC]->(Compound)",
        imp_query_fn=lambda snap, s: snap.traverse(start_node=s, catalog="hetionet").in_("CtD").out("CrC"),
        hn_matrix_fn=lambda s: np.flatnonzero(((np.eye(1, M_ctd.shape[1], seed_ctd_idx, dtype=bool) @ M_ctd.T) > 0) @ M_crc > 0),
    )

    # -------------------------------------------------------------------------
    # Q4: 3-Hop Shared Anatomy Pathology
    # -------------------------------------------------------------------------
    rows_dla = hn_matrices.get("DlA", ([],))[0]
    seed_dla_idx = rows_dla.index(disease_node_obj) if disease_node_obj in rows_dla else 0

    compare_query(
        name="CYPHER Q4: 3-Hop Shared Anatomy Pathology & Target Discovery (DlAeGbC)",
        metapath="(Disease)-[:DlA]->(Anatomy)-[:AeG]->(Gene)<-[:CbG]-(Compound)",
        imp_query_fn=lambda snap, s: snap.traverse(start_node=s, catalog="hetionet").out("DlA").out("AeG").in_("CbG"),
        hn_matrix_fn=lambda s: np.flatnonzero((((np.eye(1, M_dla.shape[0], seed_dla_idx, dtype=bool) @ M_dla) > 0) @ M_aeg > 0) @ M_cbg.T > 0),
    )

    # =========================================================================
    # WHOLE-COHORT ALL-DISEASES SCREENING SWEEP
    # =========================================================================
    print("=" * 115)
    print(f" BENCHMARK: Whole-Cohort Batch Sweep Across All {len(disease_ids)} Diseases (4-Hop Drug Repurposing)")
    print("=" * 115)

    t_imp = imp_snap.traverse(start_node=seed_node, catalog="hetionet").out("DaG").out("GpPW").in_("GpPW").in_("CbG")
    compiled_imp = t_imp.compile()
    ctx = vm.VmContext(imp_snap)
    state = vm.VmState()

    t0 = time.perf_counter()
    for d in disease_ids:
        res = compiled_imp.execute_with_context(ctx, state, d)
        if res.is_ok() and res.result_type == vm.RegisterType.TYPE_BITSET_HANDLE:
            ctx.release_bitset(res.raw_value)
    t_sweep_imp = (time.perf_counter() - t0) * 1000.0

    t0 = time.perf_counter()
    for d in range(M_dag.shape[0]):
        _ = np.flatnonzero(((((np.eye(1, M_dag.shape[0], d, dtype=bool) @ M_dag) > 0) @ M_gppw > 0) @ M_gppw.T > 0) @ M_cbg.T > 0)
    t_sweep_mat = (time.perf_counter() - t0) * 1000.0

    print(f"{'Engine / Method':<32} | {'Total Sweep Time':<18} | {'Mean / Disease':<18} | {'Speedup'}")
    print("-" * 85)
    print(f"{'Impulse Graph (Zero-Copy VM)':<32} | {t_sweep_imp:12.3f} ms   | {t_sweep_imp/len(disease_ids)*1000:10.3f} µs   | 1.0x (BASELINE)")
    print(f"{'hetnetpy (Matrix Operations)':<32} | {t_sweep_mat:12.3f} ms   | {t_sweep_mat/M_dag.shape[0]*1000:10.3f} µs   | {t_sweep_mat/t_sweep_imp:8.1f}x slower")
    print("-" * 85 + "\n")

    ctx.destroy()


if __name__ == "__main__":
    main()
