"""
Impulse Graph Engine Bytecode Virtual Machine & Fluent Query Pipeline Python SDK
"""

from typing import List, Callable, Optional, Tuple, Union
import numpy as np

try:
    import _impulse_native
    from _impulse_native import (
        QueryBuilder as _NativeQueryBuilder,
        CompiledQuery as _NativeCompiledQuery,
        QueryResult as _NativeQueryResult,
        VmContext as _NativeVmContext,
        VmState as _NativeVmState,
        RegisterType,
        VmStatus,
        Instruction,
        opcodes,
    )
except ImportError:
    _NativeQueryBuilder = None
    _NativeCompiledQuery = None
    _NativeQueryResult = None
    _NativeVmContext = None
    _NativeVmState = None
    RegisterType = None
    VmStatus = None
    Instruction = None
    opcodes = None


class VmContext:
    """
    Off-heap execution context pool managing bitsets, float/double vectors,
    node vectors, string vectors, and value maps during query execution.
    """

    def __init__(self, snapshot: Optional[object] = None):
        if _NativeVmContext is None:
            raise RuntimeError("_impulse_native extension is not compiled.")
        native_snap = getattr(snapshot, "_native", None) if snapshot else None
        self._native = _NativeVmContext(native_snap)

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.destroy()

    def destroy(self):
        if self._native is not None:
            self._native.destroy()

    def vector_size(self) -> int:
        return self._native.vector_size()

    def get_float_vector(self, handle: int) -> np.ndarray:
        return self._native.get_float_vector(handle)

    def get_double_vector(self, handle: int) -> np.ndarray:
        return self._native.get_double_vector(handle)

    def get_node_vector(self, handle: int) -> np.ndarray:
        return self._native.get_node_vector(handle)

    def acquire_bitset(self) -> int:
        return self._native.acquire_bitset()

    def release_bitset(self, handle: int):
        self._native.release_bitset(handle)

    def bitset_add(self, handle: int, node_id: int):
        self._native.bitset_add(handle, node_id)

    def bitset_test(self, handle: int, node_id: int) -> bool:
        return self._native.bitset_test(handle, node_id)

    def bitset_fill(self, handle: int, count: int):
        self._native.bitset_fill(handle, count)

    def bitset_get_word(self, handle: int, word_idx: int) -> int:
        return self._native.bitset_get_word(handle, word_idx)

    def acquire_float_vector(self) -> int:
        return self._native.acquire_float_vector()

    def release_float_vector(self, handle: int):
        self._native.release_float_vector(handle)

    def float_vector_set(self, handle: int, index: int, val: float):
        self._native.float_vector_set(handle, index, val)

    def acquire_double_vector(self) -> int:
        return self._native.acquire_double_vector()

    def release_double_vector(self, handle: int):
        self._native.release_double_vector(handle)

    def double_vector_set(self, handle: int, index: int, val: float):
        self._native.double_vector_set(handle, index, val)

    def acquire_node_vector(self) -> int:
        return self._native.acquire_node_vector()

    def release_node_vector(self, handle: int):
        self._native.release_node_vector(handle)

    def acquire_string_vector(self) -> int:
        return self._native.acquire_string_vector()

    def release_string_vector(self, handle: int):
        self._native.release_string_vector(handle)

    def string_vector_add(self, handle: int, val: str):
        self._native.string_vector_add(handle, val)

    def string_vector_size(self, handle: int) -> int:
        return self._native.string_vector_size(handle)

    def string_vector_get(self, handle: int, index: int) -> str:
        return self._native.string_vector_get(handle, index)

    def acquire_value_map(self) -> int:
        return self._native.acquire_value_map()

    def release_value_map(self, handle: int):
        self._native.release_value_map(handle)

    def value_map_size(self, handle: int) -> int:
        return self._native.value_map_size(handle)

    def value_map_get_key(self, handle: int, index: int) -> str:
        return self._native.value_map_get_key(handle, index)

    def value_map_get_value(self, handle: int, index: int) -> float:
        return self._native.value_map_get_value(handle, index)


class VmState:
    """
    VM Register and Execution State Frame (Program Counter, FLAGS, R0..R63 registers).
    """

    def __init__(self):
        if _NativeVmState is None:
            raise RuntimeError("_impulse_native extension is not compiled.")
        self._native = _NativeVmState()

    @property
    def pc(self) -> int:
        return self._native.pc

    @pc.setter
    def pc(self, value: int):
        self._native.pc = value

    @property
    def flags(self) -> int:
        return self._native.flags

    @flags.setter
    def flags(self, value: int):
        self._native.flags = value

    @property
    def call_stack_depth(self) -> int:
        return self._native.call_stack_depth

    def get_register(self, idx: int) -> int:
        return self._native.get_register(idx)

    def set_register(self, idx: int, val: int):
        self._native.set_register(idx, val)

    def get_register_type(self, idx: int) -> int:
        return self._native.get_register_type(idx)

    def set_register_type(self, idx: int, type_tag: int):
        self._native.set_register_type(idx, type_tag)


