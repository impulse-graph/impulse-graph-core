#!/usr/bin/env python3
"""
Impulse Graph Engine — Example 08: NetworkX Graph Analytics Interop
Demonstrates using .imps snapshots for NetworkX graph algorithms,
alongside side-by-side Fluent Traversal query equivalence.
"""

import time

import networkx as nx

from impulse_graph import Snapshot, Writer


def main():
    print("===============================================================")
    print(" Impulse Graph Engine — Example 08: NetworkX Graph Interop")
    print("===============================================================\n")

    # ------------------------------------------------------------------------
    # Step 1: Creating Snapshot from NetworkX Graph
    # ------------------------------------------------------------------------
    print("1. Creating .imps snapshot from NetworkX Graph:")
    G_orig = nx.cycle_graph(6, create_using=nx.DiGraph)
    # Add cross-edges
    G_orig.add_edge(0, 3)
    G_orig.add_edge(1, 4)

    snapshot_path = "temp_nx_graph.imps"
    Writer.from_networkx(snapshot_path, G_orig, domain_name="Node")
    print(
        f"   -> Successfully compiled NetworkX graph ({G_orig.number_of_nodes()} nodes, {G_orig.number_of_edges()} edges)."
    )

    # ------------------------------------------------------------------------
    # Step 2: Zero-Copy Loading into NetworkX DiGraph
    # ------------------------------------------------------------------------
    print("\n2. Loading Snapshot into NetworkX DiGraph:")
    with Snapshot(snapshot_path) as snap:
        t0 = time.perf_counter()
        G = snap.to_networkx(relation_index=0)
        t_load = (time.perf_counter() - t0) * 1e6

        print(f"   -> Loading Latency:    {t_load:.2f} µs")
        print(f"   -> Node Count:         {G.number_of_nodes()}")
        print(f"   -> Edge Count:         {G.number_of_edges()}")

        # --------------------------------------------------------------------
        # Step 3: Running NetworkX Graph Algorithms
        # --------------------------------------------------------------------
        print("\n3. Executing NetworkX Algorithms:")
        t0 = time.perf_counter()
        pagerank_scores = nx.pagerank(G)
        t_pr = (time.perf_counter() - t0) * 1e6

        shortest_paths = nx.single_source_shortest_path_length(G, source=0)

        print(f"   -> PageRank Latency:   {t_pr:.2f} µs")
        print(f"   -> Shortest Paths (0): {shortest_paths}")
        print(f"   -> PageRank Node 0:    {pagerank_scores[0]:.4f}")

        # --------------------------------------------------------------------
        # Step 4: Side-by-Side Fluent Traversal Equivalence
        # --------------------------------------------------------------------
        print("\n4. Side-by-Side Fluent Traversal Equivalence:")
        print("   NetworkX Approach:  nx.single_source_shortest_path_length(G, 0)")
        print("   ImpulseVM 1-Liner:  snap.traverse(start_node=0).out(0).out(0).to_list()")

        t0 = time.perf_counter()
        hop2 = snap.traverse(start_node=0).out(0).out(0).to_list()
        t_imp = (time.perf_counter() - t0) * 1e6

        print(f"   -> ImpulseVM Latency:  {t_imp:.2f} µs")
        print(f"   -> 2-Hop Reachable:    {hop2}")

    print("\n[SUCCESS] Example 08 completed cleanly.")


if __name__ == "__main__":
    main()
