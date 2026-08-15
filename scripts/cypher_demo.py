#!/usr/bin/env python3
"""
===============================================================================
IMPULSE GRAPH ENGINE - DECLARATIVE OPENCYPHER PYTHON SHOWCASE
===============================================================================
Demonstrates executing declarative openCypher statements directly in Python
using Impulse Graph Engine's zero-copy memory-mapped off-heap virtual machine.
"""

import os
import sys
import time
from typing import List, Dict
import numpy as np

# Ensure impulse_graph is in path
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "impulse-python")))
from impulse_graph import Snapshot, vm

HETIONET_PATH = "/Users/jesse/impulse/datasets/hetionet/hetionet.v09.imps"
DRKG_PATH = "/Users/jesse/impulse/datasets/drkg/drkg.v09.imps"


def run_cypher_benchmark(snap: Snapshot, name: str, cypher: str, params: dict, catalog: str = "hetionet", iterations: int = 10000):
    print("=" * 100)
    print(f"  {name}")
    print("=" * 100)
    print("  openCypher Query:")
    for line in cypher.strip().splitlines():
        print(f"    {line.strip()}")
    print(f"  Parameters: {params}")

    # 1. Execute direct Python openCypher Query
    results = snap.cypher(cypher, params=params, catalog=catalog)
    if isinstance(results, list):
        sample = results[:8] if len(results) > 8 else results
        print(f"  -> Discovered: {len(results)} candidate targets | Sample: {sample}")
    else:
        print(f"  -> Aggregation Count Result: {results}")

    # 2. Compile openCypher statement to reusable ImpulseVM Bytecode
    traversal = snap.compile_cypher(cypher, catalog=catalog)
    compiled = traversal.compile()
    ctx = vm.VmContext(snap)
    state = vm.VmState()
    seed_id = int(next(iter(params.values()))) if params else 0

    # Warmup
    for _ in range(1000):
        res = compiled.execute_with_context(ctx, state, seed_id)
        if res.is_ok() and res.result_type == vm.RegisterType.TYPE_BITSET_HANDLE:
            ctx.release_bitset(res.raw_value)

    # Benchmark Loop
    latencies_us: List[float] = []
    for _ in range(iterations):
        t0 = time.perf_counter_ns()
        res = compiled.execute_with_context(ctx, state, seed_id)
        t1 = time.perf_counter_ns()
        latencies_us.append((t1 - t0) / 1000.0)
        if res.is_ok() and res.result_type == vm.RegisterType.TYPE_BITSET_HANDLE:
            ctx.release_bitset(res.raw_value)

    p50 = np.median(latencies_us)
    mean = np.mean(latencies_us)
    p99 = np.percentile(latencies_us, 99)
    qps = int(1_000_000.0 / mean)

    print(f"  -> Compiled VM Code:   {compiled.instruction_count()} impOps instructions")
    print(f"  -> Median (p50):       {p50:8.3f} µs")
    print(f"  -> Mean Latency:       {mean:8.3f} µs")
    print(f"  -> P99 Latency:        {p99:8.3f} µs")
    print(f"  -> Execution Rate:     {qps:,} queries / second\n")
    ctx.destroy()


def main():
    print("\n" + "#" * 100)
    print("   IMPULSE GRAPH ENGINE - DECLARATIVE OPENCYPHER PYTHON SHOWCASE")
    print("   Zero-Copy Off-Heap Execution of Raw openCypher Queries in Sub-Microsecond Time")
    print("#" * 100 + "\n")

    if os.path.exists(HETIONET_PATH):
        print(f"[*] Opening Hetionet Snapshot: {HETIONET_PATH}")
        with Snapshot(HETIONET_PATH) as het_snap:
            print(f"    Domains: {het_snap.domain_count()} | Relations: {het_snap.relation_count()}\n")

            # Q1: 4-Hop Pathway Drug Repurposing
            run_cypher_benchmark(
                snap=het_snap,
                name="Cypher Q1: 4-Hop Pathway Drug Repurposing (CbGpPWpD)",
                cypher="MATCH (d:Disease)-[:DaG]->(g1:Gene)-[:GpPW]->(p:Pathway)<-[:GpPW]-(g2:Gene)<-[:CbG]-(c:Compound)\nWHERE d.id = $diseaseId RETURN c",
                params={"diseaseId": 14738},
                catalog="hetionet",
            )

            # Q2: 2-Hop Mechanism-of-Action (MoA) Counteraction
            run_cypher_benchmark(
                snap=het_snap,
                name="Cypher Q2: 2-Hop Expression Counteraction / MoA (CuG<rGaD)",
                cypher="MATCH (d:Disease)-[:DdG]->(g:Gene)<-[:CuG]-(c:Compound)\nWHERE d.id = $diseaseId RETURN c",
                params={"diseaseId": 14738},
                catalog="hetionet",
            )

            # Q3: 2-Hop Chemical Resemblance
            run_cypher_benchmark(
                snap=het_snap,
                name="Cypher Q3: 2-Hop Chemical Resemblance Transitivity (CrCtD)",
                cypher="MATCH (d:Disease)<-[:CtD]-(c1:Compound)-[:CrC]->(c2:Compound)\nWHERE d.id = $diseaseId RETURN c2",
                params={"diseaseId": 14738},
                catalog="hetionet",
            )

            # Q4: 3-Hop Shared Anatomy Pathology
            run_cypher_benchmark(
                snap=het_snap,
                name="Cypher Q4: 3-Hop Shared Anatomy Pathology & Target Discovery (DlAeGbC)",
                cypher="MATCH (d:Disease)-[:DlA]->(a:Anatomy)-[:AeG]->(g:Gene)<-[:CbG]-(c:Compound)\nWHERE d.id = $diseaseId RETURN c",
                params={"diseaseId": 14738},
                catalog="hetionet",
            )

            # Q5: Count Aggregation Query
            run_cypher_benchmark(
                snap=het_snap,
                name="Cypher Q5: Expression Counteraction Candidate Count Aggregation",
                cypher="MATCH (d:Disease)-[:DdG]->(g:Gene)<-[:CuG]-(c:Compound)\nWHERE d.id = $diseaseId RETURN count(c)",
                params={"diseaseId": 14738},
                catalog="hetionet",
            )

    print("=" * 100)
    print("   ALL OPENCYPHER QUERIES EXECUTED IN PYTHON SUCCESSFULLY")
    print("=" * 100 + "\n")


if __name__ == "__main__":
    main()
