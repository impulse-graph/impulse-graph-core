"""
Impulse Graph Engine Python SDK & Zero-Copy C-ABI Binding
"""

import numpy as np

try:
    from _impulse_native import Snapshot as _NativeSnapshot, Writer as _NativeWriter
except ImportError:
    _NativeSnapshot = None
    _NativeWriter = None


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

    def relation_count(self) -> int:
        return self._native.relation_count()

    def get_relation(self, index: int) -> dict:
        return self._native.get_relation(index)

    def is_reachable(self, src_domain: int, src_id: int, tgt_domain: int, tgt_id: int) -> bool:
        return self._native.is_reachable(src_domain, src_id, tgt_domain, tgt_id)

    def get_row_offsets_array(self, relation_index: int) -> np.ndarray:
        mv = self._native.get_csr_row_offsets(relation_index)
        return np.frombuffer(mv, dtype=np.uint32)

    def get_col_indices_array(self, relation_index: int) -> np.ndarray:
        mv = self._native.get_csr_col_indices(relation_index)
        return np.frombuffer(mv, dtype=np.uint32)

    def sample_neighbors(self, relation_index: int, nodes: list | np.ndarray, k_samples: int, seed: int = 42) -> tuple[np.ndarray, np.ndarray]:
        if isinstance(nodes, np.ndarray):
            nodes_list = nodes.astype(np.uint32).tolist()
        else:
            nodes_list = [int(x) for x in nodes]
        return self._native.sample_neighbors(relation_index, nodes_list, k_samples, seed)


class Writer:
    def __init__(self, output_path: str, global_features: int = 8):
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


__version__ = "2.4.0"
__all__ = ["Snapshot", "Writer"]
