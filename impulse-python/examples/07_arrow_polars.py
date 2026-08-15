#!/usr/bin/env python3
"""
Impulse Graph Engine — Example 07: Columnar Data Interchange (Arrow, Polars & Pandas)
Demonstrates using .imps snapshots as a columnar data interchange format,
alongside side-by-side openCypher query equivalence.
"""

import time
import pandas as pd
import numpy as np
from impulse_graph import Snapshot, Writer

try:
    import polars as pl
except ImportError:
    pl = None

try:
    import pyarrow as pa
except ImportError:
    pa = None

def main():
    print("===============================================================")
    print(" Impulse Graph Engine — Example 07: Columnar Interchange")
    print("===============================================================\n")

    # ------------------------------------------------------------------------
    # Step 1: Ingesting Tabular DataFrame into .imps Snapshot
    # ------------------------------------------------------------------------
    print("1. Ingesting Tabular Data into .imps via Writer.from_dataframe:")
    df_raw = pd.DataFrame({
        "sender": ["alice@co", "alice@co", "bob@co", "carol@co", "dan@co"],
        "receiver": ["bob@co", "carol@co", "dan@co", "dan@co", "eve@co"]
    })
    print(df_raw)

    snapshot_path = "temp_emails.imps"
    Writer.from_dataframe(snapshot_path, df_raw, src_col="sender", tgt_col="receiver", domain_name="EmailUser")
    print(f"   -> Successfully compiled DataFrame into '{snapshot_path}'.")

    # ------------------------------------------------------------------------
    # Step 2: Zero-Copy Export to Columnar DataFrame
    # ------------------------------------------------------------------------
    print("\n2. Exporting Snapshot to Columnar DataFrame:")
    with Snapshot(snapshot_path) as snap:
        if pa is not None:
            t0 = time.perf_counter()
            arrow_table = snap.to_arrow_table(relation_index=0)
            t_arrow = (time.perf_counter() - t0) * 1e6
            print(f"   -> Arrow Export Latency:  {t_arrow:.2f} µs (Schema: {arrow_table.schema})")

        if pl is not None:
            t0 = time.perf_counter()
            df_edges = snap.to_polars(relation_index=0)
            t_polars = (time.perf_counter() - t0) * 1e6
            print(f"   -> Polars Export Latency: {t_polars:.2f} µs")
        else:
            t0 = time.perf_counter()
            df_edges = snap.to_pandas(relation_index=0)
            t_pandas = (time.perf_counter() - t0) * 1e6
            print(f"   -> Pandas Export Latency: {t_pandas:.2f} µs")

        print(f"   -> Edge Table:\n{df_edges}")

        # --------------------------------------------------------------------
        # Step 3: Running Columnar Aggregations
        # --------------------------------------------------------------------
        print("\n3. Running Columnar Aggregations on Edge List:")
        if pl is not None:
            out_degrees = df_edges.group_by("src").len().sort("len", descending=True)
        else:
            out_degrees = df_edges.groupby("src").size().reset_index(name="count").sort_values("count", ascending=False)
        print(f"   -> Top Sender Out-Degrees:\n{out_degrees}")

        # --------------------------------------------------------------------
        # Step 4: Batch String Key Resolution
        # --------------------------------------------------------------------
        print("\n4. Batch String Key Resolution (Dense IDs -> Email Addresses):")
        unique_nids = sorted(list(df_edges["src"].unique()))
        resolved_emails = snap.resolve_keys_batch(domain_id=0, node_ids=unique_nids)

        resolved_df = pd.DataFrame({
            "dense_id": unique_nids,
            "email_address": resolved_emails
        })
        print(f"   -> Resolved Catalog:\n{resolved_df}")

        # --------------------------------------------------------------------
        # Step 5: Side-by-Side openCypher Query Equivalence
        # --------------------------------------------------------------------
        print("\n5. Side-by-Side openCypher Equivalence:")
        print("   DataFrame Approach: df[df['src'] == 0]['dst'].tolist()")
        print("   openCypher 1-Liner: snap.cypher('MATCH (a)-[r]->(b) WHERE id(a) = 0 RETURN b')")

        t0 = time.perf_counter()
        cypher_res = snap.cypher("MATCH (a)-[r]->(b) WHERE id(a) = 0 RETURN b")
        t_cypher = (time.perf_counter() - t0) * 1e6

        print(f"   -> Cypher Latency:  {t_cypher:.2f} µs")
        print(f"   -> Query Result:    {cypher_res}")

    print("\n[SUCCESS] Example 07 completed cleanly.")

if __name__ == "__main__":
    main()
