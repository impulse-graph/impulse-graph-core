"""
Impulse Graph Engine Python SDK & C++ Bytecode VM Binding
Spec v0.9.0
"""

from decimal import Decimal, ROUND_HALF_EVEN
from typing import Dict, List, Optional, Union

import numpy as np

# Attribute type codes (v0.11)
ATTR_TYPE_BOOL: int = 0x0F
ATTR_TYPE_DECIMAL64: int = 0x15

# ImpulseVM v0.11 Decimal64 status and opcode constants
IMPULSE_VM_ERR_ARITHMETIC_OVERFLOW: int = 16
IMPULSE_VM_ERR_DIV_BY_ZERO: int = 17
OP_SCALAR_DECIMAL_ALU: int = 0x15
OP_VEC_DECIMAL_ALU: int = 0x58

_INT64_MIN = -(1 << 63)
_INT64_MAX = (1 << 63) - 1


class Decimal64:
    """Fixed-point DECIMAL64 (int64 mantissa with 1 <= precision <= 18, 0 <= scale <= precision)."""

    __slots__ = ("mantissa", "scale", "precision")

    def __init__(self, mantissa: int, scale: int = 2, precision: int = 18):
        if precision < 1 or precision > 18:
            raise ValueError(f"precision must be in [1, 18], got {precision}")
        if scale < 0 or scale > precision:
            raise ValueError(f"scale must be in [0, {precision}], got {scale}")
        if mantissa < _INT64_MIN or mantissa > _INT64_MAX:
            raise OverflowError(f"mantissa {mantissa} out of int64 range")
        limit = 10**precision
        if abs(mantissa) >= limit:
            raise OverflowError(f"mantissa {mantissa} exceeds precision {precision}")
        self.mantissa = int(mantissa)
        self.scale = int(scale)
        self.precision = int(precision)

    @classmethod
    def from_decimal(
        cls,
        value: Union[Decimal, int, str],
        scale: int = 2,
        precision: int = 18,
        rounding=ROUND_HALF_EVEN,
    ) -> "Decimal64":
        dec = value if isinstance(value, Decimal) else Decimal(str(value))
        quant = Decimal(1).scaleb(-scale)
        scaled = dec.quantize(quant, rounding=rounding)
        mantissa = int((scaled * (Decimal(10) ** scale)).to_integral_exact())
        return cls(mantissa=mantissa, scale=scale, precision=precision)

    def to_decimal(self) -> Decimal:
        return Decimal(self.mantissa).scaleb(-self.scale)

    def to_int64(self) -> int:
        return self.mantissa

    @classmethod
    def from_int64(cls, mantissa: int, scale: int = 2, precision: int = 18) -> "Decimal64":
        return cls(mantissa=mantissa, scale=scale, precision=precision)

    def __eq__(self, other: object) -> bool:
        if isinstance(other, Decimal64):
            return self.to_decimal() == other.to_decimal()
        return NotImplemented

    def __repr__(self) -> str:
        return f"Decimal64({self.to_decimal()}, precision={self.precision}, scale={self.scale})"


def decimal_to_scaled_i64(
    value: Union[Decimal, int, str],
    scale: int = 2,
    precision: int = 18,
    rounding=ROUND_HALF_EVEN,
) -> int:
    """Convert a Decimal/int/str into a scaled int64 mantissa for DECIMAL64 storage."""
    return Decimal64.from_decimal(value, scale=scale, precision=precision, rounding=rounding).mantissa


def scaled_i64_to_decimal(mantissa: int, scale: int = 2) -> Decimal:
    """Convert a scaled int64 mantissa back to a Python decimal.Decimal."""
    return Decimal(int(mantissa)).scaleb(-scale)

try:
    from ._impulse_native import Snapshot as _NativeSnapshot
    from ._impulse_native import Writer as _NativeWriter
except ImportError:
    try:
        from _impulse_native import Snapshot as _NativeSnapshot
        from _impulse_native import Writer as _NativeWriter
    except ImportError:
        _NativeSnapshot = None
        _NativeWriter = None

from . import vm


