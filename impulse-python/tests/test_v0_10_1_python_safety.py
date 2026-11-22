"""TDD regression tests for impulse-graph-core v0.10.1 Python bindings (#138, #144)."""

import gc
import tempfile
from pathlib import Path

import numpy as np
import pytest

import impulse_graph as ig


def _build_small_snapshot(path: Path) -> None:
    row_offsets = np.array([0, 2, 3, 4, 4], dtype=np.uint32)
    col_indices = np.array([1, 2, 2, 3], dtype=np.uint32)
    weights = np.array([1.5, 2.5, 3.5, 4.5], dtype=np.float32)

    with ig.Writer(str(path)) as w:
        w.add_domain(0, 4, "User")
        w.add_relation(0, 0, 0, 4, 4, 0, row_offsets, col_indices)
        w.add_attribute(0, "weight", 0x06, 1, weights.tobytes())
        w.finalize()


def test_issue_144_version_not_stale_0_9_0():
    assert ig.__version__ != "0.9.0", f"Stale __version__: {ig.__version__}"
    assert isinstance(ig.__version__, str) and len(ig.__version__) >= 5


def test_issue_138_memoryview_survives_snapshot_close():
    """Exported memoryviews and NumPy arrays must not SIGSEGV after Snapshot.close()."""
    with tempfile.TemporaryDirectory() as tmpdir:
        snap_path = Path(tmpdir) / "pin_test.imps"
        _build_small_snapshot(snap_path)

        with ig.Snapshot(str(snap_path)) as snap:
            arr_off = snap.get_row_offsets_array(0)
            arr_col = snap.get_col_indices_array(0)
            mv_attr = snap._native.get_attribute_data(0, 0)
            arr_attr = np.frombuffer(mv_attr, dtype=np.float32)

        # Snapshot context has exited (close() called) — views MUST remain valid!
        gc.collect()
        assert arr_off.tolist() == [0, 2, 3, 4, 4]
        assert arr_col.tolist() == [1, 2, 2, 3]
        assert np.allclose(arr_attr, [1.5, 2.5, 3.5, 4.5])

        # Releasing views cleanly releases the underlying mmap lease
        del arr_off, arr_col, arr_attr, mv_attr
        gc.collect()


def test_issue_138_closed_snapshot_rejects_new_calls():
    with tempfile.TemporaryDirectory() as tmpdir:
        snap_path = Path(tmpdir) / "closed_test.imps"
        _build_small_snapshot(snap_path)

        snap = ig.Snapshot(str(snap_path))
        snap.close()
        with pytest.raises(RuntimeError):
            snap.get_row_offsets_array(0)
        with pytest.raises(RuntimeError):
            snap.is_reachable(0, 0, 1)
