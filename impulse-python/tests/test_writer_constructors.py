import os
import tempfile

import numpy as np
import pytest

from impulse_graph import Snapshot, Writer


def test_writer_from_scipy():
    sp = pytest.importorskip("scipy.sparse")
    with tempfile.TemporaryDirectory() as tmpdir:
        snap_path = os.path.join(tmpdir, "scipy_graph.imps")
        # 3x3 matrix: 0->1, 1->2, 2->0
        row = np.array([0, 1, 2], dtype=np.int32)
        col = np.array([1, 2, 0], dtype=np.int32)
        data = np.ones(3, dtype=np.float32)
        mat = sp.csr_matrix((data, (row, col)), shape=(3, 3))

        Writer.from_scipy(snap_path, mat, domain_name="Node")

        with Snapshot(snap_path) as snap:
            assert snap.domain_count() == 1
            assert snap.relation_count() == 1
            rel = snap.get_relation(0)
            assert rel["node_count"] == 3
            assert rel["edge_count"] == 3
            assert snap.is_reachable(0, 0, 1) is True
            assert snap.is_reachable(0, 1, 2) is True
            assert snap.is_reachable(0, 2, 0) is True
            assert snap.is_reachable(0, 0, 2) is False


def test_writer_from_dataframe():
    pd = pytest.importorskip("pandas")
    with tempfile.TemporaryDirectory() as tmpdir:
        snap_path = os.path.join(tmpdir, "df_graph.imps")
        df = pd.DataFrame(
            {
                "src": ["alice", "alice", "bob"],
                "dst": ["bob", "charlie", "charlie"],
            }
        )

        Writer.from_dataframe(snap_path, df, src_col="src", tgt_col="dst", domain_name="User")

        with Snapshot(snap_path) as snap:
            assert snap.domain_count() == 1
            assert snap.relation_count() == 1
            rel = snap.get_relation(0)
            assert rel["node_count"] == 3
            assert rel["edge_count"] == 3


def test_writer_from_networkx():
    nx = pytest.importorskip("networkx")
    with tempfile.TemporaryDirectory() as tmpdir:
        snap_path = os.path.join(tmpdir, "nx_graph.imps")
        G = nx.DiGraph()
        G.add_edge(0, 1)
        G.add_edge(1, 2)
        G.add_edge(2, 3)

        Writer.from_networkx(snap_path, G, domain_name="Node")

        with Snapshot(snap_path) as snap:
            assert snap.domain_count() == 1
            assert snap.relation_count() == 1
            assert snap.is_reachable(0, 0, 1) is True
            assert snap.is_reachable(0, 1, 2) is True
            assert snap.is_reachable(0, 2, 3) is True
            assert snap.is_reachable(0, 0, 3) is False


def test_writer_from_torch():
    torch = pytest.importorskip("torch")
    with tempfile.TemporaryDirectory() as tmpdir:
        snap_path = os.path.join(tmpdir, "torch_graph.imps")
        edge_index = torch.tensor([[0, 1, 2], [1, 2, 0]], dtype=torch.int64)

        Writer.from_torch(snap_path, edge_index, num_nodes=3, domain_name="Node")

        with Snapshot(snap_path) as snap:
            assert snap.domain_count() == 1
            assert snap.relation_count() == 1
            assert snap.is_reachable(0, 0, 1) is True
            assert snap.is_reachable(0, 1, 2) is True
            assert snap.is_reachable(0, 2, 0) is True
