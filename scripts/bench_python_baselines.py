#!/usr/bin/env python3
"""
===============================================================================
IMPULSE GRAPH vs STANDARD PYTHON DATA SCIENCE BASELINES (ALL 4 HETIONET QUERIES)
===============================================================================
Compares Impulse Graph against typical data scientist tools across Q1, Q2, Q3, Q4:
  1. Impulse Graph (Zero-Copy C-ABI off-heap binary snapshot + SIMD Traversal)
  2. SciPy Sparse (CSR Adjacency Matrix-Vector Boolean Products)
  3. Pandas (Relational DataFrame .merge / joins over edge lists)
  4. NetworkX (Pure Python MultiDiGraph / DiGraph typed neighbor expansion)
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

HETIONET_DIR = "/Users/jesse/impulse/datasets/hetionet"
NODES_PATH = os.path.join(HETIONET_DIR, "nodes.tsv")
EDGES_PATH = os.path.join(HETIONET_DIR, "edges.sif")
IMPS_PATH = os.path.join(HETIONET_DIR, "hetionet.v09.imps")


def get_process_memory_mb() -> float:
    return resource.getrusage(resource.RUSAGE_SELF).ru_maxrss / (1024.0 * 1024.0)


def main():
    print("\n" + "=" * 115)
    print("   IMPULSE GRAPH vs STANDARD PYTHON DATA SCIENCE BASELINES - COMPLETE HETIONET SUITE")
    print("   Dataset: 47,031 Nodes | 11 Domains | 24 Relations | 2,250,197 Edges")
    print("=" * 115 + "\n")

    if not os.path.exists(NODES_PATH) or not os.path.exists(EDGES_PATH) or not os.path.exists(IMPS_PATH):
        print(f"[!] Required files not found in {HETIONET_DIR}")
        return

    # 0. Load Node Mappings
    node_to_id: Dict[str, int] = {}
    id_to_node: Dict[int, str] = {}
    disease_ids: List[int] = []

    with open(NODES_PATH, "r") as f:
        next(f)
        for idx, line in enumerate(f):
            parts = line.strip().split("\t")
            if len(parts) >= 3:
                nid, name, kind = parts[0], parts[1], parts[2]
                node_to_id[nid] = idx
                id_to_node[idx] = nid
                if kind == "Disease":
                    disease_ids.append(idx)

    num_nodes = len(node_to_id)
    print(f"[*] Indexed {num_nodes:,} nodes ({len(disease_ids)} diseases).")

    seed_key = "Disease::DOID:10652" if "Disease::DOID:10652" in node_to_id else id_to_node[disease_ids[0]]
    seed_node = node_to_id[seed_key]
    print(f"[*] Target Seed Disease: {seed_key} (Dense ID: {seed_node})\n")

    # 1. Impulse Graph
    t0 = time.perf_counter()
    imp_snap = Snapshot(IMPS_PATH)
    t_load_imp = (time.perf_counter() - t0) * 1000.0
    print(f"[*] [1] Impulse Graph Snapshot Opened in {t_load_imp:.3f} ms")

    # 2. SciPy Sparse
    import scipy.sparse as sp
    t0 = time.perf_counter()
    edges_by_rel: Dict[str, List[tuple]] = {}
    with open(EDGES_PATH, "r") as f:
        next(f)
        for line in f:
            src, rel, tgt = line.strip().split("\t")[:3]
            if src in node_to_id and tgt in node_to_id:
                edges_by_rel.setdefault(rel, []).append((node_to_id[src], node_to_id[tgt]))

    scipy_matrices: Dict[str, sp.csr_matrix] = {}
    for rel, elist in edges_by_rel.items():
        srcs = [e[0] for e in elist]
        tgts = [e[1] for e in elist]
        data = np.ones(len(elist), dtype=bool)
        mat = sp.csr_matrix((data, (srcs, tgts)), shape=(num_nodes, num_nodes), dtype=bool)
        scipy_matrices[rel] = mat
    t_load_scipy = (time.perf_counter() - t0) * 1000.0
    print(f"[*] [2] SciPy Sparse CSR Matrices Ingested in {t_load_scipy:.3f} ms")

    # 3. Pandas
    import pandas as pd
    t0 = time.perf_counter()
    df_edges = pd.read_csv(EDGES_PATH, sep="\t")
    t_load_pd = (time.perf_counter() - t0) * 1000.0
    print(f"[*] [3] Pandas DataFrames Parsed in {t_load_pd:.3f} ms")

    # 4. NetworkX Adjacency
    t0 = time.perf_counter()
    nx_out_adj: Dict[str, Dict[int, Set[int]]] = {}
    nx_in_adj: Dict[str, Dict[int, Set[int]]] = {}
    for rel, elist in edges_by_rel.items():
        out_d = nx_out_adj.setdefault(rel, {})
        in_d = nx_in_adj.setdefault(rel, {})
        for u, v in elist:
            out_d.setdefault(u, set()).add(v)
            in_d.setdefault(v, set()).add(u)
    t_load_nx = (time.perf_counter() - t0) * 1000.0
    print(f"[*] [4] NetworkX Adjacency Graph Built in {t_load_nx:.3f} ms\n")

    # Helper function to benchmark a query across all 4 tools
    def benchmark_query(q_name: str, metapath: str, imp_traversal_fn, scipy_fn, nx_fn, pandas_fn, num_runs=2000, pd_runs=100):
        print("=" * 115)
        print(f" {q_name}")
        print(f" Metapath: {metapath}")
        print("=" * 115)

        # 1. Impulse Graph
        t_imp = imp_traversal_fn(imp_snap, seed_node)
        compiled_imp = t_imp.compile()
        ctx = vm.VmContext(imp_snap)
        state = vm.VmState()

        for _ in range(100):
            res = compiled_imp.execute_with_context(ctx, state, seed_node)
            if res.is_ok() and res.result_type == vm.RegisterType.TYPE_BITSET_HANDLE:
                ctx.release_bitset(res.raw_value)

        lats_imp = []
        for _ in range(num_runs):
            t0 = time.perf_counter_ns()
            res = compiled_imp.execute_with_context(ctx, state, seed_node)
            t1 = time.perf_counter_ns()
            lats_imp.append((t1 - t0) / 1000.0)
            if res.is_ok() and res.result_type == vm.RegisterType.TYPE_BITSET_HANDLE:
                ctx.release_bitset(res.raw_value)

        # 2. SciPy Sparse
        for _ in range(100):
            scipy_fn(seed_node)

        lats_scipy = []
        for _ in range(num_runs):
            t0 = time.perf_counter_ns()
            scipy_fn(seed_node)
            t1 = time.perf_counter_ns()
            lats_scipy.append((t1 - t0) / 1000.0)

        # 3. NetworkX
        for _ in range(100):
            nx_fn(seed_node)

        lats_nx = []
        for _ in range(num_runs):
            t0 = time.perf_counter_ns()
            nx_fn(seed_node)
            t1 = time.perf_counter_ns()
            lats_nx.append((t1 - t0) / 1000.0)

        # 4. Pandas
        for _ in range(10):
            pandas_fn(seed_key)

        lats_pd = []
        for _ in range(pd_runs):
            t0 = time.perf_counter_ns()
            pandas_fn(seed_key)
            t1 = time.perf_counter_ns()
            lats_pd.append((t1 - t0) / 1000.0)

        # Print Comparison Table
        print(f"{'Engine / Framework':<28} | {'P50 (Median)':<14} | {'Mean Latency':<14} | {'P99 Latency':<14} | {'Throughput (QPS)':<16} | {'Speedup'}")
        print("-" * 115)
        
        p50_imp, mean_imp, p99_imp = np.median(lats_imp), np.mean(lats_imp), np.percentile(lats_imp, 99)
        qps_imp = int(1_000_000.0 / mean_imp)
        print(f"{'Impulse Graph (Off-Heap)':<28} | {p50_imp:10.3f} µs | {mean_imp:10.3f} µs | {p99_imp:10.3f} µs | {qps_imp:12,} QPS | 1.0x (BASELINE)")

        p50_scipy, mean_scipy, p99_scipy = np.median(lats_scipy), np.mean(lats_scipy), np.percentile(lats_scipy, 99)
        qps_scipy = int(1_000_000.0 / mean_scipy)
        speedup_scipy = mean_scipy / mean_imp
        print(f"{'SciPy Sparse (CSR Mult)':<28} | {p50_scipy:10.3f} µs | {mean_scipy:10.3f} µs | {p99_scipy:10.3f} µs | {qps_scipy:12,} QPS | {speedup_scipy:8.1f}x slower")

        p50_nx, mean_nx, p99_nx = np.median(lats_nx), np.mean(lats_nx), np.percentile(lats_nx, 99)
        qps_nx = int(1_000_000.0 / mean_nx)
        speedup_nx = mean_nx / mean_imp
        print(f"{'NetworkX (Adjacency Set)':<28} | {p50_nx:10.3f} µs | {mean_nx:10.3f} µs | {p99_nx:10.3f} µs | {qps_nx:12,} QPS | {speedup_nx:8.1f}x slower")

        p50_pd, mean_pd, p99_pd = np.median(lats_pd), np.mean(lats_pd), np.percentile(lats_pd, 99)
        qps_pd = int(1_000_000.0 / mean_pd)
        speedup_pd = mean_pd / mean_imp
        print(f"{'Pandas (DataFrame.merge)':<28} | {p50_pd:10.3f} µs | {mean_pd:10.3f} µs | {p99_pd:10.3f} µs | {qps_pd:12,} QPS | {speedup_pd:8.1f}x slower")
        print("-" * 115 + "\n")
        ctx.destroy()

    # Pre-extract Pandas DataFrames
    df_dag = df_edges[df_edges["metaedge"] == "DaG"][["source", "target"]].rename(columns={"source": "disease", "target": "gene1"})
    df_gppw1 = df_edges[df_edges["metaedge"] == "GpPW"][["source", "target"]].rename(columns={"source": "gene1", "target": "pathway"})
    df_gppw2 = df_edges[df_edges["metaedge"] == "GpPW"][["source", "target"]].rename(columns={"source": "gene2", "target": "pathway"})
    df_cbg = df_edges[df_edges["metaedge"] == "CbG"][["source", "target"]].rename(columns={"source": "compound", "target": "gene2"})

    df_ddg = df_edges[df_edges["metaedge"] == "DdG"][["source", "target"]].rename(columns={"source": "disease", "target": "gene"})
    df_cug = df_edges[df_edges["metaedge"] == "CuG"][["source", "target"]].rename(columns={"source": "compound", "target": "gene"})

    df_ctd = df_edges[df_edges["metaedge"] == "CtD"][["source", "target"]].rename(columns={"source": "compound1", "target": "disease"})
    df_crc = df_edges[df_edges["metaedge"] == "CrC"][["source", "target"]].rename(columns={"source": "compound1", "target": "compound2"})

    df_dla = df_edges[df_edges["metaedge"] == "DlA"][["source", "target"]].rename(columns={"source": "disease", "target": "anatomy"})
    df_aeg = df_edges[df_edges["metaedge"] == "AeG"][["source", "target"]].rename(columns={"source": "anatomy", "target": "gene"})
    df_cbg_q4 = df_edges[df_edges["metaedge"] == "CbG"][["source", "target"]].rename(columns={"source": "compound", "target": "gene"})

    # -------------------------------------------------------------------------
    # Q1: 4-Hop Pathway Drug Repurposing
    # -------------------------------------------------------------------------
    M_dag, M_gppw, M_gppw_t, M_cbg_t = scipy_matrices["DaG"], scipy_matrices["GpPW"], scipy_matrices["GpPW"].T, scipy_matrices["CbG"].T
    dag_out, gppw_out, gppw_in, cbg_in = nx_out_adj.get("DaG", {}), nx_out_adj.get("GpPW", {}), nx_in_adj.get("GpPW", {}), nx_in_adj.get("CbG", {})

    benchmark_query(
        q_name="CYPHER Q1: 4-Hop Pathway Drug Repurposing (CbGpPWpD)",
        metapath="(Disease)-[:DaG]->(Gene)-[:GpPW]->(Pathway)<-[:GpPW]-(Gene)<-[:CbG]-(Compound)",
        imp_traversal_fn=lambda snap, s: snap.traverse(start_node=s, catalog="hetionet").out("DaG").out("GpPW").in_("GpPW").in_("CbG"),
        scipy_fn=lambda s: np.flatnonzero(((((np.eye(1, num_nodes, s, dtype=bool) @ M_dag) > 0) @ M_gppw > 0) @ M_gppw_t > 0) @ M_cbg_t > 0),
        nx_fn=lambda s: {c for g1 in dag_out.get(s, set()) for p in gppw_out.get(g1, set()) for g2 in gppw_in.get(p, set()) for c in cbg_in.get(g2, set())},
        pandas_fn=lambda sk: (
            df_dag[df_dag["disease"] == sk]
            .merge(df_gppw1, on="gene1")
            .merge(df_gppw2, on="pathway")
            .merge(df_cbg, on="gene2")["compound"].unique()
            if not df_dag[df_dag["disease"] == sk].empty else np.array([])
        )
    )

    # -------------------------------------------------------------------------
    # Q2: 2-Hop Mechanism-of-Action (MoA) Counteraction
    # -------------------------------------------------------------------------
    M_ddg, M_cug_t = scipy_matrices["DdG"], scipy_matrices["CuG"].T
    ddg_out, cug_in = nx_out_adj.get("DdG", {}), nx_in_adj.get("CuG", {})

    benchmark_query(
        q_name="CYPHER Q2: 2-Hop Expression Counteraction / MoA (CuG<rGaD)",
        metapath="(Disease)-[:DdG]->(Gene)<-[:CuG]-(Compound)",
        imp_traversal_fn=lambda snap, s: snap.traverse(start_node=s, catalog="hetionet").out("DdG").in_("CuG"),
        scipy_fn=lambda s: np.flatnonzero(((np.eye(1, num_nodes, s, dtype=bool) @ M_ddg) > 0) @ M_cug_t > 0),
        nx_fn=lambda s: {c for g in ddg_out.get(s, set()) for c in cug_in.get(g, set())},
        pandas_fn=lambda sk: (
            df_ddg[df_ddg["disease"] == sk]
            .merge(df_cug, on="gene")["compound"].unique()
            if not df_ddg[df_ddg["disease"] == sk].empty else np.array([])
        )
    )

    # -------------------------------------------------------------------------
    # Q3: 2-Hop Chemical Resemblance Transitivity
    # -------------------------------------------------------------------------
    M_ctd_t, M_crc = scipy_matrices["CtD"].T, scipy_matrices["CrC"]
    ctd_in, crc_out = nx_in_adj.get("CtD", {}), nx_out_adj.get("CrC", {})

    benchmark_query(
        q_name="CYPHER Q3: 2-Hop Chemical Resemblance Transitivity (CrCtD)",
        metapath="(Disease)<-[:CtD]-(Compound)-[:CrC]->(Compound)",
        imp_traversal_fn=lambda snap, s: snap.traverse(start_node=s, catalog="hetionet").in_("CtD").out("CrC"),
        scipy_fn=lambda s: np.flatnonzero(((np.eye(1, num_nodes, s, dtype=bool) @ M_ctd_t) > 0) @ M_crc > 0),
        nx_fn=lambda s: {c2 for c1 in ctd_in.get(s, set()) for c2 in crc_out.get(c1, set())},
        pandas_fn=lambda sk: (
            df_ctd[df_ctd["disease"] == sk]
            .merge(df_crc, on="compound1")["compound2"].unique()
            if not df_ctd[df_ctd["disease"] == sk].empty else np.array([])
        )
    )

    # -------------------------------------------------------------------------
    # Q4: 3-Hop Shared Anatomy Pathology & Target Discovery
    # -------------------------------------------------------------------------
    M_dla, M_aeg, M_cbg_t = scipy_matrices["DlA"], scipy_matrices["AeG"], scipy_matrices["CbG"].T
    dla_out, aeg_out, cbg_in = nx_out_adj.get("DlA", {}), nx_out_adj.get("AeG", {}), nx_in_adj.get("CbG", {})

    benchmark_query(
        q_name="CYPHER Q4: 3-Hop Shared Anatomy Pathology & Target Discovery (DlAeGbC)",
        metapath="(Disease)-[:DlA]->(Anatomy)-[:AeG]->(Gene)<-[:CbG]-(Compound)",
        imp_traversal_fn=lambda snap, s: snap.traverse(start_node=s, catalog="hetionet").out("DlA").out("AeG").in_("CbG"),
        scipy_fn=lambda s: np.flatnonzero((((np.eye(1, num_nodes, s, dtype=bool) @ M_dla) > 0) @ M_aeg > 0) @ M_cbg_t > 0),
        nx_fn=lambda s: {c for a in dla_out.get(s, set()) for g in aeg_out.get(a, set()) for c in cbg_in.get(g, set())},
        pandas_fn=lambda sk: (
            df_dla[df_dla["disease"] == sk]
            .merge(df_aeg, on="anatomy")
            .merge(df_cbg_q4, on="gene")["compound"].unique()
            if not df_dla[df_dla["disease"] == sk].empty else np.array([])
        )
    )

if __name__ == "__main__":
    main()
