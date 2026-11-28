import hashlib
import os
import tempfile

import numpy as np
import pytest

from impulse_graph import Snapshot, Writer


def test_python_phase1_metadata_introspection():
    with tempfile.TemporaryDirectory() as tmpdir:
        path = os.path.join(tmpdir, "meta_test.imps")
        w = Writer(path)
        w.add_domain(0, 0x03, "Account")
        w.set_domain_node_count(0, 3)
        w.add_domain_attribute_decimal64(
            0,
            "balance",
            12,
            2,
            np.array([10050, 20075, 30000], dtype=np.int64).tobytes(),
        )
        rows = np.array([0, 1, 2, 2], dtype=np.uint32)
        cols = np.array([1, 2], dtype=np.uint32)
        w.add_relation(0, 0, 0, 3, 2, 0, rows, cols)
        w.add_attribute_decimal64(
            0,
            "amount",
            10,
            2,
            np.array([500, 1250], dtype=np.int64).tobytes(),
        )
        w.add_index(
            0,
            0xFFFF,
            0,
            0x01,
            "idx_acct_balance",
            np.array([2, 0, 1], dtype=np.uint32).tobytes(),
            0xCAFEBABE,
        )
        w.set_metadata("z_key", "last")
        w.set_metadata("a_key", "first")
        w.set_metadata("🚀_emoji", "rocket")
        w.set_metadata("\uff21_fullwidth", "fw")
        w.set_metadata("big_key", "X" * 8192)
        w.finalize()
        w.destroy()

        with Snapshot(path) as snap:
            hdr = snap.header()
            assert hdr["magic"] == 0x494D5053
            assert hdr["version"] == 11
            assert hdr["data_offset"] == 4096
            assert hdr["domain_count"] == 1
            assert hdr["relation_count"] == 1
            assert hdr["index_count"] == 1
            assert hdr["file_size"] == os.path.getsize(path)
            assert snap.file_size() == hdr["file_size"]
            assert len(hdr["snapshot_uuid"]) == 16

            with open(path, "rb") as f:
                expected_sha = hashlib.sha256(f.read()).hexdigest()
            assert snap.sha256() == expected_sha

            doms = snap.domains()
            assert len(doms) == 1
            assert doms[0]["domain_id"] == 0
            assert doms[0]["name"] == "Account"
            assert doms[0]["node_count"] == 3
            assert doms[0]["attr_count"] == 1
            assert doms[0]["key_attr_index"] == 0xFFFF

            assert snap.get_domain_attribute_count(0) == 1
            da = snap.get_domain_attribute_info(0, 0)
            assert da["name"] == "balance"
            assert da["type_code"] == 0x15
            assert da["base_type_code"] == 0x15
            assert da["is_nullable"] is False
            assert da["dimension"] == 1
            assert da["precision"] == 12
            assert da["scale"] == 2
            assert da["data_bytes"] == 24

            rels = snap.relations()
            assert len(rels) == 1
            assert rels[0]["relation_id"] == 0
            assert rels[0]["attr_count"] == 1
            assert rels[0]["has_csc"] is False
            assert rels[0]["has_coo"] is False
            assert rels[0]["is_virtual"] is False
            assert rels[0]["virtual_component_ids"] == []

            ra = snap.get_attribute_info(0, 0)
            assert ra["name"] == "amount"
            assert ra["type_code"] == 0x15
            assert ra["precision"] == 10
            assert ra["scale"] == 2
            assert ra["data_bytes"] == 16

            assert snap.index_count() == 1
            idxs = snap.indices()
            assert len(idxs) == 1
            assert idxs[0]["name"] == "idx_acct_balance"
            assert idxs[0]["domain_id"] == 0
            assert idxs[0]["relation_id"] == 0xFFFF
            assert idxs[0]["payload_feature_mask"] == 0xCAFEBABE

            meta = snap.metadata()
            assert meta["a_key"] == "first"
            assert meta["z_key"] == "last"
            assert meta["🚀_emoji"] == "rocket"
            assert meta["\uff21_fullwidth"] == "fw"
            assert len(meta["big_key"]) == 8192
            assert snap.get_metadata("🚀_emoji") == "rocket"
            assert snap.get_metadata("\uff21_fullwidth") == "fw"
            assert snap.get_metadata("big_key") == "X" * 8192
            assert snap.get_metadata("missing") is None
            assert snap.get_metadata("missing", "fallback") == "fallback"

            with pytest.raises(IndexError):
                snap.get_domain_attribute_info(0, 99)
            with pytest.raises(IndexError):
                snap.get_attribute_info(0, 99)
            with pytest.raises(IndexError):
                snap.get_domain(99)
            with pytest.raises(IndexError):
                snap.get_relation(99)
            with pytest.raises(IndexError):
                snap.get_index(1)
            assert rels[0]["encoding_id"] == rels[0]["encoding_type"]

        with pytest.raises(RuntimeError):
            snap.header()
        with pytest.raises(RuntimeError):
            snap.sha256()
        with pytest.raises(RuntimeError):
            snap.get_metadata("missing")
