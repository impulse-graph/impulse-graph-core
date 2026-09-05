import os
import tempfile

import pytest

from impulse_graph import Snapshot, Writer, vm
from impulse_graph.cypher import CypherQuery


@pytest.fixture
def dummy_snapshot():
    with tempfile.TemporaryDirectory() as tmpdir:
        snap_path = os.path.join(tmpdir, "error_test.imps")
        with Writer(snap_path) as w:
            w.add_domain(0, 1, "User")
            w.add_relation(
                src_domain_id=0,
                tgt_domain_id=0,
                encoding_type=0,
                node_count=2,
                edge_count=1,
                section_features=0,
                row_offsets=[0, 1, 1],
                col_indices=[1],
            )
            w.finalize()
        yield snap_path


def test_invalid_domain_and_relation_indices(dummy_snapshot):
    with Snapshot(dummy_snapshot) as snap:
        with pytest.raises((IndexError, ValueError, RuntimeError)):
            snap.get_domain(99)

        with pytest.raises((IndexError, ValueError, RuntimeError)):
            snap.get_relation(99)


def test_closed_snapshot_operations(dummy_snapshot):
    snap = Snapshot(dummy_snapshot)
    snap.close()

    with pytest.raises(RuntimeError, match="closed"):
        snap.domain_count()

    with pytest.raises(RuntimeError, match="closed"):
        snap.get_domain(0)

    with pytest.raises(RuntimeError, match="closed"):
        snap.get_relation(0)


def test_finalized_writer_operations():
    with tempfile.TemporaryDirectory() as tmpdir:
        snap_path = os.path.join(tmpdir, "finalized_writer.imps")
        w = Writer(snap_path)
        w.add_domain(0, 1, "User")
        w.finalize()
        w.destroy()

        with pytest.raises(RuntimeError, match="destroyed"):
            w.add_domain(1, 1, "Account")


def test_invalid_cypher_syntax():
    # Missing MATCH clause
    with pytest.raises(ValueError, match="missing MATCH"):
        CypherQuery("RETURN 42")

    # Untyped relationship
    with pytest.raises(ValueError, match="typed relationship"):
        CypherQuery("MATCH (u)-->(v) RETURN v")


def test_vm_state_register_bounds():
    state = vm.VmState()
    with pytest.raises(IndexError):
        state.get_register(64)

    with pytest.raises(IndexError):
        state.set_register(100, 42)


def test_traversal_unknown_relation(dummy_snapshot):
    with Snapshot(dummy_snapshot) as snap:
        t = snap.traverse(0)
        with pytest.raises(ValueError, match="Unknown relation"):
            t.out("NON_EXISTENT_REL")
