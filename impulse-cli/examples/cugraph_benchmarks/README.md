# cuGraph Benchmark Reimplementations & Feature Gap Analysis

This directory contains reimplementations of **RAPIDS cuGraph** (NVIDIA CUDA GPU graph engine) algorithms and full C++ unit test suites (`cugraph/cpp/tests/`) written in **`ImpK`** (`.impk`) and **`ImpLog`** (`.implog`), designed to test `imp asm` (`.impas`) assembly text generation and evaluate feature gaps against `ImpulseVM` (`impOps`).

---

## 1. Complete Inventory of Reimplemented Algorithms & C++ Test Suites (18 Total)

| Benchmark Script | Category | Dialect | Original cuGraph Reference / C++ Test | Generated `impOps` Opcodes |
| :--- | :--- | :--- | :--- | :--- |
| [`pagerank.impk`](pagerank.impk) | Centrality | `ImpK` | `cugraph/cpp/src/centrality/pagerank.cu` | `OP_MXV`, `SEMIRING_PLUS_TIMES`, `OP_EWISE_FMA_SCALAR` |
| [`bfs.impk`](bfs.impk) | Traversal | `ImpK` | `cugraph/cpp/src/traversal/bfs.cu` | `OP_CSR_WALK`, `OP_ROARING_BITMAP_AND_NOT`, `OP_ROARING_BITMAP_OR` |
| [`wcc.impk`](wcc.impk) | Components | `ImpK` | `cugraph/cpp/src/components/weakly_connected_components.cu` | `OP_MXV`, `SEMIRING_MIN_MIN`, `OP_EWISE_MIN_INT` |
| [`triangles.impk`](triangles.impk) | Community | `ImpK` | `cugraph/cpp/src/community/triangle_count.cu` | `OP_MXM` (SpGEMM), Hadamard mask `&` |
| [`sssp.impk`](sssp.impk) | Traversal | `ImpK` | `cugraph/cpp/src/traversal/sssp.cu` | `OP_MXV`, `SEMIRING_MIN_PLUS`, `OP_EWISE_MIN_INT` |
| [`hits.impk`](hits.impk) | Link Analysis | `ImpK` | `cugraph/cpp/src/link_analysis/hits.cu` | `OP_MXV` (Forward/Reverse CSC), `OP_EWISE_DIV_FLOAT` |
| [`k_core.impk`](k_core.impk) | Cores | `ImpK` | `cugraph/cpp/src/cores/k_core.cu` | `OP_CSR_DEGREE`, `OP_ROARING_BITMAP_AND_NOT` |
| [`sample_neighbors.impk`](sample_neighbors.impk) | GNN Sampling | `ImpK` | `cugraph/cpp/src/sampling/sample_neighbors.cu` | `OP_CSR_WALK`, PyTorch tensor mmap |
| [`authz_rebac.implog`](authz_rebac.implog) | ReBAC / Logic | `ImpLog` | Transitive closure / reachability | `OP_CSR_WALK`, `OP_ROARING_BITMAP_AND_NOT` (Fixpoint) |
| [`jaccard_similarity.impk`](jaccard_similarity.impk) | Similarity | `ImpK` | `cugraph/cpp/tests/link_prediction/similarity_test.cu` | `OP_ROARING_BITMAP_AND`, `OP_BITSET_COUNT` |
| [`cosine_similarity.impk`](cosine_similarity.impk) | Similarity | `ImpK` | `cugraph/cpp/tests/link_prediction/similarity_test.cu` | `OP_ROARING_BITMAP_AND`, `OP_EWISE_DIV_FLOAT` |
| [`katz_centrality.impk`](katz_centrality.impk) | Centrality | `ImpK` | `cugraph/cpp/tests/centrality/katz_centrality_test.cpp` | `OP_MXV`, `SEMIRING_PLUS_TIMES`, `OP_EWISE_FMA_SCALAR` |
| [`eigenvector_centrality.impk`](eigenvector_centrality.impk) | Centrality | `ImpK` | `cugraph/cpp/tests/centrality/eigenvector_centrality_test.cpp` | `OP_MXV`, Power Iteration, `OP_EWISE_DIV_FLOAT` |
| [`k_truss.impk`](k_truss.impk) | Community | `ImpK` | `cugraph/cpp/tests/community/k_truss_test.cpp` | `OP_MXM` (SpGEMM), `OP_ROARING_BITMAP_AND_NOT` |
| [`mst.impk`](mst.impk) | Tree / Spanning | `ImpK` | `cugraph/cpp/tests/tree/mst_test.cu` | `OP_EWISE_MIN_EDGE`, Boruvka / Kruskal MST |
| [`topological_sort.impk`](topological_sort.impk) | DAG Traversal | `ImpK` | `cugraph/cpp/tests/dag/topological_sort_test.cpp` | Kahn's algorithm, `OP_CSR_DEGREE`, `OP_ROARING_BITMAP_AND_NOT` |
| [`random_walks.impk`](random_walks.impk) | Sampling | `ImpK` | `cugraph/cpp/tests/sampling/sg_random_walks_test.cpp` | `OP_SAMPLE_RANDOM_NEIGHBOR`, Node2Vec / RWR |
| [`betweenness_centrality.impk`](betweenness_centrality.impk) | Centrality | `ImpK` | `cugraph/cpp/tests/centrality/betweenness_centrality_test.cpp` | Brandes algorithm, Forward/Backward dependencies |

---

## 2. Expanded Feature Gap Analysis: cuGraph (CUDA GPU) vs. Impulse Graph Engine (`ImpulseVM`)

### A. Tree & DAG Traversal Primitives

1. **Minimum Spanning Tree (MST)**:
   - `cugraph::minimum_spanning_tree` uses GPU edge reductions.
   - *Recommendation for `impOps`*: Add `OP_CSR_MIN_OUTGOING_EDGE` for Boruvka component edge selection.

2. **Topological Sort (DAG)**:
   - `cugraph::topological_sort` orders DAG nodes.
   - *Recommendation for `impOps`*: Add zero in-degree tracking optimization during CSR walks.

3. **Random Walk Primitives**:
   - `cugraph::random_walks` generates random walks (Node2Vec, DeepWalk).
   - *Recommendation for `impOps`*: Add `OP_SAMPLE_RANDOM_NEIGHBOR` opcode.

---

## 3. Testing Assembly Generation (`.impas`)

To verify assembly generation for any benchmark script:

```bash
# Generate .impas assembly text for Minimum Spanning Tree
impulse compiler compile examples/cugraph_benchmarks/mst.impk

# Generate .impas assembly text for Topological Sort
impulse compiler compile examples/cugraph_benchmarks/topological_sort.impk
```
