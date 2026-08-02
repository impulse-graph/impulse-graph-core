# Impulse Graph Engine: Software Modules & Repository Architecture

This document defines the logical software module organization, class boundaries, polyglot workspace, and repository sitemap for **Impulse Graph Engine**.

> **Naming Notice**: The project and codebase are strictly named **Impulse Graph Engine** (`impulse-graph`). Legacy terminology (`abac-engine`) is retired.

---

## 1. Core Logical Software Modules & Classes

### 1.1 `ImpulseGraphSnapshot` (Read-Only Binary Snapshot Interface)
* **Purpose**: Provides a read-only, zero-copy interface for querying immutable graph snapshots.
* **Underlying Storage**: Backed directly by memory-mapped (`mmap`) off-heap files conforming to the [C-ABI Binary Snapshot Specification v2.3](FORMAT_SPECIFICATION.md).
* **Guarantees**: Thread-safe, lock-free, zero-allocation reads; 64-byte / 128-byte cache-line and 4KB page aligned.

### 1.2 `ImpulseGraph` (Snapshot + Live Delta Overlay)
* **Purpose**: Represents an active, writable graph instance combining an immutable base `ImpulseGraphSnapshot` with a dynamic in-memory delta overlay (`CsrDeltaLayer`).
* **Concurrency Model**: Lock-free reads across base snapshot + delta overlay using atomic reference swaps (`VarHandle` / atomic pointers) and `RoaringBitmap` tombstones/additions.
* **Mutations**: Write operations append edges to the in-memory delta layer without mutating the underlying base snapshot.

### 1.3 `ImpulseGraphQuery` (Immutable Query AST)
* **Purpose**: Strongly-typed, serializable representation of a graph query (e.g. reachability, transitive closure, set intersection, neighborhood extraction, bitwise attribute filtering).
* **Read-Only**: Immutable intent definition, completely decoupled from execution logic. Can be evaluated against either an `ImpulseGraphSnapshot` (RO) or an `ImpulseGraph` (RW).

### 1.4 `ImpulseGraphQueryEvaluator` (SIMD Vector Execution Engine)
* **Purpose**: Evaluates an `ImpulseGraphQuery` AST against a target graph instance.
* **Optimization**: Employs SIMD vector acceleration (Java `VectorAPI`, C++ AVX2/AVX-512/NEON, Rust SIMD) over off-heap CSR matrices and delta arrays. Operates lock-free and allocation-free in query hot paths.

### 1.5 `SnapshotBuilder` (Streaming Compactor & Snapshot Generator)
* **Purpose**: Builds a brand-new immutable binary snapshot by compacting an active `ImpulseGraph` (base snapshot + delta overlay) or combining multiple snapshots.
* **Memory & Storage Strategy**:
  - **Streaming Direct-to-Disk**: Writes snapshot sections (`SnapshotHeader`, `RelationDirectory`, `CSR Topology`, `ID Mappings`, `DTO Payloads`) sequentially to disk in page-aligned blocks while calculating the SHA-256 checksum over `Section 2..EOF`.
  - **Scale & Memory Footprint**: Assumes disk space is cheap and abundant, keeping physical RAM usage bounded to O(chunk) rather than O(|V| + |E|). This allows building multi-hundred-gigabyte or terabyte-scale (TB+) snapshots without risking Out-Of-Memory (OOM) failures on memory-constrained pods.

---

## 2. Polyglot Tri-Core Architecture & Repository Organization

The core engine is structured as a **Unified Polyglot Workspace** in the `impulse-graph` repository:

```
impulse-graph/
├── docs/
│   ├── spec/                   # C-ABI Binary Snapshot Specification (FORMAT_SPECIFICATION.md)
│   └── software-modules.md     # Software Modules & System Design (This File)
├── test-vectors/               # Language-agnostic binary snapshot test files
├── impulse-java/               # Pure Java 25 Engine & Modules (Maven)
│   ├── impulse-spec/           # C-ABI binary protocol constants & FFM layout definitions
│   ├── impulse-api/            # Interfaces: Snapshot, Graph, Query, Evaluator, Builder
│   ├── impulse-core/           # Pure in-memory FFM engine (0 external dependencies)
│   ├── impulse-store/          # Optional RocksDB cold-start persistence & WAL logger
│   └── impulse-ingestion/      # Optional streaming CDC (Kafka/Pulsar) event consumer
├── impulse-cpp/                # C++20 Native Engine Kernel (CMake & C-ABI C header libimpulse_graph)
├── impulse-rust/               # Rust Native Engine Kernel & Crate
├── impulse-kotlin/             # Kotlin Coroutines & DSL
├── impulse-scala/              # Scala 3 Extension Methods & ZIO
├── impulse-python/             # Python Bindings & PyTorch Zero-Copy mmap Tensors
├── impulse-csharp/             # .NET 9 P/Invoke Managed API
└── impulse-fsharp/             # F# Functional Facade
```

