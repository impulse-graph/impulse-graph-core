# Snapshot Interchange Guide: Using `.imps` as an Off-Heap Data Format

This guide details how to use the **Impulse Binary Snapshot Format (`.imps`)** purely as an immutable, zero-copy, off-heap data interchange format for **PyTorch**, **PyTorch Geometric (PyG)**, **SciPy**, **NumPy**, **Apache Arrow**, **Polars**, **Pandas**, and **NetworkX**, alongside side-by-side **ImpulseVM (ImpK, ImpLog, Cypher, Fluent Traversal)** execution snippets.

---

## 1. The `.imps` Format as an Open Graph Storage Standard

The Impulse Binary Snapshot Format (`.imps`) addresses the same challenge for graph analytics that **Apache Parquet** and **Apache Arrow** addressed for tabular analytics: **decoupling data storage from heavy runtime database servers**.

```
    ┌─────────────────────────────────────────────────────────────────┐
    │           Unified Immutable Binary Snapshot (.imps)             │
    │  [Page 0: 4KB Header] [Section 2: String Pool] [Section 4: Rel] │
    │  [Section 5: CSR/CSC Offsets, Target Buffers, SoA Attributes]   │
    └────────────────────────────────┬────────────────────────────────┘
                                     │ Direct OS Memory Mapping (mmap)
         ┌───────────────────────────┼───────────────────────────┐
         ▼                           ▼                           ▼
  ┌──────────────┐            ┌──────────────┐            ┌──────────────┐
  │   PyTorch    │            │    SciPy     │            │ Polars/Arrow │
  │ & PyG (GNNs) │            │ Sparse CSR   │            │ DataFrames   │
  └──────────────┘            └──────────────┘            └──────────────┘
```

### Physical Layout & Alignment
- **Fixed 4KB Page 0**: 64-byte active baseline, magic `0x494D5053` (`IMPS`), format version `0x0009` (`9`).
- **128-Byte Hardware Alignment**: All CSR row offsets, column targets, and columnar attribute sections are 128-byte aligned to maximize AVX-512 vector lanes, GPU warp coalescing (NVIDIA GPUDirect Storage `cuFile`), and TPU vector tiles.
- **Off-Heap Zero-Copy (`OWNDATA=False`)**: Data structures map into user virtual memory via `mmap`. Opening a 50 GB snapshot consumes sub-millisecond cold start time and **0 bytes of Python heap RAM**.

---

## 2. Read-Only Memory Mapping & Warning Mitigation

Snapshots are mapped into virtual memory in **strict Read-Only mode** (`PROT_READ`, `MAP_SHARED` / `MAP_PRIVATE`).

### Pure Read / Forward Hot Paths (Inference & Feature Extraction)
When feeding graph topology into PyTorch sparse matrix multiplications (`torch.sparse_csr_tensor`), SciPy solvers (`csr_matrix`), or GNN message-passing forward passes, tensors reference physical memory pages directly with zero copying:

```python
import torch
import numpy as np
from impulse_graph import Snapshot

with Snapshot("graph.imps") as snap:
    # Zero-copy memoryview into off-heap OS memory
    row_offsets = snap.get_row_offsets_array(relation_index=0)
    col_indices = snap.get_col_indices_array(relation_index=0)

    # Flag verification: Memory is owned off-heap by mmap
    assert not col_indices.flags.owndata
```

### PyTorch Writable Tensor Warnings & Autograd Best Practices
PyTorch expects mutable memory buffers for tensors participating in gradient backpropagation. Wrapping a read-only NumPy array can trigger:
```text
UserWarning: The given NumPy array is not writable, and PyTorch does not support non-writable tensors.
```

#### Mitigation Patterns:
1. **Read-Only Topology (CSR Edge Indices)**: Topology indices (`indptr`, `indices`) do not require gradients. Constructing tensors with `torch.from_numpy(arr.astype(np.int64))` creates an integer index tensor that is safe for sparse matrix multiplication.
2. **Trainable Edge/Node Embeddings**: When feature weights require in-place autograd mutations or GPU training, transfer the tensor to target device memory:
   ```python
   # 1. Zero-copy load from disk into CPU memoryview
   feat_np = snap.get_attribute_array(relation_index=0, attribute_index=0, shape=(num_nodes, 128))

   # 2. Transfer to GPU or clone for trainable gradient updates
   x = torch.from_numpy(feat_np).clone().to("cuda")  # or "mps"
   x.requires_grad = True
   ```

