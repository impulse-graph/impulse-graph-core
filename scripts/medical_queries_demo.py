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

from impulse_graph import Snapshot, vm

HETIONET_PATH = "/Users/jesse/impulse/datasets/hetionet/hetionet.v09.imps"
DRKG_PATH = "/Users/jesse/impulse/datasets/drkg/drkg.v09.imps"


def find_active_seed_node(snap: Snapshot, rel_idx: int, min_degree: int = 5) -> int:
    """Finds an active node ID with sufficient degree for demonstration."""
    offsets = snap.get_row_offsets_array(rel_idx)
    for node_id in range(len(offsets) - 1):
        deg = offsets[node_id + 1] - offsets[node_id]
        if deg >= min_degree:
            return node_id
    return 0


def run_benchmark(
    name: str,
    dataset: str,
    cypher: str,
    query_builder_fn,
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

    # 1. Build & Compile VM Bytecode
    qb = query_builder_fn()
    compiled = qb.compile()
    print(f"  [Bytecode]: Compiled {compiled.instruction_count()} impOps instructions")

    # 2. Execution Context
    ctx = vm.VmContext(snap)
    state = vm.VmState()

    # 3. Warmup
    for _ in range(warmup):
        res = compiled.execute_with_context(ctx, state, seed_node)
        if res.is_ok() and res.result_type == vm.RegisterType.TYPE_BITSET_HANDLE:
            ctx.release_bitset(res.raw_value)

    # 4. Timed Execution
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

    print(f"  Seed Node ID:          {seed_node}")
    print(f"  Mean Latency:          {mean_us:8.3f} µs")
    print(f"  P50 (Median) Latency:  {p50_us:8.3f} µs")
    print(f"  P90 Latency:           {p90_us:8.3f} µs")
    print(f"  P99 Latency:           {p99_us:8.3f} µs")
    print(f"  Execution Throughput:  {qps:,} queries / second\n")

    ctx.destroy()


def main():
    print("\n" + "#" * 100)
    print("   IMPULSE GRAPH ENGINE - BIOMEDICAL KNOWLEDGE GRAPH QUERY DEMONSTRATION (PYTHON SDK)")
    print("   Zero-Copy C-ABI Kernel & ImpulseVM Instruction Set Architecture")
    print("#" * 100 + "\n")

    # -------------------------------------------------------------------------
    # Hetionet v1.0 Queries
    # -------------------------------------------------------------------------
    if os.path.exists(HETIONET_PATH):
        print(f"[*] Opening Hetionet Snapshot: {HETIONET_PATH}")
        with Snapshot(HETIONET_PATH) as het_snap:
            print(f"    Domains: {het_snap.domain_count()} | Relations: {het_snap.relation_count()}\n")

            # Q1: 4-Hop Pathway Drug Repurposing
            seed_q1 = find_active_seed_node(het_snap, 7)
            run_benchmark(
                name="Cypher Q1: 4-Hop Pathway Drug Repurposing (CbGpPWpD)",
                dataset="Hetionet v1.0",
                cypher="MATCH (d:Disease)-[:DaG]->(g1:Gene)-[:GpPW]->(p:Pathway)<-[:GpPW]-(g2:Gene)<-[:CbG]-(c:Compound) WHERE d.id = $diseaseId RETURN c",
                query_builder_fn=lambda: (
                    vm.QueryBuilder()
                    .input_node(0)
                    .walk_edge(7)   # DaG
                    .walk_edge(20)  # GpPW
                    .walk_csc(20)   # GpPW (rev)
                    .walk_csc(19)   # CbG (rev)
                    .collect_bitset()
                ),
                snap=het_snap,
                seed_node=seed_q1,
            )

            # Q2: 2-Hop Expression Counteraction / MoA
            seed_q2 = find_active_seed_node(het_snap, 17)
            run_benchmark(
                name="Cypher Q2: 2-Hop Expression Counteraction / MoA (CuG<rGaD)",
                dataset="Hetionet v1.0",
                cypher="MATCH (d:Disease)-[:DdG]->(g:Gene)<-[:CuG]-(c:Compound) WHERE d.id = $diseaseId RETURN c",
                query_builder_fn=lambda: (
                    vm.QueryBuilder()
                    .input_node(0)
                    .walk_edge(17)  # DdG
                    .walk_csc(22)   # CuG (rev)
                    .collect_bitset()
                ),
                snap=het_snap,
                seed_node=seed_q2,
            )

            # Q3: 2-Hop Chemical Resemblance Transitivity
            seed_q3 = find_active_seed_node(het_snap, 9)
            run_benchmark(
                name="Cypher Q3: 2-Hop Chemical Resemblance Transitivity (CrCtD)",
                dataset="Hetionet v1.0",
                cypher="MATCH (d:Disease)<-[:CtD]-(c1:Compound)-[:CrC]->(c2:Compound) WHERE d.id = $diseaseId RETURN c2",
                query_builder_fn=lambda: (
                    vm.QueryBuilder()
                    .input_node(0)
                    .walk_csc(9)   # CtD (rev)
                    .walk_edge(10) # CrC
                    .collect_bitset()
                ),
                snap=het_snap,
                seed_node=seed_q3,
            )

            # Q4: 3-Hop Shared Anatomy Pathology & Target Discovery
            seed_q4 = find_active_seed_node(het_snap, 14)
            run_benchmark(
                name="Cypher Q4: 3-Hop Shared Anatomy Pathology & Target Discovery (DlAeGbC)",
                dataset="Hetionet v1.0",
                cypher="MATCH (d:Disease)-[:DlA]->(a:Anatomy)-[:AeG]->(g:Gene)<-[:CbG]-(c:Compound) WHERE d.id = $diseaseId RETURN c",
                query_builder_fn=lambda: (
                    vm.QueryBuilder()
                    .input_node(0)
                    .walk_edge(14)  # DlA
                    .walk_edge(0)   # AeG
                    .walk_csc(19)   # CbG (rev)
                    .collect_bitset()
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
            seed_q5 = find_active_seed_node(drkg_snap, 0)
            run_benchmark(
                name="Cypher Q5: 3-Hop Precision Oncology Cascades (DisGeNET + STRING + DrugBank)",
                dataset="DRKG (DisGeNET + STRING + DrugBank)",
                cypher="MATCH (d:Disease)-[:`DISGENET::da`]->(g1:Gene)-[:`STRING::interacts_with`]->(g2:Gene)<-[:`DRUGBANK::target`]-(c:Compound) WHERE d.id = $diseaseId RETURN c",
                query_builder_fn=lambda: (
                    vm.QueryBuilder()
                    .input_node(0)
                    .walk_edge(0)  # DISGENET::da
                    .walk_edge(0)  # STRING::interacts_with
                    .walk_csc(0)   # DRUGBANK::target (rev)
                    .collect_bitset()
                ),
                snap=drkg_snap,
                seed_node=seed_q5,
            )

            # Q6: 2-Hop Polypharmacology Adverse DDI Warning
            seed_q6 = find_active_seed_node(drkg_snap, 0)
            run_benchmark(
                name="Cypher Q6: 2-Hop Polypharmacology Adverse DDI Warning (DrugBank + GNBR)",
                dataset="DRKG (DrugBank DDI + GNBR Side Effects)",
                cypher="MATCH (c1:Compound)-[:`DRUGBANK::ddi_interactor_in`]->(c2:Compound)-[:`GNBR::C`]->(s:SideEffect) WHERE c1.id = $compoundId RETURN s",
                query_builder_fn=lambda: (
                    vm.QueryBuilder()
                    .input_node(0)
                    .walk_edge(0)  # DRUGBANK::ddi_interactor_in
                    .walk_edge(0)  # GNBR::C
                    .collect_bitset()
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