### 2.1 Language Strategy
1. **Java (`impulse-java`)**: Pure Java 25 implementation using Foreign Function & Memory (FFM `Arena` / `MemorySegment`) and `VectorAPI`. Critical for enterprise JVM integration with zero native binary compilation overhead.
2. **C++20 (`impulse-cpp`)**: Native C++ kernel delivering the canonical C-ABI C header (`impulse_graph.h`) for embedded, high-performance, and native bindings (Python, .NET, Rust FFI).
3. **Rust (`impulse-rust`)**: Safe, concurrent native implementation providing zero-cost Rust abstractions and C-ABI export.

### 2.2 Dependency & Modularity Rule
* **`impulse-core` is Ultra-Lean**: Pure in-memory graph primitives with **zero third-party dependencies**.
* **Storage and Ingestion Decoupled**: Disk persistence (RocksDB), WAL sinking, and CDC ingestion are isolated into optional extension modules (`impulse-store`, `impulse-ingestion`), preventing dependency bloat in lightweight embedded or query-only environments.

---

## 3. Core Kernel Functional Equivalence & Offline Optimizers

### 3.1 Minimal Core Kernel Contract (The 5 Primitives)
To guarantee 100% functional equivalence across Java, C++, and Rust, every core kernel implementation is strictly scoped to **5 minimal primitives**:

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    Core Kernel Functional Contract                      │
├─────────────────────────────────────────────────────────────────────────┤
│ 1. Binary Loader/Parser --> Memory-maps C-ABI Section 1 & 2 (Snapshot)  │
│ 2. Read/Write Overlay   --> Layers CsrDeltaLayer over base Snapshot     │
│ 3. Query AST Builder    --> Defines strongly-typed immutable Query AST   │
│ 4. SIMD Query Evaluator --> Executes lock-free vector queries           │
│ 5. Snapshot Writer      --> Streams page-aligned binary files to disk   │
└─────────────────────────────────────────────────────────────────────────┘
```

This tight contract ensures that any language implementation can be validated against the shared `test-vectors/` suite without needing heavy database connectors, complex graph reordering algorithms, or third-party libraries.

### 3.2 Offline Heavy Optimizer & Developer Tools (`impulse-tools`)

Heavy graph optimizations (which can take minutes or hours on multi-terabyte graphs) and developer inspection tools are **explicitly consolidated** into **`impulse-tools`** (C++ / CMake / Rust):

```
+------------------+       Stream Write       +--------------------+
|  Live Impulse    | -----------------------> | Base Binary        |
|  Graph / Pod     |                          | Snapshot File      |
+------------------+                          +--------------------+
                                                        |
                                                        v
                                              +--------------------+
                                              | impulse-tools      | (C++ / Rust Tool Suite)
                                              | • impulse-opt      | (RCM, SIMD-PFOR, Prune)
                                              | • impulse-inspect  | (Header & Directory CLI)
                                              +--------------------+
                                                        |
                                                        v
+------------------+        Zero-Copy         +--------------------+
| High-Speed Pods  | <----------------------- | Optimized Binary   |
| (Java/C++/Rust)  |        mmap Read         | Snapshot File v2.3 |
+------------------+                          +--------------------+
```

#### What `impulse-tools` provides:
* **`impulse-opt` (Snapshot Optimizer & Compressor)**:
  - **Optional Section Stripping**: Strips Section 4 (ID Mappings), Section 5 (DTO Payloads), or Section 6 (Delta Log) for hyper-compressed edge snapshots.
  - **Optional Section Injection**: Injects external ID dictionaries (Section 4) or DTO property payloads (Section 5) into raw topology snapshots.
  - **Topology Compression Codecs**: Applies compression encodings (`0x01=SIMD-PFOR`, `0x02=RoaringBitmaps`, `0x03=Varint-Delta`, `0x04=Elias-Fano`, `0x05=Sliced ELLPACK`, `0x06=TPU Tile BCOO`, `0x0A=RAW_UINT64`).
  - **Cache-Line & SIMD Reordering**: Performs RCM or Hilbert curve node renumbering for 128-byte cache-line SIMD locality.
* **`impulse-inspect` (Developer Inspection CLI)**:
  - Inspects `.imps` binary headers, validates SHA-256 checksums, dumps Section 2 directories, and runs quick test queries from terminal.

---

## 4. Ecosystem Showcase Repositories

Four dedicated **Showcase Repositories** highlight real-world use cases across key industries:

```
                               ┌───────────────────────────────────┐
                               │     impulse-graph (Core Spec)     │
                               └─────────────────┬─────────────────┘
                                                 │
      ┌──────────────────────┬───────────────────┼───────────────────┬──────────────────────┐
      ▼                      ▼                   ▼                   ▼                      ▼
