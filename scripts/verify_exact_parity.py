#!/usr/bin/env python3
"""
===============================================================================
IMPULSE GRAPH vs PYTHON BASELINES - 100% EXACT PARITY VERIFICATION
===============================================================================
Verifies that Impulse Graph, SciPy Sparse, NetworkX, and Pandas produce
IDENTICAL output sets across all active diseases for Q1, Q2, Q3, Q4.
"""

import os
import sys
from typing import List, Dict, Set
import numpy as np

sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "impulse-python")))
from impulse_graph import Snapshot, vm

HETIONET_DIR = "/Users/jesse/impulse/datasets/hetionet"
NODES_PATH = os.path.join(HETIONET_DIR, "nodes.tsv")
EDGES_PATH = os.path.join(HETIONET_DIR, "edges.sif")
IMPS_PATH = os.path.join(HETIONET_DIR, "hetionet.v09.imps")


def main():
    print("\n" + "=" * 100)
    print("   EXACT RESULT PARITY VERIFICATION: IMPULSE GRAPH vs PYTHON BASELINES")
    print("   Verifying 100% Mathematical Equivalence Across Q1, Q2, Q3, Q4")
    print("=" * 100 + "\n")

    # 1. Load Node Mappings
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
    print(f"[*] Total Nodes: {num_nodes:,} | Diseases to Test: {len(disease_ids)}")

    # 2. Open Impulse Snapshot
    imp_snap = Snapshot(IMPS_PATH)

    # 3. Load SciPy Matrices
    import scipy.sparse as sp
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

    # 4. Load NetworkX Adjacency
    nx_out_adj: Dict[str, Dict[int, Set[int]]] = {}
    nx_in_adj: Dict[str, Dict[int, Set[int]]] = {}
    for rel, elist in edges_by_rel.items():
        out_d = nx_out_adj.setdefault(rel, {})
        in_d = nx_in_adj.setdefault(rel, {})
        for u, v in elist:
            out_d.setdefault(u, set()).add(v)
            in_d.setdefault(v, set()).add(u)

    # 5. Load Pandas DataFrames
    import pandas as pd
    df_edges = pd.read_csv(EDGES_PATH, sep="\t")
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
    # TEST Q1: 4-Hop Pathway Drug Repurposing
    # -------------------------------------------------------------------------
    print("\n[*] Checking Q1: 4-Hop Drug Repurposing (DaG -> GpPW <- GpPW <- CbG)...")
    M_dag, M_gppw, M_gppw_t, M_cbg_t = scipy_matrices["DaG"], scipy_matrices["GpPW"], scipy_matrices["GpPW"].T, scipy_matrices["CbG"].T
    dag_out, gppw_out, gppw_in, cbg_in = nx_out_adj.get("DaG", {}), nx_out_adj.get("GpPW", {}), nx_in_adj.get("GpPW", {}), nx_in_adj.get("CbG", {})

    t_q1 = imp_snap.traverse(catalog="hetionet").out("DaG").out("GpPW").in_("GpPW").in_("CbG")
    ctx = vm.VmContext(imp_snap)
    state = vm.VmState()

    q1_matches = 0
    non_empty_count = 0
    for d in disease_ids:
        # Impulse Graph
        imp_nodes = t_q1.to_set(start_node=d)

        # SciPy
        v0 = np.zeros(num_nodes, dtype=bool)
        v0[d] = True
        v4 = ((((v0 @ M_dag) > 0) @ M_gppw > 0) @ M_gppw_t > 0) @ M_cbg_t > 0
        scipy_nodes = set(np.flatnonzero(v4))

        # NetworkX
        nx_nodes = {c for g1 in dag_out.get(d, set()) for p in gppw_out.get(g1, set()) for g2 in gppw_in.get(p, set()) for c in cbg_in.get(g2, set())}

        # Pandas
        sk = id_to_node[d]
        s1 = df_dag[df_dag["disease"] == sk]
        pd_nodes = set()
        if not s1.empty:
            s2 = s1.merge(df_gppw1, on="gene1")
            if not s2.empty:
                s3 = s2.merge(df_gppw2, on="pathway")
                if not s3.empty:
                    s4 = s3.merge(df_cbg, on="gene2")
                    pd_nodes = {node_to_id[c] for c in s4["compound"].unique() if c in node_to_id}

        assert imp_nodes == scipy_nodes, f"Mismatch Impulse vs SciPy on disease {d}: {len(imp_nodes)} vs {len(scipy_nodes)}"
        assert imp_nodes == nx_nodes, f"Mismatch Impulse vs NetworkX on disease {d}: {len(imp_nodes)} vs {len(nx_nodes)}"
        assert imp_nodes == pd_nodes, f"Mismatch Impulse vs Pandas on disease {d}: {len(imp_nodes)} vs {len(pd_nodes)}"
        q1_matches += 1
        if len(imp_nodes) > 0:
            non_empty_count += 1

    print(f"  -> Q1 Results: {q1_matches}/{len(disease_ids)} diseases verified ({non_empty_count} diseases with candidates). 100% IDENTICAL!")

    # -------------------------------------------------------------------------
    # TEST Q2: 2-Hop MoA Expression Counteraction
    # -------------------------------------------------------------------------
    print("\n[*] Checking Q2: 2-Hop MoA Counteraction (DdG -> CuG)...")
    M_ddg, M_cug_t = scipy_matrices["DdG"], scipy_matrices["CuG"].T
    ddg_out, cug_in = nx_out_adj.get("DdG", {}), nx_in_adj.get("CuG", {})
    t_q2 = imp_snap.traverse(catalog="hetionet").out("DdG").in_("CuG")

    q2_matches = 0
    for d in disease_ids:
        imp_nodes = t_q2.to_set(start_node=d)

        v0 = np.zeros(num_nodes, dtype=bool)
        v0[d] = True
        scipy_nodes = set(np.flatnonzero(((v0 @ M_ddg) > 0) @ M_cug_t > 0))
        nx_nodes = {c for g in ddg_out.get(d, set()) for c in cug_in.get(g, set())}

        sk = id_to_node[d]
        s1 = df_ddg[df_ddg["disease"] == sk]
        pd_nodes = set()
        if not s1.empty:
            s2 = s1.merge(df_cug, on="gene")
            pd_nodes = {node_to_id[c] for c in s2["compound"].unique() if c in node_to_id}

        assert imp_nodes == scipy_nodes == nx_nodes == pd_nodes, f"Mismatch Q2 on disease {d}"
        q2_matches += 1

    print(f"  -> Q2 Results: {q2_matches}/{len(disease_ids)} diseases verified. 100% IDENTICAL!")

    # -------------------------------------------------------------------------
    # TEST Q3: 2-Hop Chemical Resemblance
    # -------------------------------------------------------------------------
    print("\n[*] Checking Q3: 2-Hop Chemical Resemblance (CtD <- CrC)...")
    M_ctd_t, M_crc = scipy_matrices["CtD"].T, scipy_matrices["CrC"]
    ctd_in, crc_out = nx_in_adj.get("CtD", {}), nx_out_adj.get("CrC", {})
    t_q3 = imp_snap.traverse(catalog="hetionet").in_("CtD").out("CrC")

    q3_matches = 0
    for d in disease_ids:
        imp_nodes = t_q3.to_set(start_node=d)

        v0 = np.zeros(num_nodes, dtype=bool)
        v0[d] = True
        scipy_nodes = set(np.flatnonzero(((v0 @ M_ctd_t) > 0) @ M_crc > 0))
        nx_nodes = {c2 for c1 in ctd_in.get(d, set()) for c2 in crc_out.get(c1, set())}

        sk = id_to_node[d]
        s1 = df_ctd[df_ctd["disease"] == sk]
        pd_nodes = set()
        if not s1.empty:
            s2 = s1.merge(df_crc, on="compound1")
            pd_nodes = {node_to_id[c] for c in s2["compound2"].unique() if c in node_to_id}

        assert imp_nodes == scipy_nodes == nx_nodes == pd_nodes, f"Mismatch Q3 on disease {d}"
        q3_matches += 1

    print(f"  -> Q3 Results: {q3_matches}/{len(disease_ids)} diseases verified. 100% IDENTICAL!")

    # -------------------------------------------------------------------------
    # TEST Q4: 3-Hop Anatomy Pathology
    # -------------------------------------------------------------------------
    print("\n[*] Checking Q4: 3-Hop Anatomy Pathology (DlA -> AeG <- CbG)...")
    M_dla, M_aeg, M_cbg_t = scipy_matrices["DlA"], scipy_matrices["AeG"], scipy_matrices["CbG"].T
    dla_out, aeg_out, cbg_in = nx_out_adj.get("DlA", {}), nx_out_adj.get("AeG", {}), nx_in_adj.get("CbG", {})
    t_q4 = imp_snap.traverse(catalog="hetionet").out("DlA").out("AeG").in_("CbG")

    q4_matches = 0
    for d in disease_ids:
        imp_nodes = t_q4.to_set(start_node=d)

        v0 = np.zeros(num_nodes, dtype=bool)
        v0[d] = True
        scipy_nodes = set(np.flatnonzero((((v0 @ M_dla) > 0) @ M_aeg > 0) @ M_cbg_t > 0))
        nx_nodes = {c for a in dla_out.get(d, set()) for g in aeg_out.get(a, set()) for c in cbg_in.get(g, set())}

        sk = id_to_node[d]
        s1 = df_dla[df_dla["disease"] == sk]
        pd_nodes = set()
        if not s1.empty:
            s2 = s1.merge(df_aeg, on="anatomy")
            if not s2.empty:
                s3 = s2.merge(df_cbg_q4, on="gene")
                pd_nodes = {node_to_id[c] for c in s3["compound"].unique() if c in node_to_id}

        assert imp_nodes == scipy_nodes == nx_nodes == pd_nodes, f"Mismatch Q4 on disease {d}"
        q4_matches += 1

    print(f"  -> Q4 Results: {q4_matches}/{len(disease_ids)} diseases verified. 100% IDENTICAL!")

    print("\n" + "=" * 100)
    print("   100% MATHEMATICAL PARITY CONFIRMED ACROSS ALL QUERIES & BASELINES")
    print("=" * 100 + "\n")
    ctx.destroy()


if __name__ == "__main__":
    main()