---

## 3. Creating `.imps` from Existing Structures

The Python `Writer` provides convenience constructors alongside explicit streaming APIs.

### 3.1 From SciPy Sparse Matrices (`csr_matrix` / `csc_matrix`)

```python
from impulse_graph import Writer
import scipy.sparse as sp

# Generate or load a SciPy CSR matrix
adj = sp.random(10000, 10000, density=0.001, format="csr", dtype=np.float32)

# Compile into .imps snapshot
Writer.from_scipy("scipy_graph.imps", adj, domain_name="Node")
```

### 3.2 From PyTorch / PyG `edge_index` Tensors

```python
import torch
from impulse_graph import Writer

# PyTorch Geometric COO edge_index (2, num_edges)
edge_index = torch.tensor([[0, 0, 1, 2, 2], [1, 2, 3, 0, 3]], dtype=torch.int64)

# Compile into .imps snapshot
Writer.from_torch("torch_graph.imps", edge_index, num_nodes=4)
```

### 3.3 From Polars / Pandas DataFrames & Apache Arrow Tables

```python
import polars as pl
from impulse_graph import Writer

df = pl.DataFrame(
    {"src": ["alice", "alice", "bob", "carol"], "dst": ["bob", "carol", "dan", "dan"]}
)

# Ingests DataFrame, deduplicates keys, and creates string catalog
Writer.from_dataframe("social.imps", df, src_col="src", tgt_col="dst", domain_name="User")
```

### 3.4 Explicit Step-by-Step Streaming Writer Recipe

For custom ETL pipelines with explicit attribute control:

```python
from impulse_graph import Writer

with Writer("custom.imps") as writer:
    # 1. Define Domains
    writer.add_domain(domain_id=0, key_type=4, name="Account")  # KeyType: INT64

    # 2. Define CSR Topology
    row_offsets = [0, 2, 3, 4]
    col_indices = [1, 2, 2, 0]

    writer.add_relation(
        src_domain_id=0,
        tgt_domain_id=0,
        encoding_type=0,  # RAW CSR
        node_count=3,
        edge_count=4,
        section_features=0,
        row_offsets=row_offsets,
        col_indices=col_indices,
    )

    # 3. Finalize header and checksums
    writer.finalize()
```

---

## 4. Framework Interop & Side-by-Side ImpulseVM Equivalences

### 4.1 PyTorch & PyTorch Geometric (PyG)

#### External Framework Approach:
```python
import torch
from impulse_graph import Snapshot

with Snapshot("social_graph.imps") as snap:
    # Convert directly to PyTorch sparse CSR tensor
    adj_csr = snap.to_torch_csr(relation_index=0)
    
    # Convert to PyG COO edge_index (2, E)
    edge_index = snap.to_torch_edge_index(relation_index=0)
    
    # Mini-batch GNN Neighborhood Sampling
    src_nodes, tgt_nodes = snap.sample_neighbors(relation_index=0, nodes=[0, 1], k_samples=5)
```

#### ImpulseVM Equivalence (1-Liner):
```python
# Multi-hop GNN neighborhood expansion executed in C++ SIMD without materializing PyG tensors:
frontier = snap.traverse(start_node=0).out("follows").out("follows").to_list()

# Or Datalog ImpLog declarative rule:
# reachable(X, Z) :- follows(X, Y), follows(Y, Z).
```

---

### 4.2 SciPy Sparse Matrix & GraphBLAS Algorithms

#### External Framework Approach:
```python
import scipy.sparse as sp
from scipy.sparse.csgraph import dijkstra, connected_components
from impulse_graph import Snapshot

with Snapshot("rbac_snapshot.imps") as snap:
    # Zero-copy SciPy CSR matrix
    mat = snap.to_scipy_csr(relation_index=0)
    
    # Run SciPy shortest path algorithms
    dist_matrix = dijkstra(mat, directed=True, indices=0)
    n_components, labels = connected_components(mat)
```

