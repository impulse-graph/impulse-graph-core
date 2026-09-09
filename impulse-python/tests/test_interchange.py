import os
import tempfile

import numpy as np
import pytest

from impulse_graph import Snapshot, Writer


@pytest.fixture
def sample_snapshot():
    with tempfile.TemporaryDirectory() as tmpdir:
        snap_path = os.path.join(tmpdir, "interchange_test.imps")
        with Writer(snap_path) as w:
            w.add_domain(0, 4, "Node")
            # 4 nodes, 5 edges: 0->1, 0->2, 1->2, 1->3, 2->3
            w.add_relation(
                src_domain_id=0,
                tgt_domain_id=0,
                encoding_type=0,
                node_count=4,
                edge_count=5,
                section_features=0,
                row_offsets=[0, 2, 4, 5, 5],
                col_indices=[1, 2, 2, 3, 3],
            )
            w.finalize()
        yield snap_path


def test_to_scipy_csr(sample_snapshot):
    sp = pytest.importorskip("scipy.sparse")
    with Snapshot(sample_snapshot) as snap:
        csr = snap.to_scipy_csr(relation_index=0)
        assert sp.isspmatrix_csr(csr)
        assert csr.shape == (4, 4)
        assert csr.nnz == 5
        assert np.array_equal(csr.indptr, [0, 2, 4, 5, 5])
        assert np.array_equal(csr.indices, [1, 2, 2, 3, 3])

        # Test transpose
        csc = snap.to_scipy_csr(relation_index=0, transpose=True)
        assert csc.shape == (4, 4)


def test_to_pandas(sample_snapshot):
    pd = pytest.importorskip("pandas")
    with Snapshot(sample_snapshot) as snap:
        df = snap.to_pandas(relation_index=0)
        assert isinstance(df, pd.DataFrame)
        assert len(df) == 5
        assert list(df.columns) == ["src", "dst"]
        assert list(df["src"]) == [0, 0, 1, 1, 2]
        assert list(df["dst"]) == [1, 2, 2, 3, 3]


def test_to_networkx(sample_snapshot):
    nx = pytest.importorskip("networkx")
    with Snapshot(sample_snapshot) as snap:
        g = snap.to_networkx(relation_index=0)
        assert isinstance(g, nx.DiGraph)
        assert g.number_of_nodes() == 4
        assert g.number_of_edges() == 5
        assert list(g.successors(0)) == [1, 2]
        assert list(g.successors(1)) == [2, 3]
        assert list(g.successors(2)) == [3]
        assert list(g.successors(3)) == []


def test_to_polars(sample_snapshot):
    pl = pytest.importorskip("polars")
    with Snapshot(sample_snapshot) as snap:
        df = snap.to_polars(relation_index=0)
        assert isinstance(df, pl.DataFrame)
        assert df.height == 5
        assert df.columns == ["src", "dst"]
        assert df["src"].to_list() == [0, 0, 1, 1, 2]
        assert df["dst"].to_list() == [1, 2, 2, 3, 3]


def test_to_arrow_table(sample_snapshot):
    pa = pytest.importorskip("pyarrow")
    with Snapshot(sample_snapshot) as snap:
        table = snap.to_arrow_table(relation_index=0)
        assert isinstance(table, pa.Table)
        assert table.num_rows == 5
        assert table.column_names == ["src", "dst"]


def test_to_torch_csr_and_edge_index(sample_snapshot):
    torch = pytest.importorskip("torch")
    with Snapshot(sample_snapshot) as snap:
        torch_csr = snap.to_torch_csr(relation_index=0)
        assert torch_csr.is_sparse_csr
        assert torch_csr.size() == (4, 4)

        edge_index = snap.to_torch_edge_index(relation_index=0)
        assert edge_index.shape == (2, 5)
        assert torch.equal(edge_index[0], torch.tensor([0, 0, 1, 1, 2], dtype=torch.int64))
        assert torch.equal(edge_index[1], torch.tensor([1, 2, 2, 3, 3], dtype=torch.int64))
