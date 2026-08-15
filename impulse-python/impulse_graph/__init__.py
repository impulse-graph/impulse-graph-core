"""
Impulse Graph Engine Python SDK & C++ Bytecode VM Binding
"""

import numpy as np

try:
    from ._impulse_native import Snapshot as _NativeSnapshot, Writer as _NativeWriter
except ImportError:
    try:
        from _impulse_native import Snapshot as _NativeSnapshot, Writer as _NativeWriter
    except ImportError as e:
        _NativeSnapshot = None
        _NativeWriter = None

from . import vm


class Snapshot:
    def __init__(self, path: str):
        if _NativeSnapshot is None:
            raise RuntimeError("_impulse_native extension is not compiled.")
        self._native = _NativeSnapshot(path)

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()

    def close(self):
        if self._native is not None:
            self._native.close()

    def domain_count(self) -> int:
        return self._native.domain_count()

    def get_domain(self, index: int) -> dict:
        return self._native.get_domain(index)

    def relation_count(self) -> int:
        return self._native.relation_count()

    def get_relation(self, index: int) -> dict:
        return self._native.get_relation(index)

    def is_reachable(self, relation_index: int, src_id: int, tgt_id: int) -> bool:
        return self._native.is_reachable(relation_index, src_id, tgt_id)

    def get_row_offsets_array(self, relation_index: int) -> np.ndarray:
        mv = self._native.get_csr_row_offsets(relation_index)
        return np.frombuffer(mv, dtype=np.uint32)

    def get_col_indices_array(self, relation_index: int) -> np.ndarray:
        mv = self._native.get_csr_col_indices(relation_index)
        return np.frombuffer(mv, dtype=np.uint32)

    def sample_neighbors(self, relation_index: int, nodes: list | np.ndarray, k_samples: int, seed: int = 42) -> tuple[np.ndarray, np.ndarray]:
        if isinstance(nodes, np.ndarray):
            nodes_list = nodes.astype(np.uint64).tolist()
        else:
            nodes_list = [int(x) for x in nodes]
        return self._native.sample_neighbors(relation_index, nodes_list, k_samples, seed)

    def execute_query(self, query: vm.CompiledQuery, input_param: int = 0) -> vm.QueryResult:
        native_query = getattr(query, "_native", query)
        return vm.QueryResult(self._native.execute_query(native_query, input_param))

    def traverse(self, start_node: int = 0, catalog: Union[str, dict] | None = None) -> "Traversal":
        """Initiate a friendly fluent graph path traversal starting from start_node."""
        from .traversal import Traversal
        return Traversal(self, start_node=start_node, catalog=catalog)

    def cypher(
        self,
        query: str,
        params: dict | None = None,
        catalog: Union[str, dict] | None = None,
    ) -> Union[List[int], int]:
        """Execute a declarative openCypher query directly against the snapshot off-heap."""
        from .cypher import CypherQuery
        c_query = CypherQuery(query, catalog=catalog)
        return c_query.execute(self, params=params)

    def compile_cypher(
        self,
        query: str,
        catalog: Union[str, dict] | None = None,
    ) -> "Traversal":
        """Compile a declarative openCypher query into a reusable Traversal / VM executable."""
        from .cypher import CypherQuery
        c_query = CypherQuery(query, catalog=catalog)
        return c_query.build_traversal(self)

    def to_scipy_csr(self, relation_index: int = 0):
        """Convert snapshot CSR topology to a zero-copy scipy.sparse.csr_matrix."""
        import scipy.sparse as sp
        rel = self.get_relation(relation_index)
        indptr = self.get_row_offsets_array(relation_index)
        indices = self.get_col_indices_array(relation_index)
        data = np.ones(len(indices), dtype=np.float32)
        shape = (rel["node_count"], rel["node_count"])
        return sp.csr_matrix((data, indices, indptr), shape=shape)

    def to_torch_csr(self, relation_index: int = 0, device: str = "cpu"):
        """Convert snapshot CSR topology to a zero-copy torch.sparse_csr_tensor."""
        import torch
        rel = self.get_relation(relation_index)
        crow_indices = torch.from_numpy(self.get_row_offsets_array(relation_index)).to(torch.int64)
        col_indices = torch.from_numpy(self.get_col_indices_array(relation_index)).to(torch.int64)
        values = torch.ones(col_indices.shape[0], dtype=torch.float32)
        size = (rel["node_count"], rel["node_count"])
        tensor = torch.sparse_csr_tensor(crow_indices, col_indices, values, size=size)
        return tensor.to(device) if device != "cpu" else tensor

    def to_torch_edge_index(self, relation_index: int = 0, device: str = "cpu"):
        """Convert snapshot CSR topology to PyTorch Geometric COO edge_index tensor (2, num_edges)."""
        import torch
        indptr = self.get_row_offsets_array(relation_index)
        indices = self.get_col_indices_array(relation_index)
        degrees = np.diff(indptr)
        src = np.repeat(np.arange(len(degrees), dtype=np.int64), degrees)
        dst = indices.astype(np.int64)
        edge_index = torch.tensor(np.vstack([src, dst]), dtype=torch.int64)
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
            for v in indices[indptr[u]:indptr[u+1]]:
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
        row_offsets: list,
        col_indices: list,
    ):
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


from .traversal import Traversal

__version__ = "2.4.0"
__all__ = ["Snapshot", "Writer", "Traversal", "vm"]
