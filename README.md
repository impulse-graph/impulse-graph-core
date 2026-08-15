# Impulse Graph Engine (`impulse-graph-core`)

[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE)
[![C++ Standard](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![Spec Compliance](https://img.shields.io/badge/Spec-v0.9.0-green.svg)](https://github.com/impulse-graph/impulse-graph-spec)
[![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey.svg)](#platform-support--zero-dependencies)
[![Dependencies](https://img.shields.io/badge/Dependencies-0%20(Zero)-brightgreen.svg)](#platform-support--zero-dependencies)

> [!WARNING]
> **Pre-release Documentation**: This documentation describes pre-release software under active development and may be inaccurate, incomplete, or missing.

Impulse Graph is an embedded graph analytics engine. Think **SQLite, but for graphs** — open a single `.imps` snapshot file, query it from any language, no database server required.

It fills a missing gap in the open-source ecosystem as the **Apache Arrow / Parquet equivalent for graph analytics**. Rather than running a heavy database server, you compile your graph data into an immutable binary snapshot (`.imps`), memory-map it, and run traversals and linear algebra directly over the mapped memory. Datasets from thousands to billions of edges, cold starts under a millisecond, queries in microseconds.

---

## Quick Start

> [!NOTE]
> **Pre-release Notice**: Binary wheels and package registry artifacts are currently in pre-release testing; building from source is currently required (see [Build & Test](#build--test)).

### 1. Install

```bash
pip install impulse-graph
```

### 2. Download a Sample Snapshot

```bash
curl -LO https://github.com/impulse-graph/impulse-graph-samples/releases/download/v0.9.0/hetionet.v09.imps
```

> [!NOTE]
> This is [Hetionet](https://het.io), a biomedical knowledge graph with 47K nodes, 2.25M edges, and 24 relation types (Disease→Gene, Compound→Gene, Gene→Pathway, etc.). The `.imps` file is ~10 MB.

### 3. Query

The easiest way to start is with an openCypher query.

```python
from impulse_graph import Snapshot

with Snapshot("hetionet.v09.imps") as graph:
    # 1. Inspect the schema catalog
    print(f"Loaded {graph.domain_count()} domains, {graph.relation_count()} relations")
    
    # 2. Run an openCypher query directly over the memory-mapped file
    # This finds compounds targeting genes associated with Epilepsy
    query = """
        MATCH (d:Disease)-[:DaG]->(g1:Gene)-[:GpPW]->(p:Pathway)<-[:GpPW]-(g2:Gene)<-[:CbG]-(c:Compound) 
        WHERE d.name = $diseaseName 
        RETURN c
    """
    
    candidates = graph.cypher(query, params={"diseaseName": "epilepsy syndrome"}, catalog="hetionet")
    print(f"Found {len(candidates)} candidate compounds")
```

> [!NOTE]
> **Set Semantics vs Bag Semantics**: To guarantee predictable, sub-microsecond vector execution, Impulse Graph's openCypher engine implements strict **Set semantics** rather than relational Multiset/Bag semantics, eliminating duplicate frontier materialization and costly deduplication barriers.

### 4. Building Your Own Snapshots

To compile your own datasets into `.imps` files, install the [Impulse CLI tooling](https://github.com/impulse-graph/impulse-graph-tooling):

```bash
# Install the CLI
cargo install impulse-graph-tools

# Create a manifest describing your graph schema
cat > manifest.json << 'EOF'
{
  "version": "0.9.0",
  "domains": [
    { "id": 0, "name": "User", "key_type": "string" },
    { "id": 1, "name": "Product", "key_type": "string" }
  ],
  "relations": [
    { "src_domain": 0, "tgt_domain": 1, "encoding": "raw", "file": "purchases.tsv" }
  ]
}
EOF

# Build the snapshot
impulse build -m manifest.json -o my_graph.imps

# Inspect it
impulse inspect my_graph.imps
```

> [!TIP]
> **Input Formats & Auto-Discovery**: Impulse CLI supports TSV, CSV, and Parquet relation tables with optional automatic attribute discovery and schema inference.

---

## Language Bindings & Multi-Language Usage

All native bindings link against the C++20 kernel. Queries execute in the C++ engine regardless of host language — the binding layer is just the call boundary.

For JVM applications (Java 25, Kotlin, Scala 3, Clojure), see the standalone [**impulse-graph-java**](https://github.com/impulse-graph/impulse-graph-java) implementation built on pure JDK 25 Foreign Function & Memory (FFM) and Vector API with full feature parity.

| Language | Directory | Install | API Style |
| :--- | :--- | :--- | :--- |
| **Python** | [`impulse-python/`](./impulse-python/) | `pip install impulse-graph` | Fluent traversal + CEL, NumPy/PyTorch/SciPy interop |
| **C++20** | [`impulse-cpp/`](./impulse-cpp/) | CMake / system install | C-ABI + fluent `QueryBuilder` |
| **Rust** | [`impulse-rust/`](./impulse-rust/) | `cargo add impulse-graph` | Safe wrapper, fluent traversal |
| **Go** | [`impulse-go/`](./impulse-go/) | `go get github.com/impulse-graph/impulse-graph/go` | CGO, fluent traversal |
| **Node.js** | [`impulse-node/`](./impulse-node/) | `npm install impulse-graph` | N-API, fluent traversal |
| **C# / .NET 9** | [`impulse-dotnet/`](./impulse-dotnet/) | NuGet `ImpulseGraph` | P/Invoke, `QueryBuilder` |

<details>
<summary><b>Python (Fluent Builder + Filters)</b></summary>

Both openCypher and the fluent builder compile through the exact same **ImpScheme IR** pipeline down to identical SIMD bytecode. The fluent builder enables programmatic traversal construction with optional attribute filters.

👉 **[Explore Python fluent traversal documentation & examples →](impulse-python/)**

</details>

<details>
<summary><b>Zero-Copy Direct Tensor / Matrix Access (No Query Engine Required)</b></summary>

You can also use `.imps` binary snapshots directly as a zero-copy, off-heap drop-in storage format for PyTorch tensors, SciPy CSR/CSC sparse matrices, and NumPy arrays without invoking the query VM:

```python
import numpy as np
import torch
from scipy.sparse import csr_matrix
from impulse_graph import Snapshot

with Snapshot("hetionet.v09.imps") as graph:
    # Zero-copy memory-mapped slice of CSR row offsets and col indices
    row_offsets = graph.get_row_offsets_array(relation_index=0)
    col_indices = graph.get_col_indices_array(relation_index=0)

    # Zero-allocation SciPy sparse matrix
    adj = csr_matrix((np.ones_like(col_indices), col_indices, row_offsets))

    # Direct PyTorch tensor wrapping for GNN neighbor sampling
    edge_index = torch.from_numpy(col_indices)
```

👉 **[Explore Python bindings & GNN tensor integration →](impulse-python/)**

</details>

<details>
<summary><b>C++</b></summary>

```cpp
#include <impulse_graph.h>
#include <cstdio>

int main() {
    impulse_status_t st;
    impulse_snapshot_t* graph = impulse_snapshot_open("hetionet.v09.imps", &st);

    // Resolve string keys to dense node IDs via O(1) MPHF index lookup
    uint32_t disease_id = 0, gene_id = 0;
    impulse_snapshot_resolve_key(graph, 4, "Disease::DOID:10652", &disease_id); // Epilepsy
    impulse_snapshot_resolve_key(graph, 5, "Gene::5231", &gene_id);             // Target gene

    // Point reachability query
    bool connected = impulse_snapshot_is_reachable(graph, 0, disease_id, gene_id);
    printf("Disease %u → Gene %u connected: %s\n", disease_id, gene_id, connected ? "yes" : "no");

    impulse_snapshot_close(graph);
}
```

Or with the C++ fluent QueryBuilder:

```cpp
#include <impulse_vm_fluent.hpp>

using namespace impulse::vm;

QueryBuilder builder;
builder.inputNodeParam()  // Use parameterized input instead of hardcoded ID
       .walkEdge(0)       // DaG
       .walkEdge(1)       // GpPW
       .walkCsc(1)        // GpPW reverse
       .walkCsc(2)        // CbG reverse
       .collectBitset();

CompiledQuery query = builder.compile();
impulse_vm_state_t state{};
// Execute with parameter value mapped from domain index
QueryResult result = query.executeWithContext(nullptr, &state, disease_id);
printf("Status: %s\n", result.isOk() ? "OK" : "ERROR");
```

</details>

<details>
<summary><b>Rust</b></summary>

```rust
use impulse_graph::SnapshotReader;

let reader = SnapshotReader::open("hetionet.v09.imps").unwrap();
let disease_id = reader.resolve_dense_id(4, "Disease::DOID:10652").unwrap();

let candidates = reader.traverse(disease_id)
    .out("DaG")
    .out("GpPW")
    .in_step("GpPW")
    .in_step("CbG")
    .to_vec()
    .unwrap();

println!("Found {} candidate compounds", candidates.len());
```

</details>

<details>
<summary><b>Go</b></summary>

```go
package main

import (
    "fmt"
    impulse "github.com/impulse-graph/impulse-graph/go"
)

func main() {
    graph, _ := impulse.OpenSnapshot("hetionet.v09.imps")
    defer graph.Close()

    diseaseId, _ := graph.ResolveDenseId(4, "Disease::DOID:10652")

    candidates, _ := graph.Traverse(diseaseId).
        Out("DaG").
        Out("GpPW").
        In("GpPW").
        In("CbG").
        ToSlice()

    fmt.Printf("Found %d candidate compounds\n", len(candidates))
}
```

</details>

<details>
<summary><b>Node.js</b></summary>

```js
const { Snapshot } = require('impulse-graph');

const graph = new Snapshot('hetionet.v09.imps');
const diseaseId = graph.resolveDenseId(4, 'Disease::DOID:10652');

const candidates = graph.traverse(diseaseId)
    .out("DaG")       // Disease → Gene
    .out("GpPW")      // Gene → Pathway
    .in("GpPW")       // Pathway ← Gene
    .in("CbG")        // Gene ← Compound
    .toArray();

console.log(`Found ${candidates.length} candidate compounds`);
graph.close();
```

</details>

<details>
<summary><b>C# / .NET</b></summary>

```csharp
using ImpulseGraph;
using ImpulseGraph.Vm;

using var graph = new Snapshot("hetionet.v09.imps");
uint diseaseId = graph.ResolveDenseId(4, "Disease::DOID:10652");

var query = new QueryBuilder()
    .InputNodeParam()  // Use parameterized input
    .WalkEdge(0)       // DaG
    .WalkEdge(1)       // GpPW
    .CscWalk(1)        // GpPW reverse
    .CscWalk(2)        // CbG reverse
    .CollectBitset()
    .Compile();

var result = graph.ExecuteQuery(query, inputParam: diseaseId);
Console.WriteLine($"Status: {result.Status}");
```

</details>

---

## Query Languages

All query frontends compile through the shared **ImpScheme** (`.impscm`) intermediate representation into `impOps` bytecode. Because every frontend maps to the same underlying ImpulseVM execution pipeline, **all query languages benefit equally from SIMD-vectorized execution, zero-copy off-heap traversal, and sub-3 µs on-the-fly compilation**.

| Language | Style | Feature Highlights & Use Cases |
| :--- | :--- | :--- |
| **[Fluent Traversal + CEL](https://docs.impulsegraph.io/query/fluent/)** *(Recommended)* | Programmatic builder with CEL filters | **Most expressive & fullest featured**: Multi-hop path queries, SIMD predicate pushdown, rich attribute filtering |
| **[openCypher](https://docs.impulsegraph.io/query/cypher/)** | Declarative pattern matching (Set semantics) | Familiar Cypher syntax: `MATCH (d:Disease)-[:DaG]->(g:Gene) WHERE ... RETURN ...` |
| **[ImpLog](https://docs.impulsegraph.io/query/implog/)** (`.implog`) | Declarative Datalog | Recursive reachability, ReBAC/Zanzibar authorization, transitive closure |
| **[ImpK](https://docs.impulsegraph.io/query/impk/)** (`.impk`) | GraphBLAS matrix math | SIMD PageRank, connected components, semiring vector operations |

---

## Query Execution

All query frontends compile down to a shared S-Expression IR (**ImpScheme**), which is optimized and emitted as ImpulseVM bytecode:

```
  Application Code (Python, Go, Rust, C#, Node.js, C++)
                          │
                    C-ABI Kernel
                          │
         ┌────────────────┼────────────────┐
    Fluent/CEL        ImpLog           ImpK / Cypher
         └────────────────┼────────────────┘
                          │ AST
                    ImpScheme IR
                   (S-Expressions)
                          │ Optimized impOps
                     ImpulseVM
                  (SIMD + OpenMP)
                          │ Zero-copy pointers
                  .imps Snapshot (mmap, RO)
```

The compiler pipeline includes constant folding, direction selection (CSR push vs CSC pull), 2-hop kernel fusion, register ping-ponging, predicate pushdown, seed inlining, and early-exit optimization — all completing in < 3 µs.

👉 **[Full architecture deep dive →](docs/ARCHITECTURE.md)**

---

## How It Works

### The Impulse Binary Snapshot Format (`.imps`)

The snapshot format took the best ideas from **Apache Parquet** and **Apache Arrow** and optimized them for graph analytics:

- **Memory-map friendly**: Designed from the ground up so that files map directly into user space with zero deserialization, decoding, or heap allocation. Open and query multi-gigabyte graphs in under a millisecond.
- **Hardware-aligned**: All internal structures are 128-byte aligned for NVMe/SSD sector reads, SIMD vector lanes (AVX-512, ARM Neon), GPU warp coalescing (NVIDIA GPUDirect `cuFile`), and TPU vector tiles.
- **Rich graph support**: Strongly typed nodes and edges across multi-domain schemas. Relations stored as sparse matrices — **CSR** (Compressed Sparse Row), **CSC** (Compressed Sparse Column), and **COO** (Coordinate List) — with advanced encodings including **SIMDComp**, **Delta-VByte**, and **ELLPACK**.
- **Open specification**: The binary format is a [public C-ABI specification](https://github.com/impulse-graph/impulse-graph-spec) with cross-language compliance test vectors.

### The ImpulseVM Execution Engine

The **ImpulseVM** is a register-based graph query Virtual Machine inspired by **Lua's register VM**:

- **64 polymorphic registers** (`R0`..`R63`) holding node IDs, bitset frontiers, numeric vectors, and string pool offsets — no stack push/pop overhead.
- **Direct-threaded dispatch** via compiler computed-goto jump tables on Clang/GCC, eliminating branch misprediction in the instruction loop.
- **SIMD-vectorized opcodes** (`impOps`, `0x00`..`0x72`) mapping to hardware primitives: CSR/CSC walks, fused 2-hop traversals, GraphBLAS matrix-vector multiply, Afforest connected components.
- **On-the-fly compilation** in < 3 µs via the homoiconic **ImpScheme** IR — faster than a thread-safe cache lookup, so every query is compiled fresh with zero lock contention.

### Key Tradeoffs

| Get | Give |
| :--- | :--- |
| Lock-free, allocation-free query hot paths | `.imps` snapshots are **read-only and immutable** |
| Sub-millisecond cold start, no server process | Data must be compiled to `.imps` format first |
| Zero runtime dependencies, embeds anywhere | No built-in write path — mutations produce new snapshots |

---

## Performance

4-hop traversal latency across language bindings (Hetionet, 1,317 candidate compounds):

| Binding | Latency | FFI Overhead |
| :--- | :--- | :--- |
| **C++20** (Clang 18, computed goto) | **18.4 µs** | — |
| **Rust** | 19.1 µs | +0.7 µs |
| **Go** (CGO) | 21.5 µs | +3.1 µs |
| **C# / .NET 9** (P/Invoke) | 23.0 µs | +4.6 µs |
| **Python** (PyBind11) | 24.2 µs | +5.8 µs |
| **Node.js** (N-API) | 26.8 µs | +8.4 µs |

Comparison against other graph engines on the same dataset:

| Engine | Storage Model | Cold Start Time | 4-Hop Query Latency | Memory Footprint (RAM) |
| :--- | :--- | :--- | :--- | :--- |
| **Impulse Graph** | Immutable `.imps` (Mmap) | **< 1 ms** | **0.018 ms** (18 µs) | **0 MB** (Off-heap / OS Cache) |
| **PyG (PyTorch Geo)** | CSR / Tensor | 450 ms | 0.850 ms (850 µs) | 410 MB (PyTorch Tensors) |
| **NetworkX** | Python In-Memory Dicts | 1,850 ms | 42.100 ms (42,100 µs) | 890 MB (Python Objects) |

> [!NOTE]
> **Honest & Reproducible Benchmarks**: We seek to provide honest, reproducible benchmark results across all tested systems. Suggestions for benchmark methodology improvements, optimization tuning for baseline engines, or additional graph engines to test are always welcome via issues or pull requests.

👉 **[Full benchmark report →](docs/BENCHMARKS.md)**

---

## Ecosystem

| Repository | Description |
| :--- | :--- |
| **[impulse-graph-spec](https://github.com/impulse-graph/impulse-graph-spec)** | C-ABI Binary Snapshot v0.9.0 format specification and cross-language test vectors (`tc01`..`tc36`) |
| **[impulse-graph-java](https://github.com/impulse-graph/impulse-graph-java)** | Pure Java 25 FFM off-heap snapshot engine with 100% feature parity with C++ — includes [Kotlin](https://github.com/impulse-graph/impulse-graph-java/tree/main/impulse-kotlin), [Scala 3](https://github.com/impulse-graph/impulse-graph-java/tree/main/impulse-scala), and [Clojure](https://github.com/impulse-graph/impulse-graph-java/tree/main/impulse-clojure) SDKs |
| **[impulse-graph-tooling](https://github.com/impulse-graph/impulse-graph-tooling)** | CLI utilities: `impulse build`, `inspect`, `validate`, `compile`, `optimize`, `assemble`, `export` |
| **[impulse-benchmarks](https://github.com/impulse-graph/impulse-benchmarks)** | Reproducible benchmark suite vs PyG, NetworkX, MATPOWER |
| **[impulse-website](https://github.com/impulse-graph/impulse-website)** | Documentation portal ([docs.impulsegraph.io](https://docs.impulsegraph.io)) |

---

## Platform Support & Zero Dependencies

Impulse Graph maintains **zero third-party runtime dependencies**. The C++20 kernel uses only standard library headers. The Rust crate has an empty `[dependencies]`. The Java engine uses only JDK 25 FFM. No transitive dependency vulnerabilities, no supply-chain risk.

| OS | Architectures | Toolchain | SIMD |
| :--- | :--- | :--- | :--- |
| **Linux** | x86_64, aarch64 | GCC 12+, Clang 16+ | AVX-512, AVX2, ARM Neon |
| **macOS** | arm64 (Apple Silicon), x86_64 | Apple Clang / Xcode 15+ | ARM Neon, AVX2 |
| **Windows** | x64 | MSVC 2022+ (`/MT`), Clang-CL | AVX-512, AVX2 |

---

## Configuration

Set `IMPULSEGRAPH_DATA_DIR` to avoid hardcoding snapshot paths:

```bash
export IMPULSEGRAPH_DATA_DIR=~/impulse/datasets
```

All SDKs resolve snapshot paths in order: exact path / CWD → `$IMPULSEGRAPH_DATA_DIR/<path>` → `$IMPULSEGRAPH_DATA_DIR/<dataset>/<path>`.

---

## Build & Test

> [!NOTE]
> **Pre-release Source Builds**: During pre-release, building from source is required across all native components.

```bash
# C++ kernel
cd impulse-cpp && cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j8
ctest --test-dir build --output-on-failure

# Python
cd impulse-python && pip install -e . && python -m unittest discover tests

# Rust
cd impulse-rust && cargo test

# Go
cd impulse-go && go test -v ./...

# Node.js
cd impulse-node && npm install && npm test
```

---

## License

Apache License 2.0. See [LICENSE](LICENSE) for details.
