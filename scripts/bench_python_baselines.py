#!/usr/bin/env python3
import os, sys, time, gc
from typing import List, Dict, Set
import resource
import numpy as np

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "impulse-python")))
from impulse_graph import Snapshot, vm

HETIONET_DIR = "/Users/jesse/impulse/datasets/hetionet"
NODES_PATH = os.path.join(HETIONET_DIR, "nodes.tsv")
EDGES_PATH = os.path.join(HETIONET_DIR, "edges.sif")
IMPS_PATH = os.path.join(HETIONET_DIR, "hetionet.v09.imps")

def get_process_memory_mb() -> float:
    # On macOS, ru_maxrss is in bytes
    return resource.getrusage(resource.RUSAGE_SELF).ru_maxrss / (1024.0 * 1024.0)

def main():
    print("\n" + "=" * 100)
    print("   IMPULSE GRAPH vs STANDARD PYTHON DATA SCIENCE BASELINES (HETIONET v1.0)")
    print("   Dataset: 47,031 Nodes | 11 Domains | 24 Relations | 2,250,197 Edges")
    print("=" * 100 + "\n")

    if not os.path.exists(NODES_PATH) or not os.path.exists(EDGES_PATH) or not os.path.exists(IMPS_PATH):
        print(f"[!] Required files not found in {HETIONET_DIR}")
        return

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
    print(f"[*] Point Query Target Disease: {seed_key} (Dense ID: {seed_node})\n")

    print("-" * 100)
    print(" [1] Impulse Graph (Zero-Copy Memory-Mapped .imps Snapshot)")
    print("-" * 100)
    mem_before = get_process_memory_mb()
    t0 = time.perf_counter()
    imp_snap = Snapshot(IMPS_PATH)
    t_load_imp = (time.perf_counter() - t0) * 1000.0
    mem_imp = get_process_memory_mb() - mem_before
    print(f"  -> Cold Start / Open Time:      {t_load_imp:8.3f} ms")
    print(f"  -> Physical RSS RAM Delta:       {mem_imp:8.2f} MB")

    print("\n" + "-" * 100)
    print(" [2] SciPy Sparse (scipy.sparse.csr_matrix Boolean Matrix-Vector Products)")
    print("-" * 100)
    import scipy.sparse as sp
    gc.collect()
    mem_before = get_process_memory_mb()
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
    mem_scipy = get_process_memory_mb() - mem_before
    print(f"  -> Ingestion / CSR Build Time:   {t_load_scipy:8.3f} ms")
    print(f"  -> In-Memory Heap Footprint:     {mem_scipy:8.2f} MB")

    print("\n" + "-" * 100)
    print(" [3] Pandas DataFrames (pandas.DataFrame.merge / Relational Joins)")
    print("-" * 100)
    import pandas as pd
    gc.collect()
    mem_before = get_process_memory_mb()
    t0 = time.perf_counter()

    df_edges = pd.read_csv(EDGES_PATH, sep="\t")
    df_dag = df_edges[df_edges["metaedge"] == "DaG"][["source", "target"]].rename(columns={"source": "disease", "target": "gene1"})
    df_gppw1 = df_edges[df_edges["metaedge"] == "GpPW"][["source", "target"]].rename(columns={"source": "gene1", "target": "pathway"})
    df_gppw2 = df_edges[df_edges["metaedge"] == "GpPW"][["source", "target"]].rename(columns={"source": "gene2", "target": "pathway"})
    df_cbg = df_edges[df_edges["metaedge"] == "CbG"][["source", "target"]].rename(columns={"source": "compound", "target": "gene2"})

    t_load_pd = (time.perf_counter() - t0) * 1000.0
    mem_pd = get_process_memory_mb() - mem_before
    print(f"  -> CSV Load / Parse Time:        {t_load_pd:8.3f} ms")
    print(f"  -> In-Memory Heap Footprint:     {mem_pd:8.2f} MB")

    print("\n" + "-" * 100)
    print(" [4] NetworkX (networkx Adjacency Sets / MultiDiGraph Traversal)")
    print("-" * 100)
    gc.collect()
    mem_before = get_process_memory_mb()
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
    mem_nx = get_process_memory_mb() - mem_before
    print(f"  -> Graph Adjacency Build Time:   {t_load_nx:8.3f} ms")
    print(f"  -> In-Memory Heap Footprint:     {mem_nx:8.2f} MB")

    print("\n" + "=" * 100)
    print(" BENCHMARK 1: 4-Hop Pathway Drug Repurposing (Point Query)")
    print(" Metapath: (Disease)-[:DaG]->(Gene)-[:GpPW]->(Pathway)<-[:GpPW]-(Gene)<-[:CbG]-(Compound)")
    print("=" * 100)

    NUM_RUNS = 2000
    WARMUP = 200

    t_imp = imp_snap.traverse(start_node=seed_node, catalog="hetionet").out("DaG").out("GpPW").in_("GpPW").in_("CbG")
    compiled_imp = t_imp.compile()
    ctx = vm.VmContext(imp_snap)
    state = vm.VmState()

    for _ in range(WARMUP):
        res = compiled_imp.execute_with_context(ctx, state, seed_node)
        if res.is_ok() and res.result_type == vm.RegisterType.TYPE_BITSET_HANDLE:
            ctx.release_bitset(res.raw_value)

    lats_imp = []
    for _ in range(NUM_RUNS):
        t0 = time.perf_counter_ns()
        res = compiled_imp.execute_with_context(ctx, state, seed_node)
        t1 = time.perf_counter_ns()
        lats_imp.append((t1 - t0) / 1000.0)
        if res.is_ok() and res.result_type == vm.RegisterType.TYPE_BITSET_HANDLE:
            ctx.release_bitset(res.raw_value)

    M_dag = scipy_matrices["DaG"]
    M_gppw = scipy_matrices["GpPW"]
    M_gppw_t = scipy_matrices["GpPW"].T
    M_cbg_t = scipy_matrices["CbG"].T

    def run_scipy(seed: int) -> np.ndarray:
        v0 = np.zeros(num_nodes, dtype=bool)
        v0[seed] = True
        v1 = (v0 @ M_dag) > 0
        v2 = (v1 @ M_gppw) > 0
        v3 = (v2 @ M_gppw_t) > 0
        v4 = (v3 @ M_cbg_t) > 0
        return np.flatnonzero(v4)

    for _ in range(WARMUP):
        run_scipy(seed_node)

    lats_scipy = []
    for _ in range(NUM_RUNS):
        t0 = time.perf_counter_ns()
        run_scipy(seed_node)
        t1 = time.perf_counter_ns()
        lats_scipy.append((t1 - t0) / 1000.0)

    dag_out = nx_out_adj.get("DaG", {})
    gppw_out = nx_out_adj.get("GpPW", {})
    gppw_in = nx_in_adj.get("GpPW", {})
    cbg_in = nx_in_adj.get("CbG", {})

    def run_nx(seed: int) -> Set[int]:
        genes = dag_out.get(seed, set())
        pathways = set()
        for g in genes:
            pathways.update(gppw_out.get(g, set()))
        genes2 = set()
        for p in pathways:
            genes2.update(gppw_in.get(p, set()))
        compounds = set()
        for g2 in genes2:
            compounds.update(cbg_in.get(g2, set()))
        return compounds

    for _ in range(WARMUP):
        run_nx(seed_node)

    lats_nx = []
    for _ in range(NUM_RUNS):
        t0 = time.perf_counter_ns()
        run_nx(seed_node)
        t1 = time.perf_counter_ns()
        lats_nx.append((t1 - t0) / 1000.0)

    def run_pandas(seed_k: str) -> np.ndarray:
        s1 = df_dag[df_dag["disease"] == seed_k]
        if s1.empty:
            return np.array([])
        s2 = s1.merge(df_gppw1, on="gene1")
        if s2.empty:
            return np.array([])
        s3 = s2.merge(df_gppw2, on="pathway")
        if s3.empty:
            return np.array([])
        s4 = s3.merge(df_cbg, on="gene2")
        return s4["compound"].unique()

    for _ in range(10):
        run_pandas(seed_key)

    lats_pd = []
    for _ in range(100):
        t0 = time.perf_counter_ns()
        run_pandas(seed_key)
        t1 = time.perf_counter_ns()
        lats_pd.append((t1 - t0) / 1000.0)

    print(f"{'Engine / Baseline':<28} | {'P50 (Median)':<14} | {'Mean Latency':<14} | {'P99 Latency':<14} | {'Throughput (QPS)':<16} | {'Speedup'}")
    print("-" * 110)
    
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
    print("-" * 110)

    print("\n" + "=" * 100)
    print(f" BENCHMARK 2: Whole-Cohort Batch Sweep Across All {len(disease_ids)} Diseases (4-Hop Drug Repurposing)")
    print("=" * 100)

    t0 = time.perf_counter()
    for d in disease_ids:
        res = compiled_imp.execute_with_context(ctx, state, d)
        if res.is_ok() and res.result_type == vm.RegisterType.TYPE_BITSET_HANDLE:
            ctx.release_bitset(res.raw_value)
    t_sweep_imp = (time.perf_counter() - t0) * 1000.0

    t0 = time.perf_counter()
    for d in disease_ids:
        run_scipy(d)
    t_sweep_scipy = (time.perf_counter() - t0) * 1000.0

    t0 = time.perf_counter()
    for d in disease_ids:
        run_nx(d)
    t_sweep_nx = (time.perf_counter() - t0) * 1000.0

    t0 = time.perf_counter()
    for d in disease_ids:
        run_pandas(id_to_node[d])
    t_sweep_pd = (time.perf_counter() - t0) * 1000.0

    print(f"{'Engine / Framework':<28} | {'Total Sweep Time':<18} | {'Mean / Disease':<18} | {'Speedup'}")
    print("-" * 85)
    print(f"{'Impulse Graph (Off-Heap)':<28} | {t_sweep_imp:12.3f} ms   | {t_sweep_imp/len(disease_ids)*1000:10.3f} µs   | 1.0x (BASELINE)")
    print(f"{'SciPy Sparse (CSR)':<28} | {t_sweep_scipy:12.3f} ms   | {t_sweep_scipy/len(disease_ids)*1000:10.3f} µs   | {t_sweep_scipy/t_sweep_imp:8.1f}x slower")
    print(f"{'NetworkX (Adjacency)':<28} | {t_sweep_nx:12.3f} ms   | {t_sweep_nx/len(disease_ids)*1000:10.3f} µs   | {t_sweep_nx/t_sweep_imp:8.1f}x slower")
    print(f"{'Pandas (DataFrame.merge)':<28} | {t_sweep_pd:12.3f} ms   | {t_sweep_pd/len(disease_ids)*1000:10.3f} µs   | {t_sweep_pd/t_sweep_imp:8.1f}x slower")
    print("-" * 85 + "\n")

    ctx.destroy()

if __name__ == "__main__":
    main()
