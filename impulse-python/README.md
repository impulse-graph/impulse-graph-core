# Impulse Graph Engine Python SDK & C++ Bytecode VM Binding (`impulse-python`)

The official Python SDK for **Impulse Graph Engine** (`impulse-graph`), pairing zero-copy memory-mapped graph analytics with native C++20 `ImpulseVM` bytecode execution, off-heap data interchange, and high-performance ML/Data Science interop (PyTorch, PyG, SciPy, NumPy, Apache Arrow, Polars, NetworkX).

---

## 1. Features & Architectural Highlights

* **Zero-Copy Memory-Mapped Snapshots (`.imps`)**: Query multi-terabyte binary graph snapshots directly off-heap without heap copies or garbage collection overhead.
* **ML & Data Science Data Interchange**: First-class zero-copy interop with **PyTorch & PyG** (`torch.sparse_csr_tensor`, `torch_geometric.data.Data`, GNN neighbor sampling), **SciPy** (`csr_matrix` / `csgraph`), **Apache Arrow**, **Polars / Pandas**, and **NetworkX**.
* **Bytecode Virtual Machine (`QueryBuilder`)**: Fluent C++20 bytecode compiler generating vector instructions (`OP_CSR_WALK`, `OP_CSC_WALK`, `OP_COLLECT_BITSET`, `OP_MXV`).
* **NumPy Zero-Copy Interop**: Direct memoryviews into off-heap columnar attribute sections and node index buffers.
* **Declarative Cypher & Traversal DSL**: High-level graph pattern matching and reachability queries.
* **Convenience Snapshot Constructors**: Create `.imps` snapshots directly from PyTorch tensors (`Writer.from_torch`), SciPy sparse matrices (`Writer.from_scipy`), DataFrames (`Writer.from_dataframe`), or NetworkX graphs (`Writer.from_networkx`).

👉 **[Read the Snapshot Interchange Guide (`SNAPSHOT_INTERCHANGE_GUIDE.md`)](SNAPSHOT_INTERCHANGE_GUIDE.md)** for in-depth data interchange recipes and framework comparisons.

---

## 2. Prerequisites & Installation

### Prerequisites

| Dependency | Minimum Version | Notes |
| :--- | :--- | :--- |
| **Python** | 3.8+ | Python 3.8 through 3.14 supported. |
| **C++ Compiler** | GCC 11+, Clang 14+, MSVC 2022+ | Full C++20 support required to compile native C-ABI extension. |
| **CMake** | 3.20+ | Used to configure Highway SIMD dependencies during extension build. |
| **pip & setuptools** | Recent | `pip install pybind11 numpy pytest`. |

### Install from Source (Development Mode)

```bash
cd impulse-graph-core/impulse-python

# Install in editable/development mode
pip install -e .
```

Or build the native C++ extension module in-place:
```bash
python setup.py build_ext --inplace
```

---

## 3. Running the Self-Contained Test Suite

All unit tests are **100% self-contained** in the `tests/` directory and execute offline without external dataset downloads:

```bash
# Run test suite with pytest
pytest tests/
```

Test coverage includes:
* `tests/test_writer.py`: Programmatic `.imps` creation and header verification.
* `tests/test_snapshot.py`: Off-heap zero-copy memoryview and domain/relation metadata inspection.
* `tests/test_reachability.py`: Point-to-point graph reachability.
* `tests/test_vm.py`: Bytecode compiler, register windowing, and bitset collections.
* `tests/test_cypher.py`: Cypher pattern compilation and execution.

---

## 4. Running Examples & Sample Datasets

The `examples/` directory contains end-to-end sample scripts demonstrating real-world usage.

### Standard Sample Datasets Setup

Examples that load pre-built snapshots (`02_social_traversal.py`, `03_rbac_reachability.py`, `04_cel_transactions.py`) look for canonical `.imps` files in the directory specified by the **`IMPULSE_DATASETS_DIR`** environment variable:

1. **Download & Extract Sample Datasets**:
   ```bash
   curl -fsSLO https://github.com/impulse-graph/impulse-graph-core/releases/download/v0.9.0/impulse-sample-datasets.tar.gz
   tar -xzf impulse-sample-datasets.tar.gz -C ~/impulse-datasets/
   ```

2. **Export Environment Variable**:
   ```bash
   export IMPULSE_DATASETS_DIR=~/impulse-datasets
   ```

*(Note: If `IMPULSE_DATASETS_DIR` is unset, examples will automatically search local fallback paths such as `../../datasets` or generate local sample graphs).*

### Running Example Scripts

```bash
# 1. Programmatically build snapshot with Writer and query with Snapshot
python examples/01_snapshot_basics.py

# 2. Multi-hop traversal on social network graph (social_graph.imps)
python examples/02_social_traversal.py

# 3. Relationship-Based Access Control (ReBAC) on rbac_snapshot.imps
python examples/03_rbac_reachability.py

# 4. Google CEL expression filtering and zero-copy NumPy array extraction
python examples/04_cel_transactions.py

# 5. PyTorch sparse CSR, PyG Data conversion & GNN neighbor sampling
python examples/05_pytorch_pyg.py

# 6. SciPy sparse matrix, csgraph Dijkstra & GraphBLAS matrix math
python examples/06_scipy_sparse.py

# 7. Columnar data interchange with Apache Arrow, Polars & Pandas
python examples/07_arrow_polars.py

# 8. NetworkX graph construction, PageRank & shortest paths
python examples/08_networkx.py
```

---

## 5. Python API Quick Reference

### Snapshot Data Interchange (PyTorch, SciPy, Arrow, Polars)

```python
from impulse_graph import Snapshot

with Snapshot("graph.imps") as snap:
    # 1. PyTorch Sparse CSR Tensor
    torch_csr = snap.to_torch_csr(relation_index=0)
    
    # 2. SciPy CSR Matrix
    scipy_mat = snap.to_scipy_csr(relation_index=0)
    
    # 3. Polars / Pandas DataFrame
    df = snap.to_polars(relation_index=0)
    
    # 4. SIMD GNN Neighborhood Sampling
    srcs, tgts = snap.sample_neighbors(relation_index=0, nodes=[0, 1], k_samples=3)
```

### Fluent `QueryBuilder` Example

```python
from impulse_graph import Snapshot, vm

# 1. Build a bytecode query using QueryBuilder
builder = vm.QueryBuilder()
query = (
    builder.input_node(0)  # Seed node ID 0
    .walk_edge(relation_id=0)  # 1-hop CSR neighbors
    .walk_edge(relation_id=0)  # 2-hop CSR neighbors
    .collect_bitset()  # Collect destination frontier
    .compile()
)

# 2. Execute query against snapshot
with Snapshot("graph.imps") as snap:
    result = snap.execute_query(query, input_param=0)
    print("Execution Success:", result.is_ok())
    print("Result Register R" + str(result.result_register) + ":", hex(result.raw_value))
```

### Snapshot Writer Constructors

```python
from impulse_graph import Writer
import scipy.sparse as sp
import torch

# From SciPy sparse matrix
adj = sp.random(1000, 1000, density=0.01, format="csr")
Writer.from_scipy("scipy_graph.imps", adj)

# From PyTorch COO edge_index
edge_index = torch.tensor([[0, 1, 2], [1, 2, 0]], dtype=torch.int64)
Writer.from_torch("torch_graph.imps", edge_index, num_nodes=3)
```