class DomainView:
    """
    Domain Anchor Context representing a specific entity domain (e.g. User, Product).
    """

    def __init__(
        self,
        snapshot: "Snapshot",
        domain_id: int,
        domain_name: str,
        node_count: int = 0,
        key_type: int = 1,
        catalog: Optional[Union[str, Dict[str, int]]] = None,
    ):
        self._snapshot = snapshot
        self.domain_id = domain_id
        self.domain_name = domain_name
        self.node_count = node_count
        self.key_type = key_type
        self._catalog = catalog

    def from_node(self, node_id: int) -> "Traversal":
        from .traversal import Traversal

        return Traversal(
            self._snapshot, start_node=node_id, initial_domain=self, catalog=self._catalog
        )

    def from_nodes(self, node_ids: Union[List[int], np.ndarray]) -> "Traversal":
        from .traversal import Traversal

        return Traversal(
            self._snapshot, start_nodes=node_ids, initial_domain=self, catalog=self._catalog
        )

    def all(self) -> "Traversal":
        from .traversal import Traversal

        return Traversal(self._snapshot, all_nodes=True, initial_domain=self, catalog=self._catalog)

    def to_dense_id(self, key: str) -> int:
        return self._snapshot.resolve_dense_id(self.domain_id, key)

    def to_key(self, node_id: int) -> str:
        try:
            raw = self._snapshot.resolve_key(self.domain_id, node_id)
            return (
                raw.decode("utf-8", errors="replace")
                if isinstance(raw, (bytes, bytearray))
                else str(raw)
            )
        except Exception:
            return f"{self.domain_name}::{node_id}"

    def from_key(self, key: str) -> "Traversal":
        return self.from_node(self.to_dense_id(key))

    def from_keys(self, keys: List[str]) -> "Traversal":
        return self.from_nodes([self.to_dense_id(k) for k in keys])


