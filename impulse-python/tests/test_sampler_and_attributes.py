import os
import tempfile

import numpy as np
import pytest

from impulse_graph import Snapshot, Writer


@pytest.fixture
def sampling_snapshot():
    """
    Graph with 1 hub node (node 0) connected to 10 neighbors (nodes 1..10)
    """
    with tempfile.TemporaryDirectory() as tmpdir:
        snap_path = os.path.join(tmpdir, "sampling_test.imps")
        with Writer(snap_path) as w:
            w.add_domain(0, 4, "Node")
            row_offsets = [0, 10] + [10] * 10  # node 0 has 10 edges, nodes 1..10 have 0 edges
            col_indices = list(range(1, 11))
            w.add_relation(
                src_domain_id=0,
                tgt_domain_id=0,
                encoding_type=0,
                node_count=11,
                edge_count=10,
                section_features=0,
                row_offsets=row_offsets,
                col_indices=col_indices,
            )
            w.finalize()
        yield snap_path


def test_sample_neighbors(sampling_snapshot):
    with Snapshot(sampling_snapshot) as snap:
        # Sample 3 neighbors for node 0
        src_arr, tgt_arr = snap.sample_neighbors(relation_index=0, nodes=[0], k_samples=3, seed=42)
        assert len(src_arr) == 3
        assert len(tgt_arr) == 3
        assert all(s == 0 for s in src_arr)
        assert all(1 <= t <= 10 for t in tgt_arr)

        # Sample with numpy array input
        nodes_np = np.array([0], dtype=np.uint64)
        src_arr2, tgt_arr2 = snap.sample_neighbors(
            relation_index=0, nodes=nodes_np, k_samples=5, seed=123
        )
        assert len(src_arr2) == 5
        assert len(tgt_arr2) == 5
