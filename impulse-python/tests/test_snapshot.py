import os
import tempfile
import pytest
import numpy as np
from impulse_graph import Writer, Snapshot


def test_snapshot_inspection_and_zero_copy():
    with tempfile.TemporaryDirectory() as tmpdir:
        snapshot_path = os.path.join(tmpdir, "snapshot_test.imps")
        
        with Writer(snapshot_path) as writer:
            writer.add_domain(0, 1, "Account")
            writer.add_domain(1, 1, "Transaction")
            
            row_offsets = [0, 2, 5]
            col_indices = [100, 101, 102, 103, 104]
            
            writer.add_relation(
                src_domain_id=0,
                tgt_domain_id=1,
                encoding_type=0,
                node_count=2,
                edge_count=5,
                section_features=0,
                row_offsets=row_offsets,
                col_indices=col_indices,
            )
            writer.finalize()

        with Snapshot(snapshot_path) as snap:
            assert snap.domain_count() == 2
            assert snap.relation_count() == 1
            
            rel = snap.get_relation(0)
            assert rel["src_domain_id"] == 0
            assert rel["tgt_domain_id"] == 1
            assert rel["node_count"] == 2
            assert rel["edge_count"] == 5
            
            row_arr = snap.get_row_offsets_array(0)
            col_arr = snap.get_col_indices_array(0)
            
            assert np.array_equal(row_arr, np.array([0, 2, 5], dtype=np.uint32))
            assert np.array_equal(col_arr, np.array([100, 101, 102, 103, 104], dtype=np.uint32))
