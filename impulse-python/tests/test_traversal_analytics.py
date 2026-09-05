import os
import tempfile

import pytest

from impulse_graph import Snapshot, Writer, vm


@pytest.fixture
def linear_chain_snapshot():
    """
    Directed chain graph: 0 -> 1 -> 2 -> 3 -> 4
    """
    with tempfile.TemporaryDirectory() as tmpdir:
        snap_path = os.path.join(tmpdir, "chain.imps")
        with Writer(snap_path) as w:
            w.add_domain(0, 1, "Node")
            row_offsets = [0, 1, 2, 3, 4, 4]
            col_indices = [1, 2, 3, 4]
            w.add_relation(
                src_domain_id=0,
                tgt_domain_id=0,
                encoding_type=0,
                node_count=5,
                edge_count=4,
                section_features=0,
                row_offsets=row_offsets,
                col_indices=col_indices,
            )
            w.finalize()
        yield snap_path


def test_unrolled_multi_hop_traversal(linear_chain_snapshot):
    with Snapshot(linear_chain_snapshot) as snap:
        # 2 hops: node 0 -> node 1 -> node 2
        qb = vm.QueryBuilder()
        qb.input_node(0).walk_edge(0).walk_edge(0).collect_bitset()
        compiled = qb.compile()

        with vm.VmContext(snap) as ctx:
            state = vm.VmState()
            res = compiled.execute_with_context(ctx, state, input_param=0)
            assert res.is_ok()
            assert res.test_bitset(ctx, 2) is True
            assert res.test_bitset(ctx, 1) is False
            assert res.test_bitset(ctx, 0) is False

        # Traversal API 2-hop
        nodes = snap.traverse(0).out(0).out(0).to_list()
        assert nodes == [2]


def test_fixed_hop_repeat_loop(linear_chain_snapshot):
    with Snapshot(linear_chain_snapshot) as snap:
        qb = vm.QueryBuilder()
        qb.input_node(0)

        def step(b):
            b.walk_edge(0)
            b.mov(0, b.current_register)
            b.current_register = 0

        qb.repeat(2, step)
        qb.collect_bitset()
        compiled = qb.compile()

        with vm.VmContext(snap) as ctx:
            state = vm.VmState()
            res = compiled.execute_with_context(ctx, state, input_param=0)
            assert res.is_ok()
            assert res.test_bitset(ctx, 2) is True


def test_traversal_degree(linear_chain_snapshot):
    with Snapshot(linear_chain_snapshot) as snap:
        # Traversal with degree step
        t = snap.traverse(0).degree(0)
        compiled = t.compile()
        assert compiled.instruction_count() > 0


def test_traversal_filtering(linear_chain_snapshot):
    with Snapshot(linear_chain_snapshot) as snap:
        t1 = snap.traverse(0).out(0).filter(filter_id=1)
        assert t1.compile().instruction_count() > 0

        t2 = snap.traverse(0).out(0).filter(prefix="prefix_")
        assert t2.compile().instruction_count() > 0


def test_traversal_disassembly(linear_chain_snapshot):
    with Snapshot(linear_chain_snapshot) as snap:
        t = snap.traverse(0).out(0).filter(prefix="NODE_").out(0)
        impas = t.disassemble()
        assert "OP_INIT_INPUT_NODE" in impas
        assert "OP_CSR_WALK" in impas
        assert "OP_FILTER_STR_PREFIX" in impas
        assert "OP_COLLECT_BITSET" in impas
        assert "OP_HALT" in impas
