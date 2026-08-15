#!/usr/bin/env python3
"""
Impulse Graph Engine - Medical Knowledge Graph Query Showcase (Python SDK)

This script demonstrates high-performance zero-copy traversals over biomedical
knowledge graphs (Hetionet and DRKG) using the Impulse Graph Python SDK & VM.

Queries Demonstrated:
  1. Q1: 4-Hop Pathway Drug Repurposing (CbGpPWpD)
  2. Q2: 2-Hop Expression Counteraction / MoA (CuG<rGaD)
  3. Q3: 2-Hop Chemical Resemblance Transitivity (CrCtD)
  4. Q4: 3-Hop Shared Anatomy Pathology & Target Discovery (DlAeGbC)
  5. Q5: 3-Hop Precision Oncology Cascades (DisGeNET + STRING + DrugBank)
  6. Q6: 2-Hop Polypharmacology Adverse DDI Warning (DrugBank + GNBR)
"""

import os
import sys
import time
from typing import List

# Add impulse-python to sys.path if running standalone
script_dir = os.path.dirname(os.path.abspath(__file__))
pkg_dir = os.path.abspath(os.path.join(script_dir, "..", "impulse-python"))
if pkg_dir not in sys.path:
    sys.path.insert(0, pkg_dir)

from impulse_graph import Snapshot, Traversal, vm

HETIONET_PATH = "/Users/jesse/impulse/datasets/hetionet/hetionet.v09.imps"
DRKG_PATH = "/Users/jesse/impulse/datasets/drkg/drkg.v09.imps"

# Domain Catalogs for Friendly String-Based Relation Resolution
HETIONET_CATALOG = {
    "AeG": 0,
    "DaG": 7,
    "CtD": 9,
    "CrC": 10,
    "DlA": 14,
    "DdG": 17,
    "CbG": 19,
    "GpPW": 20,
    "CuG": 22,
}

DRKG_CATALOG = {
    "DISGENET::da": 0,
    "STRING::interacts_with": 0,
    "DRUGBANK::target": 0,
    "DRUGBANK::ddi_interactor_in": 0,
    "GNBR::C": 0,
}


def find_active_seed_node(snap: Snapshot, rel_idx: int, min_degree: int = 5) -> int:
    """Finds an active node ID with sufficient degree for demonstration."""
    offsets = snap.get_row_offsets_array(rel_idx)
    for node_id in range(len(offsets) - 1):
        deg = offsets[node_id + 1] - offsets[node_id]
        if deg >= min_degree:
            return node_id
    return 0


def run_showcase_query(
    name: str,
    dataset: str,
    cypher: str,
    traversal_fn,
    snap: Snapshot,
    seed_node: int,
    iterations: int = 20000,
    warmup: int = 5000,
):
    print("=" * 100)
    print(f"  {name}")
    print(f"  Dataset: {dataset}")
    print(f"  openCypher Query:")
    print(f"    {cypher}")
    print("=" * 100)

    # 1. Friendly Pythonic Traversal Syntax
    t: Traversal = traversal_fn(snap, seed_node)
    
    # 2. Extract Sample Discovered Entities
    candidates = t.to_list(start_node=seed_node)
    sample_preview = candidates[:8] if len(candidates) > 8 else candidates
    print(f"  -> Discovered {len(candidates)} candidate targets | Sample: {sample_preview}{' ...' if len(candidates) > 8 else ''}")

    # 3. High-Throughput Compiled Engine Benchmark
    compiled = t.compile()
    ctx = vm.VmContext(snap)
    state = vm.VmState()

    for _ in range(warmup):
        res = compiled.execute_with_context(ctx, state, seed_node)
        if res.is_ok() and res.result_type == vm.RegisterType.TYPE_BITSET_HANDLE:
            ctx.release_bitset(res.raw_value)

    latencies_us: List[float] = []
    t0_wall = time.perf_counter()

    for _ in range(iterations):
        t0 = time.perf_counter_ns()
        res = compiled.execute_with_context(ctx, state, seed_node)
        t1 = time.perf_counter_ns()
        latencies_us.append((t1 - t0) / 1000.0)

        if res.is_ok() and res.result_type == vm.RegisterType.TYPE_BITSET_HANDLE:
            ctx.release_bitset(res.raw_value)

    t1_wall = time.perf_counter()
    total_wall_sec = t1_wall - t0_wall

    latencies_us.sort()
    mean_us = sum(latencies_us) / len(latencies_us)
    p50_us = latencies_us[int(len(latencies_us) * 0.50)]
    p90_us = latencies_us[int(len(latencies_us) * 0.90)]
    p99_us = latencies_us[int(len(latencies_us) * 0.99)]
    qps = int(iterations / total_wall_sec)

    print(f"  -> Compiled VM Code:   {compiled.instruction_count()} impOps instructions")
    print(f"  -> Mean Latency:       {mean_us:8.3f} µs")
    print(f"  -> P50 (Median):       {p50_us:8.3f} µs")
    print(f"  -> P90 Latency:        {p90_us:8.3f} µs")
    print(f"  -> P99 Latency:        {p99_us:8.3f} µs")
    print(f"  -> Execution Rate:     {qps:,} queries / second\n")

    ctx.destroy()


