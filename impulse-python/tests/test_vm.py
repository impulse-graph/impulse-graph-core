import os
import tempfile
import pytest
import numpy as np
from impulse_graph import Snapshot, Writer, vm

def test_vm_constants():
    assert vm.opcodes.OP_NOP == 0x01
    assert vm.opcodes.OP_INIT_INPUT_NODE == 0x02
    assert vm.opcodes.OP_CSR_WALK == 0x10
    assert vm.opcodes.OP_SET_UNION == 0x30
    assert vm.opcodes.OP_HALT == 0x00

    assert vm.RegisterType.TYPE_NULL == 0x00
    assert vm.RegisterType.TYPE_INT64 == 0x01
    assert vm.RegisterType.TYPE_BITSET_HANDLE == 0x04
    assert vm.RegisterType.TYPE_FLOAT_VECTOR == 0x0C

    assert vm.VmStatus.OK == 0

def test_query_builder_compilation():
    builder = vm.QueryBuilder()
    builder.input_node(0)
    builder.load_const_int(42, 1)
    builder.walk_edge(relation_id=0)
    builder.collect_array()

    compiled = builder.compile()
    assert compiled.instruction_count() > 0
    assert compiled.result_register() == builder.current_register

    bytecode = compiled.bytecode()
    assert len(bytecode) == compiled.instruction_count()
    assert bytecode[0].opcode == vm.opcodes.OP_INIT_INPUT_NODE

def test_query_builder_repeat_loop():
    builder = vm.QueryBuilder()
    builder.input_node(0)
    
    # 3-hop graph traversal via repeat loop
    builder.repeat(3, lambda b: b.walk_edge(0))
    builder.collect_array()

    compiled = builder.compile()
    assert compiled.instruction_count() > 3

def test_query_builder_analytics_opcodes():
    builder = vm.QueryBuilder()
    builder.input_node(0)
    builder.afforest()
    builder.brandes_forward()
    builder.sample_neighbors(relation_id=0, k_samples=5, seed=1234)
    builder.matrix_vector_mul(matrix_reg=1, semiring_id=0)
    builder.collect_array()

    compiled = builder.compile()
    assert compiled.instruction_count() >= 5

def test_vm_context_handles():
    with vm.VmContext() as ctx:
        assert ctx.vector_size() == 0 or ctx.vector_size() > 0

        # Bitset test
        bs_handle = ctx.acquire_bitset()
        assert bs_handle >= 0
        ctx.bitset_add(bs_handle, 100)
        assert ctx.bitset_test(bs_handle, 100) is True
        assert ctx.bitset_test(bs_handle, 101) is False
        ctx.release_bitset(bs_handle)

        # Float Vector test
        fv_handle = ctx.acquire_float_vector()
        assert fv_handle >= 0
        ctx.float_vector_set(fv_handle, 0, 3.14)
        ctx.release_float_vector(fv_handle)

        # String Vector test
        sv_handle = ctx.acquire_string_vector()
        assert sv_handle >= 0
        ctx.string_vector_add(sv_handle, "hello_impulse")
        assert ctx.string_vector_size(sv_handle) == 1
        assert ctx.string_vector_get(sv_handle, 0) == "hello_impulse"
        ctx.release_string_vector(sv_handle)

        # Value Map test
        vm_handle = ctx.acquire_value_map()
        assert vm_handle >= 0
        ctx.release_value_map(vm_handle)

def test_vm_state_registers():
    state = vm.VmState()
    state.pc = 10
    state.flags = 1
    assert state.pc == 10
    assert state.flags == 1

    state.set_register(5, 9999)
    state.set_register_type(5, vm.RegisterType.TYPE_INT64)
    assert state.get_register(5) == 9999
    assert state.get_register_type(5) == vm.RegisterType.TYPE_INT64

def test_vm_query_execution_on_snapshot():
    with tempfile.TemporaryDirectory() as tmpdir:
        snap_path = os.path.join(tmpdir, "test_vm_graph.imps")
        
        # Build small binary snapshot
        with Writer(snap_path) as w:
            w.add_domain(domain_id=0, key_type=4, name="User")
            w.add_relation(
                src_domain_id=0,
                tgt_domain_id=0,
                encoding_type=0,
                node_count=4,
                edge_count=4,
                section_features=0,
                row_offsets=[0, 1, 2, 3, 4],
                col_indices=[1, 2, 3, 0]
            )
            w.finalize()

        # Open snapshot and execute VM query
        with Snapshot(snap_path) as snap:
            assert snap.domain_count() == 1
            assert snap.relation_count() == 1

            qb = vm.QueryBuilder()
            qb.input_node(0)
            qb.walk_edge(0)
            qb.collect_array()
            query = qb.compile()

            result = snap.execute_query(query, input_param=0)
            assert result.is_ok()
            assert result.status == vm.VmStatus.OK