┌───────────┐          ┌───────────┐       ┌───────────┐       ┌───────────┐          ┌───────────┐
│ impulse-  │          │ impulse-  │       │ impulse-  │       │ impulse-  │          │ impulse-  │
│ powergrid │          │    gnn    │       │   authz   │       │  fintech  │          │ platform  │
└───────────┘          └───────────┘       └───────────┘       └───────────┘          └───────────┘
```

### 4.1 `impulse-powergrid` — Critical Infrastructure & Grid Stability
* **Domain**: Real-time electrical power systems engineering, Balancing Authority monitoring, and grid reliability.
* **Key Features**: CIM XML / CGMES compiler, SIMD connected-component island detector, 10,000 parallel N-1 contingency simulator, and 60 Hz PMU stream consumer.

### 4.2 `impulse-gnn` — Python PyTorch AI/ML Graph Acceleration
* **Domain**: High-performance Graph Neural Network (GNN) training and LLM knowledge graph embeddings.
* **Key Features**: Zero-copy PyTorch `torch.sparse_csr_tensor` `mmap` integration and native SIMD C++ neighborhood sampler (`torch.ops.impulse.sample_neighbors`).

### 4.3 `impulse-authz` — Enterprise Fine-Grained Authorization (ReBAC / Zanzibar)
* **Domain**: Real-time Relationship-Based Access Control (ReBAC) & ABAC for SaaS and cloud platforms.
* **Key Features**: Sub-microsecond permission graph evaluation (5,000,000+ checks/sec/pod) with live Debezium CDC streaming sync.

### 4.4 `impulse-fintech` — Real-Time Anti-Money Laundering (AML) & Fraud Detection
* **Domain**: Financial crime detection, synthetic identity detection, and transaction flow analysis.
* **Key Features**: Real-time circular money laundering loop detection (A -> B -> C -> A) and high-speed transaction graph pattern matching.

---

## 5. GitHub Organization Master Repository Sitemap

The following **10 repositories** form the complete **Impulse Graph Ecosystem** in the GitHub organization:

```
┌──────────────────────────────────────────────────────────────────────────┐
│                 GitHub Organization Repository Sitemap                   │
├──────────────────────────────────────────────────────────────────────────┤
│ CORE KERNEL & SPECIFICATION                                              │
│  1. impulse-graph      --> Core polyglot workspace (C-ABI, Java, C++, Rust│
│                            Python, C#, F#, Kotlin, Scala)                │
│  2. impulse-tools      --> C++ offline optimizer, inspector CLI & tools  │
│  3. impulse-benchmarks --> Continuous micro/macro performance benchmarks │
│                                                                          │
│ ENTERPRISE CLOUD PLATFORM                                                │
│  4. impulse-platform   --> Spring Boot, K8s, GCS/S3, Kafka, RocksDB store  │
│                                                                          │
│ INDUSTRY SHOWCASE REPOS                                                  │
│  5. impulse-powergrid  --> Electrical grid stability & N-1 simulator     │
│  6. impulse-gnn        --> PyTorch / PyG zero-copy GNN acceleration      │
│  7. impulse-authz      --> ReBAC / Zanzibar fine-grained authorization  │
│  8. impulse-fintech    --> Anti-Money Laundering & fraud ring detection  │
│                                                                          │
│ GOVERNANCE & DOCUMENTATION                                               │
│  9. .github            --> Org profile, issue templates, SECURITY.md     │
│ 10. impulse-website    --> Official documentation & site (docs.impulse...)│
└──────────────────────────────────────────────────────────────────────────┘
```

### Detailed Repository Breakdown

| # | Repository Name | Primary Role & Description | Primary Tech Stack |
| :- | :--- | :--- | :--- |
| 1 | **`impulse-graph`** | **The Flagship Engine Workspace**. Contains the C-ABI binary specification v2.3, shared test vectors, Java 25 FFM engine, C++20 kernel, Rust crate, and language bindings (Python, C#, F#, Kotlin, Scala). | C++20, Java 25, Rust, Python, C#, Kotlin, Scala |
| 2 | **`impulse-tools`** | **Developer Tools & Optimizer Suite**. Contains `impulse-opt` (RCM cache-line reordering, SIMD-PFOR/Roaring compression, section stripping/injection) and `impulse-inspect` CLI (inspect headers, validate SHA-256, dump Section 2 directories). | C++20 / Rust / CMake |
| 3 | **`impulse-benchmarks`** | **Continuous Micro & Macro Performance Suite**. Reproducible benchmark harnesses comparing Impulse Graph against Neo4j, NetworkX, PyTorch Geometric, MATPOWER, and OpenFGA. Generates latency percentiles (p50, p99) and flamegraphs. | JMH (Java), Google Benchmark (C++), Criterion (Rust), Pytest-Benchmark |
| 4 | **`impulse-platform`** | **Enterprise Cloud Infrastructure**. Contains Kafka WAL ingestion, GCS/S3 cloud snapshot sync, Kubernetes leader election, RocksDB local persistence, and Spring Boot server. | Java / Spring Boot / K8s / Kafka / RocksDB |
| 5 | **`impulse-powergrid`** | **Showcase**: Real-time power grid stability engine, IEC 61970 CIM XML compiler, 60Hz PMU stream consumer, SIMD island detector, and parallel N-1 contingency simulator. | C++ / Java / Python |
| 6 | **`impulse-gnn`** | **Showcase**: PyTorch / PyTorch Geometric zero-copy `mmap` tensor integration, native SIMD C++ neighborhood sampling (`torch.ops.impulse`), and GNN anomaly detection. | Python / PyTorch / C++ |
| 7 | **`impulse-authz`** | **Showcase**: Real-time Relationship-Based Access Control (ReBAC / Zanzibar) server, Debezium CDC ingestion from Postgres/MSSQL, and gRPC service. | Java / Spring Boot / Debezium |
| 8 | **`impulse-fintech`** | **Showcase**: Real-time Anti-Money Laundering (AML) cycle detection (A -> B -> C -> A) and synthetic identity fraud ring detection. | Java / F# / Python |
| 9 | **`.github`** | **Organization Governance**. Contains organization-level README profile, shared PR/issue templates, security policy (`SECURITY.md`), and contribution guides. | Markdown / GitHub Actions |
| 10 | **`impulse-website`** | **Documentation & Landing Page**. Interactive documentation portal (`docs.impulsegraph.io`), tutorial notebooks, and C-ABI API reference docs. | TypeScript / Starlight / Docusaurus |

---

## 6. Prioritized Repository Creation Roadmap

Repositories are ordered by architectural dependency in **4 distinct execution phases**:

```
┌──────────────────────────────────────────────────────────────────────────┐
│                   Phased Repository Creation Roadmap                     │
├──────────────────────────────────────────────────────────────────────────┤
│ PHASE 1: CORE FOUNDATION (Immediate Execution)                           │
│  1. impulse-graph     --> The flagship polyglot workspace & spec v2.3    │
│  2. .github           --> Org profile README, SECURITY.md, templates     │
│  3. impulse-tools     --> C++ offline optimizer & impulse-inspect CLI    │
│                                                                          │
│ PHASE 2: CORE TOOLING & COMMUNITY                                        │
│  4. impulse-website   --> GitHub Pages docs portal (docs.impulse...)     │
│  5. impulse-benchmarks --> JMH / Google Benchmark performance suite      │
│                                                                          │
│ PHASE 3: ENTERPRISE PLATFORM                                             │
│  6. impulse-platform  --> Spring Boot, K8s, GCS/S3, Kafka, RocksDB store  │
│                                                                          │
│ PHASE 4: INDUSTRY SHOWCASES                                              │
│  7. impulse-powergrid --> Flagship electrical grid & N-1 simulator       │
│  8. impulse-gnn       --> PyTorch / PyG zero-copy GNN acceleration       │
│  9. impulse-authz     --> Fine-grained ReBAC / Zanzibar authorization    │
│ 10. impulse-fintech   --> Anti-Money Laundering & fraud ring detection   │
└──────────────────────────────────────────────────────────────────────────┘
```

---

## 7. Public Package Manager Artifacts & Distribution Matrix

The Impulse Graph ecosystem publishes native artifacts across **7 major package registries**:

```
┌──────────────────────────────────────────────────────────────────────────┐
│              Public Package Registry Distribution Matrix                 │
├──────────────────────────────────────────────────────────────────────────┤
│ 1. Maven Central (JVM) --> io.impulse.graph:impulse-core / impulse-api   │
│ 2. Crates.io (Rust)    --> cargo install impulse-graph / impulse-tools   │
│ 3. PyPI (Python)       --> pip install impulse-graph                     │
│ 4. NuGet (C# / .NET)   --> dotnet add package ImpulseGraph                │
│ 5. npm (Node.js/Bun)   --> npm install impulse-graph                     │
│ 6. Vcpkg / Conan (C++) --> vcpkg install impulse-graph / libimpulse_graph│
│ 7. GHCR / Docker       --> docker pull ghcr.io/impulse-graph/server      │
└──────────────────────────────────────────────────────────────────────────┘
```

### Complete Artifact Publishing Table

| Registry | Ecosystem / Language | Published Artifact Name | Description & Installation Command |
| :--- | :--- | :--- | :--- |
| **Maven Central** | Java (JVM) | `io.impulse.graph:impulse-spec`<br>`io.impulse.graph:impulse-api`<br>`io.impulse.graph:impulse-core` | Core Java 25 FFM engine (0 dependencies).<br>`<dependency><groupId>io.impulse.graph</groupId><artifactId>impulse-core</artifactId></dependency>` |
| **Maven Central** | Kotlin (JVM) | `io.impulse.graph:impulse-kotlin` | Kotlin extension functions & Coroutine `Flow`. |
| **Maven Central** | Scala 3 (JVM) | `io.impulse.graph:impulse-scala_3` | Scala 3 extensions & ZIO / Cats Effect integration. |
| **Maven Central** | Spring Boot | `io.impulse.platform:impulse-platform-starter-springboot` | Spring Boot auto-configuration starter. |
| **Crates.io** | Rust | `impulse-graph` | Pure Rust crate & C-ABI FFI bindings (`cargo add impulse-graph`). |
| **Crates.io** | Rust Tools / CLI | `impulse-tools` | Developer CLI & optimizer tool suite (`cargo install impulse-tools`). |
| **PyPI** | Python | `impulse-graph` | Pre-compiled wheels for PyTorch `mmap` zero-copy tensors.<br>`pip install impulse-graph` |
| **NuGet.org** | .NET (C#) | `ImpulseGraph` | Managed .NET 9 API wrapper (`dotnet add package ImpulseGraph`). |
| **NuGet.org** | .NET Native Assets | `ImpulseGraph.Native` | Cross-platform native `.dll`/`.so`/`.dylib` runtime assets. |
| **NuGet.org** | .NET (F#) | `ImpulseGraph.FSharp` | F# computation expressions & pipe operators (`|>`). |
| **npm** | Node.js / Bun / Deno | `impulse-graph` | TypeScript definitions & N-API native bindings.<br>`npm install impulse-graph` |
| **npm** | Node.js Native Binaries | `@impulse-graph/core-linux-x64`<br>`@impulse-graph/core-darwin-arm64`<br>`@impulse-graph/core-win32-x64` | Pre-compiled platform native `.node` binaries. |
| **Vcpkg / Conan** | C / C++ | `impulse-graph` | C-ABI header `impulse_graph.h` & `libimpulse_graph.so` / `.dylib` / `.dll`.<br>`vcpkg install impulse-graph` |
| **GitHub Releases** | C / C++ Native Tarballs | `impulse-graph-v2.3.0-linux-x64.tar.gz`<br>`impulse-graph-v2.3.0-darwin-arm64.tar.gz` | Pre-compiled C-ABI shared library tarballs. |
| **GHCR / Docker Hub**| Docker Containers | `ghcr.io/impulse-graph/impulse-platform-server:latest`<br>`ghcr.io/impulse-graph/impulse-tools:latest` | Standalone Spring Boot gRPC server & `impulse-opt` optimizer CLI containers. |

---

## 8. Future Endeavor: `impulse-algo` (Macro Graph Analytics Module)

To support whole-graph algorithms (e.g. GAPBS benchmark suite analytics), a dedicated, decoupled **`impulse-algo`** module is planned as a future endeavor.

* **Architecture**: Decoupled from `impulse-core` to preserve the zero-dependency, ultra-lean kernel rule for point queries and ReBAC authorization.
* **Target Execution**: Bypasses AST overhead to run bare-metal SIMD parallel sweeps (OpenMP, Rayon, FFM VectorAPI) directly over raw off-heap `.imps` CSR offset and target buffers.
* **Planned Algorithms**:
  1. **Connected Components (CC)**: Afforest / Union-Find for grid islanding and subgraph clustering.
  2. **PageRank (PR)**: Pull-direction Jacobi vector sweeps for node ranking and GNN embeddings.
  3. **Direction-Optimizing BFS**: Parent array & level distance vector generation.
  4. **Triangle Counting (TC)**: SIMD sorted neighbor array intersections.
  5. **Single-Source Shortest Paths (SSSP)**: Delta-stepping for weighted topologies.