class Snapshot:
    """
    Managed wrapper for an off-heap zero-copy memory-mapped Impulse Graph Binary Snapshot (.imps).
    """

    def __init__(self, path: str, catalog: Optional[Union[str, Dict[str, int]]] = None):
        if _NativeSnapshot is None:
            raise RuntimeError("_impulse_native extension is not compiled.")
        self._native = _NativeSnapshot(path)
        self._auto_catalog: Dict[str, int] = {}
        try:
            for idx in range(self._native.relation_count()):
                rel_info = self._native.get_relation(idx)
                rel_name = rel_info.get("name")
                if rel_name:
                    self._auto_catalog[rel_name] = idx
        except Exception:
            pass
        self._catalog = catalog if catalog is not None else (self._auto_catalog or None)
        self._default_ctx = None
        self._default_state = None

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()

    def _check_open(self):
        if self._native is None:
            raise RuntimeError("Snapshot is closed")

    def close(self):
        if self._default_ctx is not None:
            self._default_ctx.destroy()
            self._default_ctx = None
            self._default_state = None
        if self._native is not None:
            self._native.close()
            self._native = None

    def _get_default_vm(self):
        self._check_open()
        if self._default_ctx is None:
            self._default_ctx = vm.VmContext(self)
            self._default_state = vm.VmState()
        else:
            self._default_ctx.reset()
            self._default_state.reset()
        return self._default_ctx, self._default_state

    @property
    def default_context(self) -> vm.VmContext:
        self._check_open()
        if self._default_ctx is None:
            self._get_default_vm()
        return self._default_ctx

    @property
    def default_state(self) -> vm.VmState:
        self._check_open()
        if self._default_state is None:
            self._get_default_vm()
        return self._default_state

    def header(self) -> dict:
        self._check_open()
        return self._native.header()

    def file_size(self) -> int:
        self._check_open()
        return self._native.file_size()

    def sha256(self) -> str:
        self._check_open()
        return self._native.sha256()

    def domain_count(self) -> int:
        self._check_open()
        return self._native.domain_count()

    def get_domain(self, index: int) -> dict:
        self._check_open()
        return self._native.get_domain(index)

    def domains(self) -> List[dict]:
        self._check_open()
        return [self._native.get_domain(i) for i in range(self._native.domain_count())]

    def get_domain_attribute_count(self, domain_index: int) -> int:
        self._check_open()
        return self._native.get_domain_attribute_count(domain_index)

    def get_domain_attribute_info(self, domain_index: int, attribute_index: int) -> dict:
        self._check_open()
        return self._native.get_domain_attribute_info(domain_index, attribute_index)

    def index_count(self) -> int:
        self._check_open()
        return self._native.index_count()

    def get_index(self, index: int) -> dict:
        self._check_open()
        return self._native.get_index(index)

    def indices(self) -> List[dict]:
        self._check_open()
        return [self._native.get_index(i) for i in range(self._native.index_count())]

    def metadata(self) -> Dict[str, str]:
        self._check_open()
        return self._native.metadata()

    def get_metadata(self, key: str, default: Optional[str] = None) -> Optional[str]:
        self._check_open()
        val = self._native.get_metadata(key)
        return default if val is None else val

    def domain(
        self, name_or_id: Union[str, int], catalog: Optional[Union[str, Dict[str, int]]] = None
    ) -> DomainView:
        self._check_open()
        effective_catalog = catalog if catalog is not None else self._catalog
        if isinstance(name_or_id, int):
            count = self.domain_count()
            for i in range(count):
                dom_info = self.get_domain(i)
                if dom_info["domain_id"] == name_or_id:
                    return DomainView(
                        self,
                        domain_id=dom_info["domain_id"],
                        domain_name=dom_info["name"],
                        node_count=dom_info.get("node_count", 0),
                        key_type=dom_info["key_type"],
                        catalog=effective_catalog,
                    )
            raise KeyError(f"Domain ID {name_or_id} not found in snapshot catalog.")
        elif isinstance(name_or_id, str):
            count = self.domain_count()
            for i in range(count):
                dom_info = self.get_domain(i)
                if dom_info["name"] == name_or_id:
                    return DomainView(
                        self,
                        domain_id=dom_info["domain_id"],
                        domain_name=dom_info["name"],
                        node_count=dom_info.get("node_count", 0),
                        key_type=dom_info["key_type"],
                        catalog=effective_catalog,
                    )
            raise KeyError(f"Domain '{name_or_id}' not found in snapshot catalog.")
        raise TypeError(f"domain() argument must be str or int, got {type(name_or_id).__name__}")

    def relation_count(self) -> int:
        self._check_open()
        return self._native.relation_count()

    def get_relation(self, index: int) -> dict:
        self._check_open()
        return self._native.get_relation(index)

    def relations(self) -> List[dict]:
        self._check_open()
        return [self._native.get_relation(i) for i in range(self._native.relation_count())]

    def is_reachable(self, relation_index: int, src_id: int, tgt_id: int) -> bool:
        self._check_open()
        return self._native.is_reachable(relation_index, src_id, tgt_id)

    def resolve_key(self, domain_id: int, node_id: int) -> bytes:
        self._check_open()
        return self._native.resolve_key(domain_id, node_id)

    def resolve_dense_id(self, domain_id: int, key: str) -> int:
        self._check_open()
        return self._native.resolve_dense_id(domain_id, key)

    def resolve_keys_batch(
        self, domain_id: int, node_ids: Union[List[int], np.ndarray]
    ) -> List[str]:
        """Resolve a batch of dense node IDs back to string identifiers."""
        self._check_open()
        result = []
        for nid in node_ids:
            try:
                k_bytes = self.resolve_key(domain_id, int(nid))
                result.append(k_bytes.decode("utf-8", errors="replace"))
            except Exception:
                result.append(f"Node::{nid}")
        return result

    def get_row_offsets_array(self, relation_index: int = 0) -> np.ndarray:
        """Zero-copy memoryview into CSR row offsets uint32 array."""
        self._check_open()
        mv = self._native.get_csr_row_offsets(relation_index)
        return np.frombuffer(mv, dtype=np.uint32)

    def get_col_indices_array(self, relation_index: int = 0) -> np.ndarray:
        """Zero-copy memoryview into CSR column indices uint32 array."""
        self._check_open()
        mv = self._native.get_csr_col_indices(relation_index)
        return np.frombuffer(mv, dtype=np.uint32)

    def get_csc_row_offsets_array(self, relation_index: int = 0) -> np.ndarray:
        """Zero-copy memoryview into inverse CSC row offsets uint32 array."""
        self._check_open()
        mv = self._native.get_csc_row_offsets(relation_index)
        return np.frombuffer(mv, dtype=np.uint32)

    def get_csc_col_indices_array(self, relation_index: int = 0) -> np.ndarray:
        """Zero-copy memoryview into inverse CSC column indices uint32 array."""
        self._check_open()
        mv = self._native.get_csc_col_indices(relation_index)
        return np.frombuffer(mv, dtype=np.uint32)

    def get_attribute_array(
        self,
        relation_index: int = 0,
        attribute_index: int = 0,
        dtype=np.float32,
        shape: Optional[tuple] = None,
    ) -> np.ndarray:
        """
        Zero-copy memoryview into columnar Structure-of-Arrays (SoA) attribute section.
        Supports 1D scalar attributes or 2D (N x D) embedding matrices.
        """
        self._check_open()
        mv = self._native.get_attribute_data(relation_index, attribute_index)
        arr = np.frombuffer(mv, dtype=dtype)
        if shape is not None:
            return arr.reshape(shape)
        return arr

    def get_attribute_offsets_array(
        self, relation_index: int = 0, attribute_index: int = 0
    ) -> Optional[np.ndarray]:
        """Zero-copy memoryview into variable-length attribute offsets uint32 array."""
        self._check_open()
        mv = self._native.get_attribute_offsets(relation_index, attribute_index)
        if mv is None:
            return None
        return np.frombuffer(mv, dtype=np.uint32)

    def get_attribute_info(
        self, relation_index: int = 0, attribute_index: int = 0
    ) -> dict:
        """Get attribute type_code, dimension, and byte sizes."""
        self._check_open()
        return self._native.get_attribute_info(relation_index, attribute_index)

    def get_string_attribute(
        self, relation_index: int = 0, attribute_index: int = 0, index: int = 0
    ) -> str:
        """Retrieve a single string attribute by index using variable-length offsets."""
        self._check_open()
        offsets = self.get_attribute_offsets_array(relation_index, attribute_index)
        data = self._native.get_attribute_data(relation_index, attribute_index)
        data_bytes = bytes(data)
        if offsets is not None:
            if index + 1 >= len(offsets):
                raise IndexError(f"Attribute index {index} out of range (max {len(offsets) - 2})")
            start = int(offsets[index])
            end = int(offsets[index + 1])
            return data_bytes[start:end].decode("utf-8", errors="replace")
        return data_bytes.decode("utf-8", errors="replace")

    def get_string_attributes(
        self, relation_index: int = 0, attribute_index: int = 0
    ) -> List[str]:
        """Retrieve all strings for a variable-length string attribute."""
        self._check_open()
        offsets = self.get_attribute_offsets_array(relation_index, attribute_index)
        data = self._native.get_attribute_data(relation_index, attribute_index)
        data_bytes = bytes(data)
        if offsets is not None:
            res = []
            for i in range(len(offsets) - 1):
                start = int(offsets[i])
                end = int(offsets[i + 1])
                res.append(data_bytes[start:end].decode("utf-8", errors="replace"))
            return res
        return [data_bytes.decode("utf-8", errors="replace")]

    def sample_neighbors(
        self,
        relation_index: int,
        nodes: Union[List[int], np.ndarray],
        k_samples: int,
        seed: int = 42,
    ) -> tuple[np.ndarray, np.ndarray]:
        """High-speed SIMD C++ neighborhood sampler for GNN mini-batching."""
        self._check_open()
        if isinstance(nodes, np.ndarray):
            nodes_list = nodes.astype(np.uint64).tolist()
        else:
            nodes_list = [int(x) for x in nodes]
        return self._native.sample_neighbors(relation_index, nodes_list, k_samples, seed)

    def execute_query(self, query: vm.CompiledQuery, input_param: int = 0) -> vm.QueryResult:
        self._check_open()
        native_query = getattr(query, "_native", query)
        return vm.QueryResult(self._native.execute_query(native_query, input_param))

    def traverse(self, start_node: int = 0, catalog: Union[str, dict] | None = None) -> "Traversal":
        """Initiate a friendly fluent graph path traversal starting from start_node."""
        self._check_open()
        from .traversal import Traversal

        effective_catalog = catalog if catalog is not None else self._catalog
        return Traversal(self, start_node=start_node, catalog=effective_catalog)

    def cypher(
        self,
        query: str,
        params: dict | None = None,
        catalog: Union[str, dict] | None = None,
    ) -> Union[List[int], int]:
        """Execute a declarative openCypher query directly against the snapshot off-heap."""
        self._check_open()
        from .cypher import CypherQuery

        effective_catalog = catalog if catalog is not None else self._catalog
        c_query = CypherQuery(query, catalog=effective_catalog)
        return c_query.execute(self, params=params)

    def compile_cypher(
        self,
        query: str,
        catalog: Union[str, dict] | None = None,
    ) -> "Traversal":
        """Compile a declarative openCypher query into a reusable Traversal / VM executable."""
        self._check_open()
        from .cypher import CypherQuery

        effective_catalog = catalog if catalog is not None else self._catalog
        c_query = CypherQuery(query, catalog=effective_catalog)
        return c_query.build_traversal(self)

    def _relation_shape(self, relation_index: int, transpose: bool = False) -> tuple[int, int]:
        rel = self.get_relation(relation_index)
        src_count = rel["node_count"]
        tgt_count = rel["node_count"]
        try:
            src_dom = self.domain(int(rel["src_domain_id"]))
            if src_dom.node_count > 0:
                src_count = src_dom.node_count
        except Exception:
            pass
        try:
            tgt_dom = self.domain(int(rel["tgt_domain_id"]))
            if tgt_dom.node_count > 0:
                tgt_count = tgt_dom.node_count
        except Exception:
            pass
        return (tgt_count, src_count) if transpose else (src_count, tgt_count)

    def to_scipy_csr(self, relation_index: int = 0, transpose: bool = False):
        """
        Convert snapshot topology to a scipy.sparse.csr_matrix or csc_matrix.
        Uses zero-copy off-heap pointers.
        """
        import scipy.sparse as sp

        if transpose:
            try:
                indptr = self.get_csc_row_offsets_array(relation_index)
                indices = self.get_csc_col_indices_array(relation_index)
            except Exception:
                # Transpose forward CSR if CSC is not compiled in file
                forward = self.to_scipy_csr(relation_index, transpose=False)
                return forward.transpose().tocsr()
            _, cols = self._relation_shape(relation_index, transpose=True)
            rows = len(indptr) - 1 if len(indptr) > 0 else 0
            if len(indices) > 0:
                cols = max(cols, int(indices.max()) + 1)
            data = np.ones(len(indices), dtype=np.float32)
            return sp.csr_matrix((data, indices, indptr), shape=(rows, cols))

        indptr = self.get_row_offsets_array(relation_index)
        indices = self.get_col_indices_array(relation_index)
        _, cols = self._relation_shape(relation_index, transpose=False)
        rows = len(indptr) - 1 if len(indptr) > 0 else 0
        if len(indices) > 0:
            cols = max(cols, int(indices.max()) + 1)
        data = np.ones(len(indices), dtype=np.float32)
        return sp.csr_matrix((data, indices, indptr), shape=(rows, cols))

    def to_torch_csr(self, relation_index: int = 0, transpose: bool = False, device: str = "cpu"):
        """
        Convert snapshot CSR topology to a torch.sparse_csr_tensor.
        Safe for zero-copy read-only inference.
        """
        import torch

        if transpose:
            try:
                crow_np = self.get_csc_row_offsets_array(relation_index)
                col_np = self.get_csc_col_indices_array(relation_index)
            except Exception:
                fwd = self.to_torch_csr(relation_index, transpose=False, device="cpu")
                return fwd.transpose(0, 1).to_sparse_csr().to(device)
        else:
            crow_np = self.get_row_offsets_array(relation_index)
            col_np = self.get_col_indices_array(relation_index)

        _, cols = self._relation_shape(relation_index, transpose=transpose)
        rows = len(crow_np) - 1 if len(crow_np) > 0 else 0
        if len(col_np) > 0:
            cols = max(cols, int(col_np.max()) + 1)
        size = (rows, cols)

        # Use np.copy or int64 casting to satisfy PyTorch sparse_csr 64-bit requirement
        crow_indices = torch.from_numpy(crow_np.astype(np.int64))
        col_indices = torch.from_numpy(col_np.astype(np.int64))
        values = torch.ones(col_indices.shape[0], dtype=torch.float32)

        tensor = torch.sparse_csr_tensor(crow_indices, col_indices, values, size=size)
        return tensor.to(device) if device != "cpu" else tensor

    def to_torch_edge_index(
        self, relation_index: int = 0, transpose: bool = False, device: str = "cpu"
    ):
        """
        Convert snapshot CSR topology to PyTorch Geometric COO edge_index tensor of shape (2, num_edges).
        """
        import torch

        if transpose:
            try:
                indptr = self.get_csc_row_offsets_array(relation_index)
                indices = self.get_csc_col_indices_array(relation_index)
            except Exception:
                fwd = self.to_torch_edge_index(relation_index, transpose=False, device="cpu")
                return torch.stack([fwd[1], fwd[0]]).to(device)
        else:
            indptr = self.get_row_offsets_array(relation_index)
            indices = self.get_col_indices_array(relation_index)

        degrees = np.diff(indptr)
        src = np.repeat(np.arange(len(degrees), dtype=np.int64), degrees)
        dst = indices.astype(np.int64)
        edge_index = torch.from_numpy(np.vstack([src, dst]))
        return edge_index.to(device) if device != "cpu" else edge_index

    def to_networkx(self, relation_index: int = 0, create_using=None):
        """Convert snapshot CSR topology to a NetworkX Graph or DiGraph."""
        import networkx as nx

        if create_using is None:
            create_using = nx.DiGraph
        indptr = self.get_row_offsets_array(relation_index)
        indices = self.get_col_indices_array(relation_index)
        G = create_using()
        for u in range(len(indptr) - 1):
            for v in indices[indptr[u] : indptr[u + 1]]:
                G.add_edge(u, int(v))
        return G

    def to_polars(self, relation_index: int = 0):
        """Convert snapshot CSR topology into a Polars DataFrame with columns ['src', 'dst']."""
        import polars as pl

        indptr = self.get_row_offsets_array(relation_index)
        indices = self.get_col_indices_array(relation_index)
        degrees = np.diff(indptr)
        src = np.repeat(np.arange(len(degrees), dtype=np.uint32), degrees)
        return pl.DataFrame({"src": src, "dst": indices})

    def to_pandas(self, relation_index: int = 0):
        """Convert snapshot CSR topology into a Pandas DataFrame with columns ['src', 'dst']."""
        import pandas as pd

        indptr = self.get_row_offsets_array(relation_index)
        indices = self.get_col_indices_array(relation_index)
        degrees = np.diff(indptr)
        src = np.repeat(np.arange(len(degrees), dtype=np.uint32), degrees)
        return pd.DataFrame({"src": src, "dst": indices})

    def to_arrow_table(self, relation_index: int = 0):
        """Convert snapshot CSR topology into an Apache Arrow Table."""
        import pyarrow as pa

        indptr = self.get_row_offsets_array(relation_index)
        indices = self.get_col_indices_array(relation_index)
        degrees = np.diff(indptr)
        src = np.repeat(np.arange(len(degrees), dtype=np.uint32), degrees)
        return pa.Table.from_arrays([pa.array(src), pa.array(indices)], names=["src", "dst"])


