"""
Impulse Graph Engine Python SDK & C++ Bytecode VM Binding
Spec v0.9.0
"""

from typing import Dict, List, Optional, Union

import numpy as np

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
        self._catalog = catalog

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()

    def _check_open(self):
        if self._native is None:
            raise RuntimeError("Snapshot is closed")

    def close(self):
        if self._native is not None:
            self._native.close()
            self._native = None

    def domain_count(self) -> int:
        self._check_open()
        return self._native.domain_count()

    def get_domain(self, index: int) -> dict:
        self._check_open()
        return self._native.get_domain(index)

    def domain(
        self, name_or_id: Union[str, int], catalog: Optional[Union[str, Dict[str, int]]] = None
    ) -> DomainView:
        self._check_open()
        effective_catalog = catalog if catalog is not None else self._catalog
        if isinstance(name_or_id, int):
            dom_info = self.get_domain(name_or_id)
            return DomainView(
                self,
                domain_id=dom_info["domain_id"],
                domain_name=dom_info["name"],
                key_type=dom_info["key_type"],
                catalog=effective_catalog,
            )
        elif isinstance(name_or_id, str):
            count = self.domain_count()
            for i in range(count):
                dom_info = self.get_domain(i)
                if dom_info["name"] == name_or_id:
                    return DomainView(
                        self,
                        domain_id=dom_info["domain_id"],
                        domain_name=dom_info["name"],
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

    def to_scipy_csr(self, relation_index: int = 0, transpose: bool = False):
        """
        Convert snapshot topology to a scipy.sparse.csr_matrix or csc_matrix.
        Uses zero-copy off-heap pointers.
        """
        import scipy.sparse as sp

        rel = self.get_relation(relation_index)
        node_count = rel["node_count"]
        shape = (node_count, node_count)

        if transpose:
            try:
                indptr = self.get_csc_row_offsets_array(relation_index)
                indices = self.get_csc_col_indices_array(relation_index)
            except Exception:
                # Transpose forward CSR if CSC is not compiled in file
                forward = self.to_scipy_csr(relation_index, transpose=False)
                return forward.transpose().tocsr()
            data = np.ones(len(indices), dtype=np.float32)
            return sp.csr_matrix((data, indices, indptr), shape=shape)

        indptr = self.get_row_offsets_array(relation_index)
        indices = self.get_col_indices_array(relation_index)
        data = np.ones(len(indices), dtype=np.float32)
        return sp.csr_matrix((data, indices, indptr), shape=shape)

    def to_torch_csr(self, relation_index: int = 0, transpose: bool = False, device: str = "cpu"):
        """
        Convert snapshot CSR topology to a torch.sparse_csr_tensor.
        Safe for zero-copy read-only inference.
        """
        import torch

        rel = self.get_relation(relation_index)
        node_count = rel["node_count"]
        size = (node_count, node_count)

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

__version__ = "0.9.0"
__all__ = ["Snapshot", "DomainView", "Writer", "Traversal", "vm"]
