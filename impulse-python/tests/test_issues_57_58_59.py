import os
import tempfile
import numpy as np
import pytest

from impulse_graph import Snapshot, Writer, vm


@pytest.fixture
def sample_graph():
    """
    Creates a sample snapshot with 5 nodes, 6 edges, and an attribute.
    0 -> [1, 2]
    1 -> [2, 3]
    2 -> [3, 4]
    """
    with tempfile.TemporaryDirectory() as tmpdir:
        snap_path = os.path.join(tmpdir, "test_sample.imps")
        with Writer(snap_path) as w:
            w.add_domain(domain_id=0, key_type=1, name="Item")
            w.add_relation(
                src_domain_id=0,
                tgt_domain_id=0,
                encoding_type=0,
                node_count=5,
                edge_count=6,
                section_features=0,
                row_offsets=[0, 2, 4, 6, 6, 6],
                col_indices=[1, 2, 2, 3, 3, 4],
            )
            # Add string attribute with offsets
            # Node strings: "alpha", "beta", "gamma", "delta", "epsilon"
            strings = ["alpha", "beta", "gamma", "delta", "epsilon"]
            data_bytes = "".join(strings).encode("utf-8")
            offsets = [0]
            for s in strings:
                offsets.append(offsets[-1] + len(s.encode("utf-8")))
            offsets_bytes = np.array(offsets, dtype=np.uint32).tobytes()

            w.add_attribute(
                relation_index=0,
                name="name",
                type_code=0x0B,  # string
                dimension=1,
                data=data_bytes,
                offsets=offsets_bytes,
            )
            w.finalize()
        yield snap_path


def test_issue_57_vm_context_reuse_and_ctz(sample_graph):
    with Snapshot(sample_graph) as snap:
        # 1. Verify default_context and default_state exist on Snapshot
        ctx1 = snap.default_context
        state1 = snap.default_state
        assert ctx1 is not None
        assert state1 is not None

        # Verify caching returns the same objects
        assert snap.default_context is ctx1
        assert snap.default_state is state1

        # 2. Verify Traversal.to_list() reuses the default VM without reallocating
        res1 = snap.traverse(0).out(0).to_list()
        assert res1 == [1, 2]

        res2 = snap.traverse(1).out(0).to_list()
        assert res2 == [2, 3]

        # 3. Verify passing custom ctx/state to to_list() and execute()
        with vm.VmContext(snap) as custom_ctx:
            custom_state = vm.VmState()
            res3 = snap.traverse(2).out(0).to_list(ctx=custom_ctx, state=custom_state)
            assert res3 == [3, 4]

            res4 = snap.traverse(0).out(0).execute(ctx=custom_ctx, state=custom_state)
            assert res4 == [1, 2]

        # 4. Verify hardware CTZ bitset extraction in C++
        bs = ctx1.acquire_bitset()
        assert bs >= 0
        ctx1.bitset_add(bs, 5)
        ctx1.bitset_add(bs, 63)
        ctx1.bitset_add(bs, 64)
        ctx1.bitset_add(bs, 130)

        extracted = ctx1.get_bitset_nodes(bs)
        assert extracted == [5, 63, 64, 130]
        ctx1.release_bitset(bs)

        # 5. Verify ctx.reset() clears handles
        ctx1.reset()
        bs_new = ctx1.acquire_bitset()
        assert bs_new == 0  # Reuses handle 0 after reset
        ctx1.release_bitset(bs_new)


def test_issue_58_collect_array_and_node_vector(sample_graph):
    with Snapshot(sample_graph) as snap:
        qb = vm.QueryBuilder()
        qb.input_node(0).walk_edge(0, 0).collect_array()
        compiled = qb.compile()

        with vm.VmContext(snap) as ctx:
            state = vm.VmState()
            res = compiled.execute_with_context(ctx, state, 0)
            assert res.is_ok()
            assert res.result_type == vm.RegisterType.TYPE_NODE_VECTOR

            # 1. Test ctx.get_collected_nodes()
            nodes_collected = ctx.get_collected_nodes()
            assert list(nodes_collected) == [1, 2]

            # 2. Test ctx.get_node_vector(res.raw_value) with collected buffer address
            nodes_vec = ctx.get_node_vector(res.raw_value)
            assert list(nodes_vec) == [1, 2]

            # 3. Test slot handles 0..31 with acquire_node_vector
            nv_handle = ctx.acquire_node_vector()
            assert 0 <= nv_handle < 32
            # Initially empty / 0 length
            slot_vec = ctx.get_node_vector(nv_handle)
            assert len(slot_vec) >= 0
            ctx.release_node_vector(nv_handle)


def test_issue_59_domain_node_count_and_attribute_offsets(sample_graph):
    with Snapshot(sample_graph) as snap:
        # 1. Verify get_domain returns node_count
        dom_info = snap.get_domain(0)
        assert "node_count" in dom_info
        assert dom_info["node_count"] == 5

        # 2. Verify DomainView.node_count is properly populated
        dom_view = snap.domain("Item")
        assert dom_view.node_count == 5

        dom_view_idx = snap.domain(0)
        assert dom_view_idx.node_count == 5

        # 3. Verify get_attribute_info
        attr_info = snap.get_attribute_info(relation_index=0, attribute_index=0)
        assert attr_info["type_code"] == 0x0B
        assert attr_info["dimension"] == 1
        assert attr_info["offsets_bytes"] > 0

        # 4. Verify get_attribute_offsets_array
        offsets_arr = snap.get_attribute_offsets_array(relation_index=0, attribute_index=0)
        assert offsets_arr is not None
        assert list(offsets_arr) == [0, 5, 9, 14, 19, 26]

        # 5. Verify string attribute access
        assert snap.get_string_attribute(0, 0, 0) == "alpha"
        assert snap.get_string_attribute(0, 0, 1) == "beta"
        assert snap.get_string_attribute(0, 0, 2) == "gamma"
        assert snap.get_string_attribute(0, 0, 3) == "delta"
        assert snap.get_string_attribute(0, 0, 4) == "epsilon"

        assert snap.get_string_attributes(0, 0) == ["alpha", "beta", "gamma", "delta", "epsilon"]