class Writer:
    def __init__(self, output_path: str, global_features: int = 1):
        if _NativeWriter is None:
            raise RuntimeError("_impulse_native extension is not compiled.")
        self._native = _NativeWriter(output_path, global_features)

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.destroy()

    def destroy(self):
        if self._native is not None:
            self._native.destroy()

    def add_domain(self, domain_id: int, key_type: int, name: str):
        self._native.add_domain(domain_id, key_type, name)

    def set_domain_node_count(self, domain_index: int, node_count: int):
        self._native.set_domain_node_count(domain_index, node_count)

    def add_domain_attribute_decimal64(
        self,
        domain_index: int,
        name: str,
        precision: int,
        scale: int,
        data: Union[bytes, bytearray, np.ndarray, List[int]],
        dimension: int = 1,
        type_code: int = ATTR_TYPE_DECIMAL64,
    ):
        if isinstance(data, np.ndarray):
            data_bytes = np.ascontiguousarray(data, dtype=np.int64).tobytes()
        elif isinstance(data, (bytes, bytearray)):
            data_bytes = data
        else:
            data_bytes = np.asarray(data, dtype=np.int64).tobytes()
        self._native.add_domain_attribute_decimal64(
            domain_index, name, type_code, dimension, precision, scale, data_bytes
        )

    def add_relation(
        self,
        src_domain_id: int,
        tgt_domain_id: int,
        encoding_type: int,
        node_count: int,
        edge_count: int,
        section_features: int,
        row_offsets: Union[List[int], np.ndarray],
        col_indices: Union[List[int], np.ndarray],
    ):
        if isinstance(row_offsets, np.ndarray):
            row_offsets = row_offsets.astype(np.uint32).tolist()
        if isinstance(col_indices, np.ndarray):
            col_indices = col_indices.astype(np.uint32).tolist()

        self._native.add_relation(
            src_domain_id,
            tgt_domain_id,
            encoding_type,
            node_count,
            edge_count,
            section_features,
            list(row_offsets),
            list(col_indices),
        )

    def add_attribute(
        self,
        relation_index: int,
        name: str,
        type_code: int,
        dimension: int,
        data: Union[bytes, bytearray, np.ndarray],
        offsets: Optional[Union[bytes, bytearray, np.ndarray]] = None,
    ):
        data_bytes = data.tobytes() if isinstance(data, np.ndarray) else data
        offsets_bytes = offsets.tobytes() if isinstance(offsets, np.ndarray) else offsets
        self._native.add_attribute(relation_index, name, type_code, dimension, data_bytes, offsets_bytes)

    def add_attribute_decimal64(
        self,
        relation_index: int,
        name: str,
        precision: int,
        scale: int,
        data: Union[bytes, bytearray, np.ndarray, List[int]],
        dimension: int = 1,
        type_code: int = ATTR_TYPE_DECIMAL64,
    ):
        if isinstance(data, np.ndarray):
            data_bytes = np.ascontiguousarray(data, dtype=np.int64).tobytes()
        elif isinstance(data, (bytes, bytearray)):
            data_bytes = data
        else:
            data_bytes = np.asarray(data, dtype=np.int64).tobytes()
        self._native.add_attribute_decimal64(
            relation_index, name, type_code, dimension, precision, scale, data_bytes
        )

    def set_metadata(self, key: str, value: str):
        self._native.set_metadata(key, value)

    def add_index(
        self,
        domain_id: int,
        relation_id: int,
        attribute_index: int,
        index_type: int,
        index_name: str,
        index_data: bytes,
        payload_feature_mask: int = 0,
    ):
        self._native.add_index(
            domain_id,
            relation_id,
            attribute_index,
            index_type,
            index_name,
            index_data,
            payload_feature_mask,
        )

    def finalize(self):
        self._native.finalize()

    @classmethod
    def from_scipy(cls, output_path: str, matrix, domain_name: str = "Node", key_type: int = 4):
        """
        Convenience constructor: Compile a scipy.sparse.csr_matrix or csc_matrix into an .imps file.
        """
        import scipy.sparse as sp

        if not sp.isspmatrix_csr(matrix):
            matrix = matrix.tocsr()

        num_nodes = matrix.shape[0]
        num_edges = matrix.nnz

        with cls(output_path) as writer:
            writer.add_domain(0, key_type, domain_name)
            writer.add_relation(
                src_domain_id=0,
                tgt_domain_id=0,
                encoding_type=0,
                node_count=num_nodes,
                edge_count=num_edges,
                section_features=0,
                row_offsets=matrix.indptr,
                col_indices=matrix.indices,
            )
            writer.finalize()

    @classmethod
    def from_torch(
        cls,
        output_path: str,
        edge_index,
        num_nodes: Optional[int] = None,
        domain_name: str = "Node",
        key_type: int = 4,
    ):
        """
        Convenience constructor: Compile a PyTorch edge_index tensor (2, E) or torch.sparse_csr_tensor into an .imps file.
        """
        import torch

        if hasattr(edge_index, "is_sparse_csr") and edge_index.is_sparse_csr:
            crow = edge_index.crow_indices().cpu().numpy().astype(np.uint32)
            col = edge_index.col_indices().cpu().numpy().astype(np.uint32)
            n_nodes = num_nodes or edge_index.size(0)
            n_edges = col.shape[0]
        else:
            if isinstance(edge_index, torch.Tensor):
                ei = edge_index.cpu().numpy()
            else:
                ei = np.asarray(edge_index)

            src = ei[0].astype(np.int64)
            dst = ei[1].astype(np.int64)
            n_nodes = num_nodes or int(max(src.max(), dst.max()) + 1)
            n_edges = len(src)

            # Sort edges by source node
            sort_idx = np.argsort(src)
            src_sorted = src[sort_idx]
            dst_sorted = dst[sort_idx]

            # Compute CSR offsets
            counts = np.bincount(src_sorted, minlength=n_nodes)
            crow = np.zeros(n_nodes + 1, dtype=np.uint32)
            crow[1:] = np.cumsum(counts, dtype=np.uint32)
            col = dst_sorted.astype(np.uint32)

        with cls(output_path) as writer:
            writer.add_domain(0, key_type, domain_name)
            writer.add_relation(
                src_domain_id=0,
                tgt_domain_id=0,
                encoding_type=0,
                node_count=n_nodes,
                edge_count=n_edges,
                section_features=0,
                row_offsets=crow,
                col_indices=col,
            )
            writer.finalize()

    @classmethod
    def from_dataframe(
        cls,
        output_path: str,
        df,
        src_col: str = "src",
        tgt_col: str = "dst",
        domain_name: str = "Node",
        key_type: int = 1,  # STRING key
    ):
        """
        Convenience constructor: Compile a Pandas/Polars DataFrame into an .imps file with automatic key mapping.
        """
        if hasattr(df, "to_pandas"):
            pdf = df.to_pandas()
        else:
            pdf = df

        src_raw = pdf[src_col].values
        tgt_raw = pdf[tgt_col].values

        # Unique key mapping
        unique_keys, inverse = np.unique(np.concatenate([src_raw, tgt_raw]), return_inverse=True)
        n_nodes = len(unique_keys)
        n_edges = len(src_raw)

        src_ids = inverse[:n_edges]
        dst_ids = inverse[n_edges:]

        sort_idx = np.argsort(src_ids)
        src_sorted = src_ids[sort_idx]
        dst_sorted = dst_ids[sort_idx]

        counts = np.bincount(src_sorted, minlength=n_nodes)
        row_offsets = np.zeros(n_nodes + 1, dtype=np.uint32)
        row_offsets[1:] = np.cumsum(counts, dtype=np.uint32)
        col_indices = dst_sorted.astype(np.uint32)

        with cls(output_path) as writer:
            writer.add_domain(0, key_type, domain_name)
            writer.add_relation(
                src_domain_id=0,
                tgt_domain_id=0,
                encoding_type=0,
                node_count=n_nodes,
                edge_count=n_edges,
                section_features=0,
                row_offsets=row_offsets,
                col_indices=col_indices,
            )
            writer.finalize()

    @classmethod
    def from_networkx(cls, output_path: str, graph, domain_name: str = "Node"):
        """
        Convenience constructor: Compile a NetworkX Graph / DiGraph into an .imps file.
        """
        nodes = sorted(list(graph.nodes()))
        node_map = {n: i for i, n in enumerate(nodes)}
        n_nodes = len(nodes)

        row_offsets = [0]
        col_indices = []
        for n in nodes:
            nbrs = [node_map[nbr] for nbrbr in [graph.neighbors(n)] for nbr in sorted(nbrbr)]
            col_indices.extend(nbrs)
            row_offsets.append(len(col_indices))

        with cls(output_path) as writer:
            writer.add_domain(0, 4, domain_name)
            writer.add_relation(
                src_domain_id=0,
                tgt_domain_id=0,
                encoding_type=0,
                node_count=n_nodes,
                edge_count=len(col_indices),
                section_features=0,
                row_offsets=np.array(row_offsets, dtype=np.uint32),
                col_indices=np.array(col_indices, dtype=np.uint32),
            )
            writer.finalize()