class QueryResult:
    """
    Result returned by VM execution containing status, target result register, type tag, and output value.
    """

    def __init__(self, native_result: Optional[_NativeQueryResult] = None):
        self._native = native_result if native_result is not None else _NativeQueryResult()

    @property
    def status(self) -> int:
        return self._native.status

    @property
    def result_register(self) -> int:
        return self._native.result_register

    @property
    def result_type(self) -> int:
        return self._native.result_type

    @property
    def raw_value(self) -> int:
        return self._native.raw_value

    def is_ok(self) -> bool:
        return self._native.is_ok()

    def as_int(self) -> int:
        return self._native.as_int()

    def as_float(self) -> float:
        return self._native.as_float()

    def as_double(self) -> float:
        return self._native.as_double()

    def test_bitset(self, ctx: VmContext, node_id: int) -> bool:
        return self._native.test_bitset(ctx._native, node_id)


class CompiledQuery:
    """
    Compiled VM bytecode program ready for single-shot or repeated execution on Snapshots.
    """

    def __init__(self, native_query: _NativeCompiledQuery):
        self._native = native_query

    def bytecode(self) -> List[Instruction]:

        return self._native.bytecode()

    def result_register(self) -> int:
        return self._native.result_register()

    def instruction_count(self) -> int:
        return self._native.instruction_count()

    def execute(self, snapshot: Optional[object] = None, input_param: int = 0) -> QueryResult:
        native_snap = getattr(snapshot, "_native", None) if snapshot else None
        return QueryResult(self._native.execute(native_snap, input_param))

    def execute_with_context(self, ctx: VmContext, state: VmState, input_param: int = 0) -> QueryResult:
        return QueryResult(self._native.execute_with_context(ctx._native, state._native, input_param))


