import os
import tempfile
import pytest
from impulse_graph import Writer, Snapshot


def test_reachability_queries():
    with tempfile.TemporaryDirectory() as tmpdir:
        snapshot_path = os.path.join(tmpdir, "reachability.imps")
        
        with Writer(snapshot_path) as writer:
            writer.add_domain(0, 1, "Node")
            
            # Node 0 connected to 1, 2
            # Node 1 connected to 3
            # Node 2 has no outgoing edges
            # Node 3 connected to 0
            row_offsets = [0, 2, 3, 3, 4]
            col_indices = [1, 2, 3, 0]
            
            writer.add_relation(
                src_domain_id=0,
                tgt_domain_id=0,
                encoding_type=0,
                node_count=4,
                edge_count=4,
                section_features=0,
                row_offsets=row_offsets,
                col_indices=col_indices,
            )
            writer.finalize()

        with Snapshot(snapshot_path) as snap:
            # Check edge existence
            assert snap.is_reachable(0, 0, 0, 1) is True
            assert snap.is_reachable(0, 0, 0, 2) is True
            assert snap.is_reachable(0, 0, 0, 3) is False
            
            assert snap.is_reachable(0, 1, 0, 3) is True
            assert snap.is_reachable(0, 1, 0, 0) is False
            
            assert snap.is_reachable(0, 2, 0, 0) is False
            assert snap.is_reachable(0, 3, 0, 0) is True
            
            # Out of bounds
            assert snap.is_reachable(0, 10, 0, 0) is False
