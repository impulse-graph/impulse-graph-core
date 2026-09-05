import os
import tempfile

from impulse_graph import Writer


def test_writer_create_and_finalize():
    with tempfile.TemporaryDirectory() as tmpdir:
        snapshot_path = os.path.join(tmpdir, "test_graph.imps")
        writer = Writer(snapshot_path)
        writer.add_domain(0, 1, "User")
        writer.add_domain(1, 1, "Document")

        row_offsets = [0, 2, 3, 3]  # 3 source nodes, node 0 has 2 edges, node 1 has 1 edge
        col_indices = [10, 20, 30]

        writer.add_relation(
            src_domain_id=0,
            tgt_domain_id=1,
            encoding_type=0,  # RAW_UINT32
            node_count=3,
            edge_count=3,
            section_features=0,
            row_offsets=row_offsets,
            col_indices=col_indices,
        )
        writer.finalize()

        assert os.path.exists(snapshot_path)
        assert os.path.getsize(snapshot_path) > 4096