#### ImpulseVM Equivalence (1-Liners):
```python
# 1. Multi-Hop Shortest Path / Breadth-First Search:
reachable_nodes = snap.traverse(start_node=0).out(0).to_list()

# 2. GraphBLAS Power-Iteration PageRank via ImpK (20 SIMD matrix-vector iterations):
# ImpK DSL: (repeat 20 (assign p (+ (* 0.85 (mxv A p)) (/ 0.15 N))))
pr_query = (
    vm.QueryBuilder()
    .repeat(20, lambda q: q.sparse_mat_vec(matrix_reg=0, vec_reg=1, out_reg=1))
    .compile()
)
pr_result = snap.execute_query(pr_query)
```

---

### 4.3 Apache Arrow, Polars & Pandas Tabular Analytics

#### External Framework Approach:
```python
import polars as pl
import pyarrow as pa
from impulse_graph import Snapshot

with Snapshot("financial_transactions.imps") as snap:
    # Export edge list as Apache Arrow Table
    arrow_table = snap.to_arrow_table(relation_index=0)
    
    # Export as Polars DataFrame for columnar analytics
    df = snap.to_polars(relation_index=0)
    
    # GroupBy degree aggregation in Polars
    out_degrees = df.group_by("src").len().sort("len", descending=True)
```

#### ImpulseVM Equivalence (1-Liner):
```python
# Run declarative openCypher query directly over the memory-mapped file off-heap:
top_recipients = snap.cypher(
    "MATCH (a:Account)-[r:TRANSFERS_TO]->(b:Account) WHERE id(a) = 0 RETURN b"
)
```

---

### 4.4 NetworkX Graph Analytics

#### External Framework Approach:
```python
import networkx as nx
from impulse_graph import Snapshot

with Snapshot("social_graph.imps") as snap:
    # Construct NetworkX DiGraph directly from snapshot CSR slices
    G = snap.to_networkx(relation_index=0)
    
    # Run NetworkX algorithms
    pagerank_scores = nx.pagerank(G)
    shortest_paths = nx.single_source_shortest_path_length(G, source=0)
```

#### ImpulseVM Equivalence (1-Liner):
```python
# Fluent Traversal with zero Python object allocation:
reachable = snap.traverse(start_node=0).out(0).to_list()
```

---

## 5. Rich Columnar Attributes (Structure of Arrays - SoA)

Attributes in `.imps` are stored in columnar Structure of Arrays (SoA) layout rather than row-based tuples, enabling SIMD vector registers to load numeric values in contiguous cache line bursts.

```python
with Snapshot("financial_graph.imps") as snap:
    # 1. 1D Scalar Edge Attribute (e.g. float32 transaction amounts)
    amounts = snap.get_attribute_array(relation_index=0, attribute_index=0, dtype=np.float32)
    print("Mean Transfer Amount:", np.mean(amounts))
    
    # 2. 2D Node Feature Embeddings (e.g. 128-dimensional dense vectors)
    node_count = snap.get_relation(0)["node_count"]
    embeddings = snap.get_attribute_array(
        relation_index=0, attribute_index=1, dtype=np.float32, shape=(node_count, 128)
    )
    
    # Wrap in PyTorch Tensor without memory copies
    x_tensor = torch.from_numpy(embeddings)
```

---

## 6. Domain Key Catalogs & Bidirectional Key Resolution

`.imps` snapshots decouple external business identifiers (UUIDs, string keys, URLs) from 0-indexed dense internal node integers (`0..N-1`).

```python
with Snapshot("hetionet.v09.imps") as graph:
    # 1. Forward Lookup: String Key -> Dense Node ID via O(1) MPHF lookup
    disease_id = graph.resolve_dense_id(domain_id=4, key="Disease::DOID:10652")

    # 2. Execute PyTorch / SciPy / ImpulseVM graph traversal
    target_node_ids = graph.traverse(start_node=disease_id).out("DaG").to_list()

    # 3. Batch Reverse Lookup: Dense Node IDs -> Human-Readable String Keys
    target_names = graph.resolve_keys_batch(domain_id=5, node_ids=target_node_ids)

    # 4. Assemble Tabular Results in Polars DataFrame
    results_df = pl.DataFrame({"dense_id": target_node_ids, "gene_key": target_names})
```

---

## 7. Virtual Relations: Transpose Views, Multiplexing & Partitioning

Virtual relations in `.imps` allow developers to define **logical relation views** in schema manifests and queries without duplicating physical edges on disk or in RAM.