class QueryBuilder:
    """
    Fluent C++ VM bytecode builder for constructing high-performance graph traversal algorithms.
    """

    def __init__(self, start_register: int = 0):
        if _NativeQueryBuilder is None:
            raise RuntimeError("_impulse_native extension is not compiled.")
        self._native = _NativeQueryBuilder(start_register)

    def input_node(self, dst_reg: int = 0) -> "QueryBuilder":
        self._native.input_node(dst_reg)
        return self

    def input_set(self, dst_reg: int = 0) -> "QueryBuilder":
        self._native.input_set(dst_reg)
        return self

    def load_const_int(self, value: int, dst_reg: int = 0) -> "QueryBuilder":
        self._native.load_const_int(value, dst_reg)
        return self

    def load_const_float(self, value: float, dst_reg: int = 0) -> "QueryBuilder":
        self._native.load_const_float(value, dst_reg)
        return self

    def load_const_str_prefix(self, prefix: str, dst_reg: int = 0) -> "QueryBuilder":
        self._native.load_const_str_prefix(prefix, dst_reg)
        return self

    def load_keys(self, keys: List[str], dst_reg: int = 0) -> "QueryBuilder":
        self._native.load_keys(keys, dst_reg)
        return self

    def walk_edge(self, relation_id: int, flags: int = 0) -> "QueryBuilder":
        self._native.walk_edge(relation_id, flags)
        return self

    def walk_edge_filtered(self, relation_id: int, filter_id: int) -> "QueryBuilder":
        self._native.walk_edge_filtered(relation_id, filter_id)
        return self

    def walk_edge_predicate(self, relation_id: int, filter_id: int) -> "QueryBuilder":
        self._native.walk_edge_predicate(relation_id, filter_id)
        return self

    def walk_degree(self, relation_id: int) -> "QueryBuilder":
        self._native.walk_degree(relation_id)
        return self

    def walk_reduce_sum(self, relation_id: int, val_reg: int) -> "QueryBuilder":
        self._native.walk_reduce_sum(relation_id, val_reg)
        return self

    def walk_csc(self, relation_id: int) -> "QueryBuilder":
        self._native.walk_csc(relation_id)
        return self

    def filter_node(self, filter_id: int) -> "QueryBuilder":
        self._native.filter_node(filter_id)
        return self

    def filter_node_str_prefix(self, prefix: str) -> "QueryBuilder":
        self._native.filter_node_str_prefix(prefix)
        return self

    def union_with(self, src_reg: int) -> "QueryBuilder":
        self._native.union_with(src_reg)
        return self

    def intersect_with(self, src_reg: int) -> "QueryBuilder":
        self._native.intersect_with(src_reg)
        return self

    def difference_with(self, src_reg: int) -> "QueryBuilder":
        self._native.difference_with(src_reg)
        return self

    def cardinality(self) -> "QueryBuilder":
        self._native.cardinality()
        return self

    def vector_mul_attr(self, attr_reg: int) -> "QueryBuilder":
        self._native.vector_mul_attr(attr_reg)
        return self

    def vector_reduce_sum(self) -> "QueryBuilder":
        self._native.vector_reduce_sum()
        return self

    def vector_div(self, denom_reg: int) -> "QueryBuilder":
        self._native.vector_div(denom_reg)
        return self

    def l1_norm_diff(self, other_reg: int) -> "QueryBuilder":
        self._native.l1_norm_diff(other_reg)
        return self

    def matrix_vector_mul(self, matrix_reg: int, semiring_id: int = 0) -> "QueryBuilder":
        self._native.matrix_vector_mul(matrix_reg, semiring_id)
        return self

    def vector_matrix_mul(self, matrix_reg: int, semiring_id: int = 0) -> "QueryBuilder":
        self._native.vector_matrix_mul(matrix_reg, semiring_id)
        return self

    def ewise_add(self, other_reg: int, binary_op: int = 0) -> "QueryBuilder":
        self._native.ewise_add(other_reg, binary_op)
        return self

    def ewise_mult(self, other_reg: int, binary_op: int = 1) -> "QueryBuilder":
        self._native.ewise_mult(other_reg, binary_op)
        return self

    def reduce(self, binary_op: int = 0) -> "QueryBuilder":
        self._native.reduce(binary_op)
        return self

    def afforest(self) -> "QueryBuilder":
        self._native.afforest()
        return self

    def tc_sweep_batch(self) -> "QueryBuilder":
        self._native.tc_sweep_batch()
        return self

    def brandes_forward(self) -> "QueryBuilder":
        self._native.brandes_forward()
        return self

    def brandes_backward(self) -> "QueryBuilder":
        self._native.brandes_backward()
        return self

    def delta_step_relax(self, weight_reg: int) -> "QueryBuilder":
        self._native.delta_step_relax(weight_reg)
        return self

    def sample_neighbors(self, relation_id: int, k_samples: int, seed: int = 0) -> "QueryBuilder":
        self._native.sample_neighbors(relation_id, k_samples, seed)
        return self

    def random_walk(self, relation_id: int, steps: int, seed: int = 0) -> "QueryBuilder":
        self._native.random_walk(relation_id, steps, seed)
        return self

    def scatter_gather(self) -> "QueryBuilder":
        self._native.scatter_gather()
        return self

    def rebac_check(self, permission_id: int) -> "QueryBuilder":
        self._native.rebac_check(permission_id)
        return self

    def roaring_bitmap_and(self, other_reg: int) -> "QueryBuilder":
        self._native.roaring_bitmap_and(other_reg)
        return self

    def island_detect(self, secondary_reg: int) -> "QueryBuilder":
        self._native.island_detect(secondary_reg)
        return self

    def sparse_mat_vec(self) -> "QueryBuilder":
        self._native.sparse_mat_vec()
        return self

    def louvain_modularity(self) -> "QueryBuilder":
        self._native.louvain_modularity()
        return self

    def kcore_decomposition(self) -> "QueryBuilder":
        self._native.kcore_decomposition()
        return self

    def motif_match_3(self) -> "QueryBuilder":
        self._native.motif_match_3()
        return self

    def graph_isomorphism(self) -> "QueryBuilder":
        self._native.graph_isomorphism()
        return self

    def mov(self, dst_reg: int, src_reg: int) -> "QueryBuilder":
        self._native.mov(dst_reg, src_reg)
        return self

    def clear_reg(self, reg: int) -> "QueryBuilder":
        self._native.clear_reg(reg)
        return self

    def nop(self) -> "QueryBuilder":
        self._native.nop()
        return self

    def repeat(self, count: int, body: Callable[["QueryBuilder"], None]) -> "QueryBuilder":
        def native_body(native_qb):
            wrapper = QueryBuilder.__new__(QueryBuilder)
            wrapper._native = native_qb
            body(wrapper)

        self._native.repeat(count, native_body)
        return self

    def repeat_until_stable(self, body: Callable[["QueryBuilder"], None]) -> "QueryBuilder":
        def native_body(native_qb):
            wrapper = QueryBuilder.__new__(QueryBuilder)
            wrapper._native = native_qb
            body(wrapper)

        self._native.repeat_until_stable(native_body)
        return self

    def jmp(self, instruction_offset: int) -> "QueryBuilder":
        self._native.jmp(instruction_offset)
        return self

    def jz(self, instruction_offset: int) -> "QueryBuilder":
        self._native.jz(instruction_offset)
        return self

    def jnz(self, instruction_offset: int) -> "QueryBuilder":
        self._native.jnz(instruction_offset)
        return self

    def collect_bitset(self) -> "QueryBuilder":
        self._native.collect_bitset()
        return self

    def collect_array(self) -> "QueryBuilder":
        self._native.collect_array()
        return self

    def map_dense_to_keys(self) -> "QueryBuilder":
        self._native.map_dense_to_keys()
        return self

    def collect_value_map(self) -> "QueryBuilder":
        self._native.collect_value_map()
        return self

    def allocate_register(self) -> int:
        return self._native.allocate_register()

    @property
    def current_register(self) -> int:
        return self._native.current_register

    @current_register.setter
    def current_register(self, reg: int):
        self._native.current_register = reg

    def raw_instructions(self) -> List[Instruction]:

        return self._native.raw_instructions()

    def compile(self) -> CompiledQuery:
        return CompiledQuery(self._native.compile())


__all__ = [
    "VmContext",
    "VmState",
    "QueryResult",
    "CompiledQuery",
    "QueryBuilder",
    "RegisterType",
    "VmStatus",
    "Instruction",
    "opcodes",
]
