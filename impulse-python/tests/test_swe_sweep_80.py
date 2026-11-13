"""Regression tests for impulse-graph-core#80 (Python SWE-sweep)."""

import gc
import tempfile
from pathlib import Path

import numpy as np
import pytest

from impulse_graph import Snapshot, Writer, vm


def _make_bipartite_snapshot(path: Path) -> None:
    with Writer(str(path)) as w:
        # Register domain_id=1 before domain_id=0 so catalog index != domain_id
        w.add_domain(1, 1, "Item")
        w.add_domain(0, 1, "User")

        # Rel 0: 3 users -> 5 items (u0 -> [0, 2], u1 -> [4], u2 -> [])
        w.add_relation(
            0,
            1,
            0,
            3,
            3,
            0,
            [0, 2, 3, 3],
            [0, 2, 4],
        )
        # Rel 1: 5 items -> 3 users so Item domain reports node_count=5
        w.add_relation(
            1,
            0,
            0,
            5,
            3,
            0,
            [0, 1, 1, 2, 2, 3],
            [0, 0, 1],
        )
        w.finalize()


def test_vm_exports_not_nullified():
    assert vm.RegisterType is not None
    assert vm.VmStatus is not None
    assert vm.Instruction is not None
    assert vm.opcodes is not None


def test_numpy_vector_views_keep_vm_context_alive():
    with tempfile.TemporaryDirectory() as tmp:
        snap_path = Path(tmp) / "vec.imps"
        _make_bipartite_snapshot(snap_path)
        with Snapshot(str(snap_path)) as snap:
            ctx = vm.VmContext(snap)
            qb = vm.QueryBuilder()
            qb.input_node(0).walk_edge(0).collect_array()
            compiled = qb.compile()
            st = vm.VmState()
            res = compiled.execute_with_context(ctx, st, 0)
            assert res.is_ok()
            arr = ctx.get_collected_nodes()

            del ctx
            gc.collect()

            assert list(arr) == [0, 2]


def test_non_contiguous_domain_id_and_traversal():
    with tempfile.TemporaryDirectory() as tmp:
        snap_path = Path(tmp) / "dom.imps"
        _make_bipartite_snapshot(snap_path)
        with Snapshot(str(snap_path)) as snap:
            user_dom = snap.domain(0, catalog={"PURCHASED": 0})
            assert user_dom.domain_name == "User"
            assert user_dom.domain_id == 0

            items = user_dom.from_node(0).out("PURCHASED").to_list()
            assert items == [0, 2]

            with pytest.raises(KeyError):
                snap.domain(999)


def test_traversal_degree_does_not_corrupt_bitset_pool():
    with tempfile.TemporaryDirectory() as tmp:
        snap_path = Path(tmp) / "deg.imps"
        _make_bipartite_snapshot(snap_path)
        with Snapshot(str(snap_path)) as snap:
            res = snap.traverse(0).degree(0).to_list()
            assert res == [2]


def test_bipartite_transpose_scipy_csr_shape():
    scipy_sparse = pytest.importorskip("scipy.sparse")
    assert scipy_sparse is not None
    with tempfile.TemporaryDirectory() as tmp:
        snap_path = Path(tmp) / "bipartite.imps"
        _make_bipartite_snapshot(snap_path)
        with Snapshot(str(snap_path)) as snap:
            fwd = snap.to_scipy_csr(0, transpose=False)
            assert fwd.shape == (3, 5)
            rev = snap.to_scipy_csr(0, transpose=True)
            assert rev.shape == (5, 3)


def test_cypher_string_literal_seed_resolution(monkeypatch):
    with tempfile.TemporaryDirectory() as tmp:
        snap_path = Path(tmp) / "cypher_str.imps"
        with Writer(str(snap_path)) as w:
            w.add_domain(0, 1, "User")
            w.add_relation(0, 0, 0, 2, 1, 0, [0, 1, 1], [1])
            w.finalize()

        with Snapshot(str(snap_path)) as snap:
            seen = {}

            def fake_resolve(domain_id: int, key: str) -> int:
                seen["domain_id"] = domain_id
                seen["key"] = key
                return 0

            monkeypatch.setattr(snap, "resolve_dense_id", fake_resolve)
            res = snap.cypher(
                "MATCH (u)-[:KNOWS]->(v) WHERE u.name = 'alice' RETURN v",
                catalog={"KNOWS": 0},
            )
            assert seen == {"domain_id": 0, "key": "alice"}
            assert res == [1]