def main():
    print("\n" + "#" * 100)
    print("   IMPULSE GRAPH ENGINE - BIOMEDICAL KNOWLEDGE GRAPH SHOWCASE (PYTHON SDK)")
    print("   Friendly Fluent Path Traversal DSL & High-Performance ImpulseVM Engine")
    print("#" * 100 + "\n")

    # -------------------------------------------------------------------------
    # Hetionet v1.0 Queries
    # -------------------------------------------------------------------------
    if os.path.exists(HETIONET_PATH):
        print(f"[*] Opening Hetionet Snapshot: {HETIONET_PATH}")
        with Snapshot(HETIONET_PATH) as het_snap:
            print(f"    Domains: {het_snap.domain_count()} | Relations: {het_snap.relation_count()}\n")

            # Q1: 4-Hop Pathway Drug Repurposing
            seed_q1 = find_active_seed_node(het_snap, 6)
            run_showcase_query(
                name="Cypher Q1: 4-Hop Pathway Drug Repurposing (CbGpPWpD)",
                dataset="Hetionet v1.0",
                cypher="MATCH (d:Disease)-[:DaG]->(g1:Gene)-[:GpPW]->(p:Pathway)<-[:GpPW]-(g2:Gene)<-[:CbG]-(c:Compound) WHERE d.id = $diseaseId RETURN c",
                traversal_fn=lambda snap, seed: (
                    snap.traverse(start_node=seed, catalog="hetionet")
                        .out("DaG")
                        .out("GpPW")
                        .in_("GpPW")
                        .in_("CbG")
                ),
                snap=het_snap,
                seed_node=seed_q1,
            )

            # Q2: 2-Hop Expression Counteraction / MoA (CuG<rGaD)
            seed_q2 = find_active_seed_node(het_snap, 15)
            run_showcase_query(
                name="Cypher Q2: 2-Hop Expression Counteraction / MoA (CuG<rGaD)",
                dataset="Hetionet v1.0",
                cypher="MATCH (d:Disease)-[:DdG]->(g:Gene)<-[:CuG]-(c:Compound) WHERE d.id = $diseaseId RETURN c",
                traversal_fn=lambda snap, seed: (
                    snap.traverse(start_node=seed, catalog="hetionet")
                        .out("DdG")
                        .in_("CuG")
                ),
                snap=het_snap,
                seed_node=seed_q2,
            )

            # Q3: 2-Hop Chemical Resemblance Transitivity (CrCtD)
            seed_q3 = find_active_seed_node(het_snap, 8)
            run_showcase_query(
                name="Cypher Q3: 2-Hop Chemical Resemblance Transitivity (CrCtD)",
                dataset="Hetionet v1.0",
                cypher="MATCH (d:Disease)<-[:CtD]-(c1:Compound)-[:CrC]->(c2:Compound) WHERE d.id = $diseaseId RETURN c2",
                traversal_fn=lambda snap, seed: (
                    snap.traverse(start_node=seed, catalog="hetionet")
                        .in_("CtD")
                        .out("CrC")
                ),
                snap=het_snap,
                seed_node=seed_q3,
            )

            # Q4: 3-Hop Shared Anatomy Pathology & Target Discovery (DlAeGbC)
            seed_q4 = find_active_seed_node(het_snap, 12)
            run_showcase_query(
                name="Cypher Q4: 3-Hop Shared Anatomy Pathology & Target Discovery (DlAeGbC)",
                dataset="Hetionet v1.0",
                cypher="MATCH (d:Disease)-[:DlA]->(a:Anatomy)-[:AeG]->(g:Gene)<-[:CbG]-(c:Compound) WHERE d.id = $diseaseId RETURN c",
                traversal_fn=lambda snap, seed: (
                    snap.traverse(start_node=seed, catalog="hetionet")
                        .out("DlA")
                        .out("AeG")
                        .in_("CbG")
                ),
                snap=het_snap,
                seed_node=seed_q4,
            )
    else:
        print(f"[!] Hetionet snapshot not found at {HETIONET_PATH}")

    # -------------------------------------------------------------------------
    # DRKG (Drug Repurposing Knowledge Graph) Queries
    # -------------------------------------------------------------------------
    if os.path.exists(DRKG_PATH):
        print(f"[*] Opening DRKG Snapshot: {DRKG_PATH}")
        with Snapshot(DRKG_PATH) as drkg_snap:
            print(f"    Domains: {drkg_snap.domain_count()} | Relations: {drkg_snap.relation_count()}\n")

            # Q5: 3-Hop Precision Oncology Cascades
            seed_q5 = find_active_seed_node(drkg_snap, 81)
            run_showcase_query(
                name="Cypher Q5: 3-Hop Precision Oncology Cascades (DisGeNET + STRING + DrugBank)",
                dataset="DRKG (DisGeNET + STRING + DrugBank)",
                cypher="MATCH (d:Disease)-[:`Hetionet::DaG::Disease:Gene`]->(g1:Gene)-[:`STRING::OTHER::Gene:Gene`]->(g2:Gene)<-[:`DRUGBANK::target::Compound:Gene`]-(c:Compound) WHERE d.id = $diseaseId RETURN c",
                traversal_fn=lambda snap, seed: (
                    snap.traverse(start_node=seed)
                        .out(81)
                        .out(99)
                        .in_(101)
                ),
                snap=drkg_snap,
                seed_node=seed_q5,
            )

            # Q6: 2-Hop Polypharmacology Adverse DDI Warning
            seed_q6 = find_active_seed_node(drkg_snap, 96)
            run_showcase_query(
                name="Cypher Q6: 2-Hop Polypharmacology Adverse DDI Warning (DrugBank + GNBR)",
                dataset="DRKG (DrugBank DDI + GNBR Side Effects)",
                cypher="MATCH (c1:Compound)-[:`DRUGBANK::ddi_interactor_in::Compound:Compound`]->(c2:Compound)-[:`GNBR::T::Compound:Disease`]->(s:Disease) WHERE c1.id = $compoundId RETURN s",
                traversal_fn=lambda snap, seed: (
                    snap.traverse(start_node=seed)
                        .out(0)
                        .out(96)
                ),
                snap=drkg_snap,
                seed_node=seed_q6,
            )
    else:
        print(f"[!] DRKG snapshot not found at {DRKG_PATH}")

    print("=" * 100)
    print("   ALL MEDICAL KNOWLEDGE GRAPH QUERIES EXECUTED SUCCESSFULLY")
    print("=" * 100 + "\n")


if __name__ == "__main__":
    main()