from .traversal import Traversal  # noqa: E402


class CompiledV11Program:
    """Compiled ImpulseVM v0.11 program with typed parameter and DECIMAL64 output schema."""

    def __init__(self, compiled_dict: dict):
        self.impb_bytes: bytes = compiled_dict["impb_bytes"]
        self.param_name_to_id: Dict[str, int] = dict(compiled_dict["param_name_to_id"])
        self.param_schema: List[dict] = list(compiled_dict["param_schema"])
        self.output_names: List[str] = list(compiled_dict["output_names"])
        self.output_schema: List[dict] = list(compiled_dict["output_schema"])
        self.impscm_source: Optional[str] = compiled_dict.get("impscm_source")

    def execute(
        self,
        snapshot: Optional["Snapshot"] = None,
        params: Optional[Dict[str, object]] = None,
        scratch_bytes: int = 4 * 1024 * 1024,
    ) -> Dict[str, object]:
        """Execute this compiled v0.11 program with automatic Decimal64 param/output conversion."""
        from . import _impulse_native

        params = params or {}
        native_snap = getattr(snapshot, "_native", snapshot) if snapshot is not None else None
        bindings = []
        for pm in self.param_schema:
            name = pm["name"]
            bare = name[1:] if name.startswith("$") else name
            if name in params:
                val = params[name]
            elif bare in params:
                val = params[bare]
            else:
                raise KeyError(f"Missing required query parameter '{name}'")

            pid = pm["param_id"]
            if val is None:
                bindings.append({"param_id": pid, "kind": 0})
            elif pm["is_decimal"]:
                if isinstance(val, Decimal64):
                    scaled = decimal_to_scaled_i64(
                        val.to_decimal(), scale=pm["scale"], precision=pm["precision"]
                    )
                else:
                    scaled = decimal_to_scaled_i64(
                        val, scale=pm["scale"], precision=pm["precision"]
                    )
                bindings.append({"param_id": pid, "kind": 1, "scalar_i64": int(scaled)})
            elif pm["type"] in ("ID", "U64"):
                bindings.append({"param_id": pid, "kind": 2, "scalar_bits": int(val)})
            elif pm["type"] == "I64":
                bindings.append({"param_id": pid, "kind": 1, "scalar_i64": int(val)})
            elif pm["type"] == "F64":
                bindings.append({"param_id": pid, "kind": 3, "scalar_f64": float(val)})
            elif pm["type"] == "BOOL":
                bindings.append({"param_id": pid, "kind": 4, "scalar_bits": 1 if val else 0})
            elif pm["type"] == "STR":
                bindings.append({"param_id": pid, "kind": 5, "str_val": str(val)})
            elif pm["type"] == "FRONTIER":
                bindings.append(
                    {
                        "param_id": pid,
                        "kind": 6,
                        "domain_id": pm["domain_id"],
                        "ids": [int(x) for x in val],
                    }
                )
            else:
                bindings.append({"param_id": pid, "kind": 1, "scalar_i64": int(val)})

        with _impulse_native.VmWorkspace(scratch_bytes) as ws:
            rc = ws.execute_params(self.impb_bytes, native_snap, bindings)
            if rc != int(_impulse_native.VmStatus.OK):
                raise RuntimeError(f"ImpulseVM v0.11 execution failed with status={rc}")
            outputs: Dict[str, object] = {}
            for slot, meta in enumerate(self.output_schema):
                raw_out = ws.get_output(slot)
                data = raw_out.get("data", [])
                is_scalar = meta.get("collect_kind") == "scalar"
                if meta["is_decimal"]:
                    dec_vals = [
                        None if v is None else scaled_i64_to_decimal(v, scale=meta["scale"])
                        for v in data
                    ]
                    outputs[meta["name"]] = (
                        (dec_vals[0] if dec_vals else None) if is_scalar else dec_vals
                    )
                elif meta.get("type") == "BOOL":
                    bool_vals = [None if v is None else bool(v) for v in data]
                    outputs[meta["name"]] = (
                        (bool_vals[0] if bool_vals else None) if is_scalar else bool_vals
                    )
                else:
                    outputs[meta["name"]] = (data[0] if data else None) if is_scalar else data
            return outputs