### 7.1 Combining & Multiplexing Edge Types
In rich heterogeneous graphs (e.g. social networks, e-commerce, knowledge graphs), multiple distinct physical interactions exist between the same domain pairs:
- Physical relations: `VIEW`, `LIKE`, `DISLIKE`, `POSTED`, `COMMENTED`

Rather than materializing a redundant physical table for combined engagement, a **virtual relation** is defined logically:
- `USER_TO_POST_CONTENT` = `POSTED + COMMENTED` (authoring interactions)
- `USER_ENGAGEMENT` = `VIEW + LIKE + COMMENTED` (all positive interactions)

When queried, ImpulseVM dynamically unions the underlying CSR offset frontiers in SIMD registers (`OP_BITSET_OR`), executing in microseconds without allocating intermediate tables:

```python
# In openCypher: Traversal over combined virtual edge types:
# MATCH (u:User)-[:POSTED|COMMENTED]->(p:Post) RETURN p
posts = snap.cypher("MATCH (u:User)-[:POSTED|COMMENTED]->(p:Post) WHERE id(u) = 0 RETURN p")
```

### 7.2 Partitioning & Filtered Slices
Virtual relations also enable logical slicing and partitioning across edge attributes (e.g. transaction date windows, confidence scores, or high-value transfers):
- `TRANSFERS_LARGE` = `TRANSFERS WHERE amount >= 10000.0`
- `VERIFIED_FOLLOWS` = `FOLLOWS WHERE verified == true`

Predicate pushdown evaluates filters directly during the CSR walk instruction, skipping non-matching targets before frontier registers are populated.

### 7.3 Pre-Indexed CSC Transpose Views (Inverse Relations)
For bidirectional algorithms (e.g. GNN message passing on undirected graphs, PageRank incoming link calculations, backward reachability):
- **Pre-Indexed CSC Buffers**: Physical snapshots can store pre-sorted CSC column offset and row index arrays alongside CSR topology, enabling $O(1)$ off-heap incoming edge lookups without runtime sorting or transposition overhead.
- **Virtual Inverse Relations**: Schema catalogs define virtual reverse edges (e.g. `FOLLOWED_BY` as the inverse of `FOLLOWS`) referencing existing relation CSC buffers, avoiding the creation of separate relation entries or duplicating edge attributes and metadata.

```python
with Snapshot("graph.imps") as snap:
    # 1. Forward CSR Matrix (Outgoing edges: User -> follows -> User)
    fwd_csr = snap.to_torch_csr(relation_index=0, transpose=False)
    
    # 2. Reverse CSC Matrix (Incoming edges: User <- followed_by <- User)
    # Reads the pre-compiled CSC column index buffers directly from the snapshot
    rev_csc = snap.to_torch_csr(relation_index=0, transpose=True)
```

---

## 8. Pure Zero-Dependency Format Mechanics (Standard `mmap` + `struct`)

Any external script or tool can read `.imps` snapshots without the Impulse Graph C++ kernel or Python SDK using standard Python library primitives:

```python
import mmap
import struct

with open("graph.imps", "rb") as f:
    # 1. Memory-map snapshot file read-only
    mm = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
    
    # 2. Parse 64-byte baseline Page 0 header
    magic, version, domains, relations = struct.unpack_from("<IHHI", mm, 0)
    assert magic == 0x494D5053, "Invalid .imps file format"
    
    print(f"Spec Version: {version}, Domains: {domains}, Relations: {relations}")
    
    # 3. Read Relation 0 Directory Entry (Offset 0x0100)
    # Layout: src_domain(u16), tgt_domain(u16), encoding(u8), flags(u8), node_count(u64), edge_count(u64), offsets...
    src_dom, tgt_dom, enc, flags, n_nodes, n_edges = struct.unpack_from("<HHBBQQ", mm, 256)
    row_off_ptr, row_off_len, col_idx_ptr, col_idx_len = struct.unpack_from("<QQQQ", mm, 256 + 24)
    
    # 4. Zero-copy buffer extraction
    row_offsets_view = memoryview(mm)[row_off_ptr : row_off_ptr + row_off_len]
    col_indices_view = memoryview(mm)[col_idx_ptr : col_idx_ptr + col_idx_len]
    
    print(f"Nodes: {n_nodes}, Edges: {n_edges}, Buffer Bytes: {len(col_indices_view)}")
```
