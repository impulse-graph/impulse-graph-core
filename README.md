# Impulse Graph Engine (`impulse-graph-core`)

[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE)
[![C++ Standard](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![Spec Compliance](https://img.shields.io/badge/Spec-v0.9.0-green.svg)](https://github.com/impulse-graph/impulse-graph-spec)
[![Platform](https://img.shields.io/badge/Platform-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey.svg)](#-cross-platform-hardware-support)
[![Dependencies](https://img.shields.io/badge/Dependencies-0%20(Zero)-brightgreen.svg)](#-zero-third-party-runtime-dependencies)

High-performance, zero-copy, SIMD-vectorized C++20 graph traversal engine and polyglot native bindings. 

Impulse Graph is the **"SQLite for Graphs"** and the **"Apache Arrow for Graph Analytics"** — an embedded, serverless, single-file binary snapshot engine designed for sub-millisecond cold starts, sub-microsecond vector traversals, and multi-terabyte scale graph analytics without database servers or garbage collection pauses.

---

## 🛡️ Zero Third-Party Runtime Dependencies

Impulse Graph maintains **strictly zero external runtime dependencies**:

* **C++20 Kernel (`impulse-cpp`)**: Pure standard library implementation (`<cstdlib>`, `<vector>`, `<sys/mman.h>`). Requires no external shared libraries or runtime frameworks.
* **Rust Crate (`impulse-rust`)**: `Cargo.toml` has `[dependencies]` completely empty.
* **Java Engine (`impulse-graph-java`)**: Pure JDK 25 Foreign Function & Memory (FFM) API with zero Maven runtime dependencies.
* **Zero Supply-Chain Risk**: Immune to transitive dependency vulnerabilities, CVE bloat, and runtime licensing conflicts.
* **Universal Portability**: Compiles statically into standalone executables or links dynamically into container images (`FROM scratch` / Alpine).

---

## 🖥️ Cross-Platform Hardware Support

Impulse Graph compiles natively and runs across all major operating systems and CPU architectures:

| Operating System | Architectures | Compiler Toolchain | Acceleration / SIMD |
| :--- | :--- | :--- | :--- |
| **Linux** | `x86_64` (AMD64), `aarch64` (ARM64) | GCC 12+, Clang 16+ | AVX-512, AVX2, ARM Neon, Highway SIMD, OpenMP |
| **macOS** | `arm64` (Apple Silicon M1-M4), `x86_64` (Intel) | Apple Clang / Xcode 15+ | ARM Neon, AVX2, Direct Threaded Dispatch |
| **Windows** | `x64` (AMD64 / Intel 64) | MSVC 2022+ (`/MT` Static CRT), Clang-CL | AVX-512, AVX2, Highway SIMD |

---

## ⚡ Features & Architectural Tradeoffs

* **The SQLite / Parquet of Graphs**: Runs embedded directly inside your application process without database servers, daemons, socket overhead, or network serialization.
* **Impulse Binary Snapshot Format (`.imps`)**: All adjacency structures (CSR/CSC), node/edge attributes (Structure of Arrays), string tables, and secondary indexes reside in an open, immutable, 128-byte hardware-aligned C-ABI binary snapshot file ([Specification v0.9.0](https://github.com/impulse-graph/impulse-graph-spec)).
* **Lock-Free & Allocation-Free Hot Path**: Query hot paths execute without mutexes, spinlocks, or heap memory allocations. SIMD bitsets and vector registers execute directly off-heap over memory-mapped (`mmap`) storage.
* **Read-Only (RO) Immutable Design Tradeoff**: To guarantee maximum SIMD throughput and zero lock contention, `.imps` files are strictly **immutable and read-only**. Dynamic updates and streaming mutations are handled out-of-band by snapshot compilers streaming new versions direct to disk.
* **Zero Third-Party Runtime Dependencies**: The core C++20 kernel, Rust crate, and native FFI layers maintain **0 external runtime dependencies**.

---

## 🌐 Ecosystem Repositories

* **[impulse-graph-spec](https://github.com/impulse-graph/impulse-graph-spec)**: Normative C-ABI Binary Snapshot v0.9.0 Format Specification and shared cross-language test vectors (`tc01`..`tc36`).
* **[impulse-graph-java](https://github.com/impulse-graph/impulse-graph-java)**: Pure Java 25 Foreign Function & Memory (FFM) off-heap snapshot engine:
  * **Kotlin SDK** (`impulse-kotlin`): Coroutine-enabled async extensions and type-safe DSL builders.
  * **Scala 3 SDK** (`impulse-scala_3`): Opaque types and functional GraphBLAS combinators.
  * **Clojure SDK** (`impulse-clojure`): Homoiconic S-Expression and threading macro (`->`) graph querying.
* **[impulse-graph-tooling](https://github.com/impulse-graph/impulse-graph-tooling)**: Developer utilities (`impulse assemble`, `disassemble`, `compile`, `inspect`, `opt`).
* **[impulse-benchmarks](https://github.com/impulse-graph/impulse-benchmarks)**: Reproducible macro benchmark suite comparing Impulse against Neo4j, PyG, NetworkX, and MATPOWER.

---

## 💻 Language Bindings Supported by C++ Engine

All bindings link against the native C++20 engine kernel with zero serialization overhead:

* **[C++20](./impulse-cpp/)**: Native zero-copy memory-mapped kernel, Highway SIMD vectorization, and C-ABI headers.
* **[Python](./impulse-python/)**: PyBind11 bindings with zero-copy NumPy/PyTorch `mmap` tensor integration and fluent API.
* **[Go](./impulse-go/)**: High-performance CGO bindings with fluent multi-hop traversal builder and openCypher queries.
* **[C# / .NET 9](./impulse-dotnet/)**: High-performance P/Invoke managed bindings with span-based memory access.
* **[Node.js / TypeScript](./impulse-node/)**: Node-API (N-API) native addon for Node.js, Bun, and TypeScript runtimes.
* **[Rust](./impulse-rust/)**: Safe Rust engine crate (`impulse-graph`) implementing zero-copy reading, snapshot generation, and fluent traversals.

---

## 🔍 Supported Query Languages & DSLs

Impulse Graph supports multiple declarative and programmatic query frontends, all compiling into the unified `ImpScheme` IR:

* **[Fluent Traversal + Google CEL](https://docs.impulsegraph.io/query/fluent/)**: Multi-hop edge traversal with embedded Google Common Expression Language (CEL) predicate filters (`edge.affinity <= $maxAffinity`).
* **[ImpLog (Datalog)](https://docs.impulsegraph.io/query/implog/)**: Declarative Datalog logic rules, stratification validation (negation cycle detection), Magic Sets rewriting, and recursive reachability.
* **[ImpK (GraphBLAS)](https://docs.impulsegraph.io/query/impk/)**: Matrix-vector semiring math (`OP_MXV`), PageRank (`pr`), and Afforest connected components (`OP_CC_AFFOREST`).
* **[openCypher](https://docs.impulsegraph.io/query/cypher/)**: Industry-standard graph pattern matching (`MATCH (d:Disease)-[:DaG]->(g:Gene) WHERE ... RETURN ...`).

---

## 🏛️ Architecture Overview

Impulse Graph decouples analytical graph execution from storage by pairing an immutable binary format (`.imps`) with a register-based virtual machine (**ImpulseVM**).

```
+-----------------------------------------------------------------------+
|                         Application Process                           |
|  +------------+  +------------+  +------------+  +-----------------+  |
|  | Python SDK |  |   Go SDK   |  |  Node.js   |  | .NET / C# / Rust|  |
|  +-----+------+  +-----+------+  +-----+------+  +--------+--------+  |
|        |               |               |                  |           |
|  +-----v---------------v---------------v------------------v--------+  |
|  |                    Impulse C-ABI Kernel                         |  |
|  |  +-----------------------------------------------------------+  |  |
|  |  |     Frontend DSLs (Fluent/CEL, ImpLog, ImpK, Cypher)      |  |  |
|  |  +-----------------------------+-----------------------------+  |  |
|  |                                | (AST)                          |  |
|  |  +-----------------------------v-----------------------------+  |  |
|  |  |           ImpScheme Homoiconic S-Expression IR            |  |  |
|  |  +-----------------------------+-----------------------------+  |  |
|  |                                | (Optimized impOps)             |  |
|  |  +-----------------------------v-----------------------------+  |  |
|  |  |       ImpulseVM (SIMD Vector Execution & OpenMP)          |  |  |
|  |  +-----------------------------+-----------------------------+  |  |
|  +--------------------------------|--------------------------------+  |
|                                   | (Zero-Copy Pointer Access)        |
|  +--------------------------------v--------------------------------+  |
|  |          Immutable Binary Snapshot (.imps, mmap, RO)            |  |
|  +-----------------------------------------------------------------+  |
+-----------------------------------------------------------------------+
```

### On-The-Fly Compilation (Zero Query Cache Overhead)
Because the zero-dependency C++ AST optimizer and bytecode emitter compile in single-digit microseconds (< 3 µs), Impulse Graph compiles queries **on-the-fly for every execution by default**. This avoids the lock contention, memory overhead, and invalidation bugs of traditional query plan caches.

👉 **Read the full architectural deep dive**: **[docs/ARCHITECTURE.md](docs/ARCHITECTURE.md)**

---

## ⚙️ The ImpulseVM Engine

* **Register-Based ISA (`impOps`)**: 64 virtual registers (`R0`..`R63`) supporting scalar node IDs, bitsets, dense numeric vectors, and string offsets.
* **Direct Threaded Dispatch**: Utilizes computed goto (`dispatch_table[]`) on modern Clang and GCC to eliminate branch misprediction latency in instruction dispatch.
* **Google Highway Portable SIMD**: Automatically selects AVX-512, AVX2, ARM Neon, or SVE vector instructions at runtime.
* **Parallel Execution**: OpenMP intra-query thread pooling for large-scale matrix and frontier evaluations.

---

## 📊 Performance & Continuous Benchmarks

Impulse Graph is built for world-class performance and continuous empirical verification. All published performance figures are derived exclusively from actual runtime executions on physical hardware.

### 4-Hop Traversal Latency (Hetionet Dataset, 1,317 Candidate Compounds)
* **Native C++20 Engine**: **18.4 µs**
* **Rust SDK**: **19.1 µs** (+0.7 µs FFI overhead)
* **Go SDK**: **21.5 µs** (+3.1 µs FFI overhead)
* **C# / .NET 9 SDK**: **23.0 µs** (+4.6 µs FFI overhead)
* **Python SDK**: **24.2 µs** (+5.8 µs FFI overhead)
* **Node.js SDK**: **26.8 µs** (+8.4 µs FFI overhead)

👉 **Read the full benchmark report & cross-engine comparison**: **[docs/BENCHMARKS.md](docs/BENCHMARKS.md)**

---

## 🔧 Global Dataset Resolution (`IMPULSEGRAPH_DATA_DIR`)

You can configure a global snapshot directory to eliminate hardcoded file paths across all bindings:

```bash
export IMPULSEGRAPH_DATA_DIR=~/impulse/datasets
```

When opening snapshots (e.g. `Snapshot("hetionet.v09.imps")`), all SDKs automatically resolve:
1. Exact file path in current working directory (CWD).
2. `$IMPULSEGRAPH_DATA_DIR/<path>`.
3. `$IMPULSEGRAPH_DATA_DIR/<dataset_name>/<path>`.

---

## 🛠️ Build & Test Instructions

### C++ Kernel & Test Suite
```bash
cd impulse-cpp
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j8
ctest --test-dir build --output-on-failure
```

### Python SDK
```bash
cd impulse-python
pip install -e .
python -m unittest discover tests
```

### Go SDK
```bash
cd impulse-go
go test -v ./...
```

### Rust Crate
```bash
cd impulse-rust
cargo test
```

### Node.js Native Addon
```bash
cd impulse-node
npm install
npm test
```

---

## 📄 License

Licensed under the Apache License, Version 2.0. See [LICENSE](LICENSE) for details.