def compile_impscm(
    source: str,
    snapshot: Optional["Snapshot"] = None,
    enable_optimizations: bool = True,
) -> CompiledV11Program:
    """Compile an ImpScheme v0.11 (query ...) program against a Snapshot."""
    from . import _impulse_native

    native_snap = getattr(snapshot, "_native", snapshot) if snapshot is not None else None
    return CompiledV11Program(
        _impulse_native.compile_impscm(source, native_snap, enable_optimizations)
    )


def compile_cypher(
    cypher: str,
    snapshot: Optional["Snapshot"] = None,
    param_types: Optional[Dict[str, object]] = None,
    enable_optimizations: bool = True,
) -> CompiledV11Program:
    """Compile an openCypher query (with DECIMAL64 literals, CAST, and typed $params) to ImpulseVM v0.11."""
    from . import _impulse_native

    native_snap = getattr(snapshot, "_native", snapshot) if snapshot is not None else None
    return CompiledV11Program(
        _impulse_native.compile_cypher(
            cypher, native_snap, param_types or {}, enable_optimizations
        )
    )


try:
    from importlib.metadata import version as _pkg_version

    __version__ = _pkg_version("impulse-graph")
except Exception:
    __version__ = "0.10.1"

__all__ = [
    "ATTR_TYPE_BOOL",
    "ATTR_TYPE_DECIMAL64",
    "IMPULSE_VM_ERR_ARITHMETIC_OVERFLOW",
    "IMPULSE_VM_ERR_DIV_BY_ZERO",
    "OP_SCALAR_DECIMAL_ALU",
    "OP_VEC_DECIMAL_ALU",
    "Decimal64",
    "decimal_to_scaled_i64",
    "scaled_i64_to_decimal",
    "CompiledV11Program",
    "compile_impscm",
    "compile_cypher",
    "Snapshot",
    "DomainView",
    "Writer",
    "Traversal",
    "vm",
]
