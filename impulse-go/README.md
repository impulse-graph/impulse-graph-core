# Impulse Graph Engine Go SDK & CGo VM Binding (`impulse-go`)

The official Go package for **Impulse Graph Engine** (`impulse-graph`), providing high-performance zero-copy memory-mapped binary snapshot analysis, CGo native bindings into the `ImpulseVM` bytecode engine, and multi-hop graph traversals.

---

## 1. Features & Architectural Highlights

* **Zero-Copy Memory-Mapped Snapshots (`.imps`)**: Off-heap snapshot reading via OS memory mapping (`mmap`) without GC allocation or heap copying overhead.
* **CGo Native VM Engine**: High-speed CGo bindings into the register-based `ImpulseVM` bytecode execution core.
* **Fluent QueryBuilder & Traversal API**: Compose and compile impOps instructions or use high-level fluent traversal pipelines (`snap.Traverse(0).Out("follows").ToSlice()`).
* **Direct Point Reachability & Delta Layers**: Fast sub-microsecond point edge checks and thread-safe concurrent delta layer compaction.
* **Zero External Dependencies**: Pure Go and CGo linking directly into the core C++ static kernel (`libimpulse_graph_static.a`).

---

## 2. Prerequisites & Building from Source

### Prerequisites

| Tool | Minimum Version | Notes |
| :--- | :--- | :--- |
| **Go** | 1.22+ | Standard Go toolchain with `CGO_ENABLED=1`. |
| **C++ Compiler** | GCC 11+, Clang 14+ | Required for CGo static linking. |
| **C++ Core Build** | `impulse-cpp` | Build `libimpulse_graph_static.a` in `impulse-cpp/build/`. |

### Build from Source

First, ensure the C++ static kernel is built:
```bash
cd impulse-graph-core/impulse-cpp
cmake -B build && cmake --build build
```

Then test and build the Go package:
```bash
cd ../impulse-go
go build ./...
```

---

## 3. Running the Self-Contained Test Suite

All unit tests are **100% self-contained** in the package and execute offline without external dataset downloads:

```bash
# Run Go unit tests
go test -v ./...
```

Test coverage includes:
* `snapshot_test.go`: Off-heap zero-copy snapshot open and domain/relation metadata.
* `delta_test.go`: Live overlay delta layer creation, edge insertion, tombstones, and compaction.
* `vm_test.go`: QueryBuilder bytecode compilation, VmContext memory pools, and execution.
* `simd_test.go`: Hardware SIMD vector math and dynamic target detection.
* `writer_test.go`: Single-pass snapshot writer and header validation.

---

## 4. Running Examples & Sample Datasets

The `examples/` directory contains standalone Go sample programs.

### Standard Sample Datasets Setup

Examples that load canonical snapshots (`social_graph.imps`, `rbac_snapshot.imps`, etc.) automatically locate `.imps` files via the **`IMPULSE_DATASETS_DIR`** environment variable:

1. **Download & Extract Sample Datasets**:
   ```bash
   curl -fsSLO https://github.com/impulse-graph/impulse-graph-core/releases/download/v0.9.0/impulse-sample-datasets.tar.gz
   tar -xzf impulse-sample-datasets.tar.gz -C ~/impulse-datasets/
   ```

2. **Export Environment Variable**:
   ```bash
   export IMPULSE_DATASETS_DIR=~/impulse-datasets
   ```

*(Note: If `IMPULSE_DATASETS_DIR` is unset, examples automatically search local fallback paths or generate local sample graphs).*

### Running Examples

```bash
# 1. Snapshot creation and zero-copy inspection
go run ./examples/01_snapshot_basics

# 2. Multi-hop traversal on social network graph
go run ./examples/02_social_traversal

# 3. Relationship-Based Access Control (ReBAC) authorization
go run ./examples/03_rbac_reachability

# 4. Low-level bytecode compilation & analytical VM queries
go run ./examples/04_analytical_queries
```

---

## 5. Go API Quick Reference

### Fluent Traversal Pipeline

```go
package main

import (
	"fmt"
	"log"

	impulse "github.com/impulse-graph/impulse-graph/go"
)

func main() {
	snap, err := impulse.OpenSnapshot("social_graph.imps")
	if err != nil {
		log.Fatalf("Failed to open snapshot: %v", err)
	}
	defer snap.Close()

	// 2-Hop Traversal: Seed(0) -> follows -> follows
	friends, err := snap.Traverse("0", 0).
		Out("0").
		ToSlice()
	if err != nil {
		log.Fatalf("Traversal failed: %v", err)
	}

	fmt.Printf("2-Hop Reachable Users from User 0: %v\n", friends)
}
```

### Snapshot Writer

```go
package main

import (
	"log"

	impulse "github.com/impulse-graph/impulse-graph/go"
)

func main() {
	writer, err := impulse.NewWriter("sample_graph.imps", 0)
	if err != nil {
		log.Fatalf("Failed to create writer: %v", err)
	}
	defer writer.Close()

	writer.AddDomain(0, impulse.KeyTypeInt64, "User")

	rowOffsets := []uint32{0, 2, 4, 5, 5}
	colIndices := []uint32{1, 2, 2, 3, 3}

	err = writer.AddRelation(0, 0, 0, 4, 5, 0, rowOffsets, colIndices)
	if err != nil {
		log.Fatalf("Failed to add relation: %v", err)
	}

	if err := writer.Finalize(); err != nil {
		log.Fatalf("Finalize failed: %v", err)
	}
}
```

## Idiomatic & Safe Zero-Copy Memory
The Go bindings utilize strongly-typed View structures (e.g., `Float32VectorView`, `NodeVectorView`) wrapping `unsafe.Slice`. These view objects explicitly hold a reference to their parent `VmContext`, strictly guaranteeing that Go's Garbage Collector will not finalize the underlying C++ VM buffers while you are still accessing the slice. 

> [!WARNING]
> Long-running graph traversals executing synchronously via CGO can block OS threads and starve the Goroutine scheduler. For operations executing millions of hops, ensure your VM execution is chunked, or rely on explicit context yields in your Go application logic.
