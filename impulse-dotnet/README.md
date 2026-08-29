# Impulse Graph Engine .NET SDK (`impulse-dotnet`)

The official .NET (C# / F#) SDK for **Impulse Graph Engine** (`impulse-graph`), providing zero-copy memory-mapped binary snapshot analysis, P/Invoke native bindings into the `ImpulseVM` bytecode engine, and high-throughput multi-hop graph traversals.

---

## 1. Features & Architectural Highlights

* **Zero-Copy Memory-Mapped Snapshots (`.imps`)**: Read multi-terabyte binary graph snapshots directly off-heap without .NET GC allocations.
* **Native C-ABI P/Invoke VM Engine**: Direct bindings into the `ImpulseVM` register architecture (`R0`..`R63`) and hardware SIMD instructions.
* **Fluent `QueryBuilder` API**: Construct, compile, and execute `impOps` bytecode queries directly in C# or F#.
* **Direct Point Reachability**: Fast sub-microsecond point edge checks.
* **Cross-Platform**: Targets `.NET 8.0+` across macOS, Linux, and Windows.

---

## 2. Prerequisites & Building from Source

### Prerequisites

| Tool | Minimum Version | Notes |
| :--- | :--- | :--- |
| **.NET SDK** | 8.0+ | `dotnet` CLI toolchain. |
| **C++ Core Build** | `impulse-cpp` | Build native shared library (`libimpulse_graph.dylib` / `.so` / `.dll`). |

### Build from Source

First, ensure the native C++ library is built:
```bash
cd impulse-graph-core/impulse-cpp
cmake -B build && cmake --build build
```

Then build the .NET library:
```bash
cd ../impulse-dotnet
dotnet build
```

---

## 3. Running the Self-Contained Test Suite

All unit tests are **100% self-contained** and execute offline without external dataset dependencies:

```bash
# Run .NET unit tests
dotnet test tests/
```

Test coverage includes:
* Snapshot opening, magic bytes, and domain/relation metadata validation.
* Direct point-to-point reachability queries.
* QueryBuilder bytecode assembly and VM execution.
* VmContext off-heap bitset allocations.

---

## 4. Running Examples & Sample Datasets

The `examples/` directory contains standalone .NET sample projects.

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
dotnet run --project examples/01_snapshot_basics

# 2. Multi-hop traversal on social network graph
dotnet run --project examples/02_social_traversal

# 3. Relationship-Based Access Control (ReBAC) authorization
dotnet run --project examples/03_rbac_reachability

# 4. Low-level bytecode compilation & analytical VM queries
dotnet run --project examples/04_analytical_queries
```

---

## 5. C# API Quick Reference

### Fluent Traversal Example


### Zero-Allocation Vector Iteration (`ReadOnlySpan<T>`)

Impulse Graph is designed to minimize .NET GC pressure during graph analytical queries. C# FFI bindings map unmanaged pointers directly to `ReadOnlySpan<T>` over the C++ VM memory contexts.

```csharp
using var ctx = new VmContext(snap);
using var state = new VmState();

// Execute analytical bytecode
var result = compiledQuery.ExecuteWithContext(ctx, state, 0UL);

// Extract zero-allocation span over off-heap memory
ReadOnlySpan<ulong> nodes = ctx.GetNodeVector(result.RawValue);
Console.WriteLine($"Found {nodes.Length} reachable targets.");

// Iterate without allocating managed arrays
foreach (ulong node in nodes) {
    Console.WriteLine(node);
}
```

```csharp
using System;
using ImpulseGraph;
using ImpulseGraph.Vm;

// Open snapshot with zero-copy mmap
using var snap = new Snapshot("social_graph.imps");

// 2-Hop Traversal: Seed(0) -> follows -> follows -> collectBitset
var query = new QueryBuilder()
    .InputNode(0)
    .WalkEdge(0)
    .WalkEdge(0)
    .CollectBitset()
    .Compile();

using var result = snap.ExecuteQuery(query, inputParam: 0UL);
Console.WriteLine($"Query Succeeded: {result.IsOk}");
Console.WriteLine($"Result Bitset Register: R{result.ResultRegister}");
```
