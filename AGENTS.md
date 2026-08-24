# AGENTS.md — Impulse Graph Ecosystem AI Agent Guide

This document is the primary architectural entrypoint and context guide for AI Coding Assistants (AGY, Antigravity, Claude, Copilot, Cursor) working across the **Impulse Graph Engine** multi-repository ecosystem located in `~/impulse/`.

---

## 1. Executive Summary, Naming Policy & Ecosystem Story

* **Official Project Name**: **Impulse Graph Engine** (`impulse-graph`).
* **Organization GitHub Namespace**: `impulse-graph` (`github.com/impulse-graph`).
* **Naming Directive**: Legacy names (`abac-engine`, `impulse-engine`) are retired. Always use **Impulse Graph Engine** (`impulse-graph`) in code, documentation, comments, and public artifacts.

### Core Value Proposition & The Apache Arrow for Graphs
Impulse Graph is an ultra-high-performance, zero-copy, C-ABI binary snapshot graph engine designed for sub-millisecond cold start, sub-microsecond vector traversals, and multi-terabyte (TB+) scale graph analytics.

It fills a critical missing gap in the open-source software ecosystem by acting as the **"Apache Arrow / Parquet equivalent for Graph Analytics"**. It decouples static graph analytics from heavy database servers by pairing an **immutable binary snapshot format (`.imps`)** with a **register-based Virtual Machine (`ImpulseVM`)** that executes SIMD-vectorized traversals directly off-heap without physical data loading or GC pauses.

### Standardized Ecosystem Naming Lexicon

| Category | Canonical Term | Usage & Guidelines | Legacy Terms (RETIRED) |
| :--- | :--- | :--- | :--- |
| **Official Project Name** | **Impulse Graph Engine** | Use in titles, documentation, READMEs, and organization overview (`impulse-graph`). | `abac-engine`, `impulse-engine` |
| **Binary Snapshot Format** | **Impulse Binary Snapshot Format** (`.imps`) | Refers to the physical C-ABI binary file format specification (Version 0.9.0). File extension is strictly `.imps`. | `Snapshot File v2.3`, `v2.4 format` |
| **Snapshot Data Structure** | **Impulse Graph Snapshot** | The read-only, memory-mapped in-memory representation of an immutable `.imps` file (`ImpulseGraphSnapshot`). | `Impulse Snapshot`, `Snapshot File` |
| **Execution Core / VM** | **Impulse VM** (`ImpulseVM`) | The bytecode Virtual Machine engine executing `impOps` bytecode instructions. | `Impulse VM Engine`, `Impulse Core Engine` |
| **Bytecode Instruction Set** | **`impOps`** | The formal instruction set architecture and binary opcode specifications (opcodes `0x01`..`0x6A`). | `impulse opcodes`, `vm instructions` |
| **Array & Vector DSL** | **ImpK** (`.impk`) | Primary user frontend DSL for GraphBLAS matrix math, PageRank, connected components, and SIMD vector operations. | `Impulse Vector DSL` |
| **Datalog Logic DSL** | **ImpLog** (`.implog`) | Primary user frontend DSL for declarative Datalog logic rules, ReBAC authorizations, and transitive reachability. | `Impulse Datalog` |
| **Compiler IR & Macro DSL** | **ImpScheme** (`.impscm`) | Universal Compiler Intermediate Representation (IR) & Low-Level Macro Extension Target. Homoiconic S-Expression AST compiler bus between frontends (`ImpK`, `ImpLog`) and `impOps` emitter. | `ImpScm`, `Impulse IR` |
| **Assembly Text Format** | **ImpAsm** (`.impas`) | Human-readable text assembly format representing `ImpulseVM` registers (`R0`..`R63`). | `Impulse Assembly` |

---

## 2. Organization Repositories Sitemap & Navigation Guide

The ecosystem is partitioned into specialized repositories located at `~/impulse/<repo-name>`:

| Repository Name | Role & Description | Primary Tech Stack | Primary Directory |
| :--- | :--- | :--- | :--- |
| **`impulse-graph-core`** | **Core Engine Kernel & FFI Bindings**. Contains the C++20 zero-copy memory-mapped kernel, Rust core engine crate (`impulse-rust`), and multi-language FFI bindings (Python, C#, Go, Node.js). | C++20, Rust, Python, C#, Go, Node.js | `~/impulse/impulse-graph-core/` |
| **`impulse-graph-spec`** | **Binary Snapshot Specification & Test Vectors**. Canonical C-ABI Binary Snapshot v0.9.0 normative format specification (`docs/FORMAT_SPECIFICATION.md`) and cross-language compliance test vectors (`tc01`..`tc36`). | Markdown, Python | `~/impulse/impulse-graph-spec/` |
| **`impulse-graph-java`** | **Java 25 FFM Core Engine**. Standalone Java 25 Foreign Function & Memory (FFM) off-heap snapshot engine & Impulse VM interpreter (`impulse-api`, `impulse-core`, `impulse-spec`). | Java 25 (FFM/Vector API), Maven | `~/impulse/impulse-graph-java/` |
| **`impulse-graph-tooling`** | **Developer Utilities & ImpVM Suite**. Contains `impulse assemble`, `impulse disassemble`, `impulse run`, `impulse compile`, `impulse inspect`, and `impulse opt`. | C++20, Rust, Go, CMake | `~/impulse/impulse-graph-tooling/` |
| **`impulse-benchmarks`** | **Performance & Macro Benchmark Suite**. Reproducible benchmark harnesses comparing Impulse Graph against Neo4j, NetworkX, PyTorch Geometric, MATPOWER, and OpenFGA. | Java (JMH), C++ (Google Bench), Python | `~/impulse/impulse-benchmarks/` |
| **`impulse-website`** | **Documentation Portal & Landing Page**. Official documentation site (`docs.impulsegraph.io`) built with Material for MkDocs. | Markdown, MkDocs Material, YAML | `~/impulse/impulse-website/` |
| **`impulse-platform`** | **Enterprise Cloud Infrastructure**. Standalone Spring Boot gRPC server, Kafka WAL ingestion, GCS/S3 cloud snapshot sync, Kubernetes leader election, and RocksDB local persistence. | Java 25, Spring Boot, K8s, Kafka | `~/impulse/impulse-platform/` |
| **`impulse-powergrid`** | **Showcase**: Real-time electrical power grid stability engine, IEC 61970 CIM XML compiler, 60Hz PMU stream consumer, SIMD island detector, and 10,000 parallel N-1 contingency simulator. | C++20, Java, Python | `~/impulse/impulse-powergrid/` |
| **`impulse-gnn`** | **Showcase**: Python PyTorch / PyG zero-copy `mmap` tensor integration and native SIMD C++ neighborhood sampler (`torch.ops.impulse.sample_neighbors`). | Python, PyTorch, C++20 | `~/impulse/impulse-gnn/` |
| **`impulse-authz`** | **Showcase**: Sub-microsecond Relationship-Based Access Control (ReBAC / Zanzibar) fine-grained authorization server with live Debezium CDC sync. | Java 25, gRPC, Debezium | `~/impulse/impulse-authz/` |
| **`.github`** | **Organization Governance**. Organization README profile, issue/PR templates, `SECURITY.md`, and shared workflow actions. | Markdown, GitHub Actions | `~/impulse/.github/` |

---

## 3. Core Engine Architectural Conventions

When modifying or implementing code across any repository in `~/impulse/`, enforce the following architectural rules:

### 3.1 C-ABI Binary Snapshot Specification v0.9.0
* **Header Baseline**: Fixed 4KB Page 0 with 64-byte active baseline, magic `0x494D5053` (`IMPS`), 16-bit version `0x0009` (`9`).
* **Section 2 Shared String Table**: Single global string pool for node/edge relation names, domain catalog, and string attributes.
* **Hardware Alignment**: 128-byte alignment across all offset sections for AVX-512 vector units, GPU warp coalescing (NVIDIA GPUDirect Storage `cuFile`), and TPU vector tiles.
* **Single-Pass Cloud S3 Streaming Write**: `SnapshotWriter` streams binary files direct to cloud object storage (Amazon S3, Google Cloud Storage) in $O(\text{chunk})$ physical RAM footprint without requiring random file seeks.

### 3.2 Ultra-Lean Core Kernel Rule (Zero Dependencies)
* **Rule**: `impulse-core` in Java (`impulse-graph-java`), C++ (`impulse-graph-core/impulse-cpp`), and Rust (`impulse-graph-core/impulse-rust`) MUST maintain **zero third-party runtime dependencies**.
* **Decoupling**: RocksDB persistence, Kafka streaming, and Spring Boot server frameworks belong exclusively in `impulse-platform`.

### 3.3 Storage & Memory Philosophy
* **Scale Assumption**: NVMe disk space and object storage are cheap and abundant; physical RAM is constrained.
* **Direct-to-Disk Streaming**: `SnapshotBuilder` and `SnapshotWriter` stream binary snapshots direct-to-disk or cloud storage in $O(\text{chunk})$ physical memory footprint, preventing Out-Of-Memory (OOM) failures on multi-hundred-gigabyte snapshots.

### 3.4 Read-Only (RO) Query Focus & Impulse VM (`ImpulseVM` & `impOps`)
* **Read-Only Lock-Free Traversal**: Core query kernels focus 100% of execution engine optimizations on Read-Only (RO) vector queries over immutable zero-copy `.imps` snapshots. Real-time updates and CDC ingestion are handled out-of-band by streaming compilers writing new `.imps` snapshots.
* **Impulse VM Architecture**: Register-based opcode Virtual Machine (`ImpulseVM`) executing `impOps` bytecode instructions (`OP_CSR_WALK`, `OP_CSC_WALK`, `OP_MXV`, `OP_CC_AFFOREST`, `OP_COLLECT_BITSET`) with OpenMP intra-instruction parallelization and Java 25 `MethodHandle` JIT combinators.

### 3.5 Distributables & Standalone Packaging Guidelines
* **Static Core Engine Linking**: All `impulse-graph-tooling` executables (`impulse-opt`, `impulse-compile`, `impulse-inspect`, `impulse-run`) MUST statically embed the core Impulse Graph C-ABI kernel (`libimpulse_graph_static.a` / `.lib`) when building release distributables (`.deb`, `.rpm`, `.msi`, `.pkg`, `tar.gz`).
* **Linux C Runtime (`glibc` vs `musl`)**: Release packages (`.deb`, `.rpm`) target `glibc >= 2.28` for standard Linux distros. Portable container tarballs target `musl-libc` fully static (`-static`) for zero-dependency execution in `scratch` / Alpine containers.
* **macOS Distribution (`.pkg` / Homebrew)**: Embeds static engine kernel; links system `libc++.dylib` and `libSystem.B.dylib` (guaranteed on macOS 11.0+).
* **Windows Packaging (`.msi` / `.zip`)**: Built with MSVC `/MT` (Static C++ Runtime `LIBCMT.lib`) to eliminate Visual C++ Redistributable runtime installer requirements.

### 3.6 Git Repository Data Hygiene Policy
* **Zero Heavy Data Blobs in Git**: NEVER commit heavy dataset edge lists (`*.tsv`, `*.csv`), compressed archives (`*.zst`), or compiled binary snapshots (`*.bin`, `*.imps`) to Git repositories.
* **`datasets/` Standard Location**: Store all local benchmark data, downloads, and sample outputs in `datasets/` subdirectories, which are strictly ignored by `.gitignore`.

### 3.7 Empirical Benchmark Verification Policy
* **Zero Estimated / Unverified Benchmark Claims**: ALL published benchmark metrics, execution latencies, throughput (QPS/MTEPS), memory footprints, and JVM/C++ comparisons MUST be derived exclusively from **actual empirical runtime execution logs** gathered on physical hardware. NEVER publish estimated, extrapolated, or projected benchmark figures in website documentation, READMEs, or project artifacts.

### 3.8 100% GitHub CI Pipeline Build Mandate & Cryptographic Attestation (SLSA Level 3)
* **100% CI Build Mandate**: All public release binaries, container images, and package artifacts MUST be built strictly inside GitHub Actions CI/CD pipelines — no local builds permitted.
* **Build Provenance Attestation**: Release workflows generate GitHub Artifact Attestations (`actions/attest-build-provenance`) linking every published binary directly to its workflow execution.
* **Code Signing & Notarization**: macOS `.dylib` / `.pkg` release binaries are signed with Apple Developer ID (`codesign`) and notarized via `xcrun notarytool` inside GitHub macOS builders. Windows executables/DLLs are signed with Authenticode (`signtool.exe`) inside GitHub Windows builders.

### 3.9 Test-First Opcode Specification & Coverage Mandate
* **Test-Driven Bytecode Specification**: When introducing new `impOps` opcodes or ISA extensions, test vectors in `impulse-graph-spec/test-vectors/vm-impas/` MUST be defined first (or alongside the opcode specification) before runtime implementation.
* **Positive & Negative Test Vector Mandate**: Every defined opcode MUST have both **Positive Test Vectors** (verifying expected status `IMPULSE_VM_OK`, correct register outputs, and status flags) and **Negative Test Vectors** (verifying graceful error status codes, bounds checking, `OP_THROW`, or `OP_ASSERT` failure traps).
* **Opcode Test Coverage & Multi-File Threshold**: The `run_vm_asm_suite.py` test harness enforces **100% Opcode Test Coverage**. Every defined opcode (`0x00`..`0x72`) MUST appear in **at least 2 distinct test files** in `impulse-graph-spec/test-vectors/vm-impas/`. Build/test pipelines will reject any PR if an opcode has 0 test files or fails the multi-file appearance threshold.

### 3.10 Multi-Frontend Compiler Suite & DSL Architectural Positioning
* **ImpK (`.impk`)**: Primary User Frontend DSL for GraphBLAS matrix mathematics, PageRank, connected components, and SIMD vector operations.
* **ImpLog (`.implog`)**: Primary User Frontend DSL for declarative Datalog logic rules, Relationship-Based Access Control (ReBAC / Zanzibar), and transitive reachability.
* **ImpScheme (`.impscm`)**: Universal Compiler Intermediate Representation (IR) & Low-Level Macro Extension Target. Homoiconic S-Expression AST compiler bus between frontends (`ImpK`, `ImpLog`) and the bytecode emitter (`ImpAsm` / `impOps`). Can be authored directly for advanced macro metaprogramming or AST pass debugging.
* **ImpAsm (`.impas`)**: Canonical human-readable assembly text format representing `ImpulseVM` registers (`R0`..`R63`).
* **impOps (`.impb`)**: Virtual Machine Bytecode Instruction Set Architecture (ISA opcodes `0x01`..`0x6A`). Documented as part of the core specification.

### 3.11 Git Branching, Remote Push Protocol & Sandbox Rules
* **Dedicated Feature Branching Mandate**: All development work MUST be performed strictly on dedicated Git feature branches (`git checkout -b feat/<feature-name>`). Direct commits to `main` are prohibited except for minor documentation fixes.
* **Confirmation Before Push**: AI agents MUST explicitly request and receive user confirmation before pushing any new or updated branches to the remote repository.
* **Sandbox Bypass Requirement for Remote Git Operations**: Shell commands attempting remote network interactions (`git push`, `git fetch`, `git clone`, `cmake` FetchContent) will fail in sandboxed execution environments (`Operation not permitted`). AI agents MUST set `BypassSandbox: true` when invoking `run_command` for remote Git network operations.
* **Pull Request Link Generation**: After executing `git push origin <branch-name>`, AI agents MUST capture and report the generated GitHub Pull Request URL (e.g. `https://github.com/impulse-graph/<repo>/pull/new/<branch-name>`) to the user.

### 3.12 Technical Communication Tone & Prohibition of Generic Performance Fluff
* **Prohibition on Performance Buzzwords**: Do NOT excessively mention Impulse's SIMD fast execution, memory-mapped zero-copy snapshots, off-heap lack of GC pauses, or other marketing accolades. The user is the author/architect of the software and already intimately understands its performance features.
* **Direct Technical Explanations**: Assume user questions are seeking precise technical explanations for specific points of inquiry rather than generic praise.
  - **Good**: *"The compiler performed partition elimination because parameter `$p` had a constant value of `0`."*
  - **Bad**: *"The impulse engine is capable of performing parallel SIMD vector comparisons over zero-allocation memory-mapped files at more than 3500 MTEPS!"*
* **Concise Reporting**: Keep all test, benchmark, and execution summaries concise, dense, and focused strictly on the metrics, root causes, and diffs.

### 3.13 Benchmark Discrepancy & Cross-Language Anomaly Investigation Policy
* **Performance Parity Expectation**: In VM microbenchmarks and traversal workloads (e.g. C++ vs Java 25), the warmed-up Java engine is expected to execute within **$\le \text{2x}$ of C++**, with the delta primarily attributable to Java pointer range and array bounds checking.
* **Mandatory Anomaly Investigation**:
  - **Unacceptable Java Gap ($> \text{5x}$)**: Any benchmark where the warmed-up Java engine is $> \text{5x}$ slower than C++ is considered an unacceptable performance regression that MUST be investigated, profiled, and resolved (e.g. off-heap memory accessor overhead, JIT deopt, escape analysis failure).
  - **Unexpected C++ Inefficiencies**: Any non-intuitive result where C++ is unexpectedly slower than Java or exhibits anomalous slowdowns MUST be investigated immediately for pipeline stalls, missing compiler optimizations, cache line misses, or branch mispredictions.

### 3.14 Universal Node Architecture: Per-Domain Dense ID Independence & Integer Widths
* **Strict Per-Domain Dense ID Independence ($0 \dots N_d-1$)**: In Impulse Graph Engine, **there is NO global flattened or synthetic unified node ID space**. Every Node Domain (e.g. `User`, `WineInventory`, `Cheese`, `Gene`, `Disease`) possesses its own completely independent 0-indexed dense integer space $0 \dots N_d-1$. Dense node ID `1` in domain `User` is fundamentally distinct and unrelated to dense node ID `1` in `WineInventory` or `Cheese`.
* **Configurable Primitive Integer Widths per Domain**: Each Node Domain can independently configure its physical integer representation width in the binary snapshot layout based on domain cardinality:
  - `uint16_t` (up to 65,536 nodes) — optimal for compact catalogs and small entity domains.
  - `uint32_t` (up to 4,294,967,296 nodes) — standard default for high-scale enterprise graphs.
  - `uint64_t` — for multi-billion/trillion-node hyperscale domains.
* **Domain-Bound Traversal & Relation Transitions**:
  - Traversal entrypoints MUST bind an explicit domain context in multi-domain graphs (`snap.domain("User").traverse([1, 2, 3, 4])`). Passing a raw integer or integer array without a domain context on a multi-domain snapshot is an invalid/ambiguous query error.
  - Every Relation Descriptor in the binary snapshot explicitly encodes `SrcDomainID` and `TgtDomainID`. Traversing an edge relation (`(User)-[:PURCHASED]->(WineInventory)`) explicitly maps source dense IDs in `SrcDomainID` to target dense IDs in `TgtDomainID`.
  - Attribute evaluation (`.filter("age > 5")`) resolves strictly against the attribute table of the *currently active domain* in the traversal pipeline.

### 3.15 Universal Traversal Model: Kleisli Frontier Propagation & Monoidic Reduction
All graph traversals across all language SDKs and VM execution engines follow the **Kleisli Frontier Propagation Model**:
$$\text{Pipeline} = \text{Anchor}(D_0, F_0, S_0) \gg= T_1 \gg= T_2 \dots \gg= T_k \gg= \text{Collect}()$$
* **1. Anchor Context & Initial Frontier $\langle D_0, \text{Frontier}_0, \text{State}_0 \rangle$**:
  Every traversal is anchored to an initial Domain $D_0$ and a seed frontier (all nodes in domain, single node ID, sparse ID array, dense bitset, or Roaring bitmap) with optional attached state vector (weights, distances, probabilities).
* **2. In-Domain Endomorphisms (Filter & State Projection)**:
  Operations within the current domain ($\langle D, S \rangle \to \langle D, S' \rangle$) filter nodes (`.filter("age > 21")`) or project/mutate attached node state (`.project("state.dog_age = age / 7.0")`) in a single vectorized SIMD pass over the active frontier.
* **3. Cross-Domain Traversal & Monoidic Reduction ($\langle D, S \rangle \xrightarrow{R} \langle D', S' \rangle$)**:
  When stepping across relation $R: D \to D'$, multiple incoming paths converging on the same target node $v \in D'$ are reduced via a **Monoid $(\mathcal{S}, \oplus, \mathbf{0})$**:
  - **Boolean Reachability / Set Union**: $\oplus = \lor$ (Bitwise OR over BitSets).
  - **Shortest Paths / BFS**: $\oplus = \min$, $\otimes = +$ (Min-Plus Semiring).
  - **PageRank / Markov Walks**: $\oplus = +$, $\otimes = \times$ (Arithmetic Semiring).
  - **Rank / Confidence**: $\oplus = \max$.
* **4. Kleisli Chaining & Fixed-Point Loops**:
  Because each traversal step outputs a valid $\langle D', S' \rangle$, steps chain indefinitely and fixed-point loops (`repeatUntilStable`) execute BFS, ReBAC reachability, and Connected Components with zero heap allocations.
* **5. Terminal Materialization (`collect()`)**:
  Projects the final target domain's active nodes and state into Arrow columnar arrays, BitSets, or scalar aggregates.

---

### 3.16 Prohibition of Imperative Point-Lookup Workarounds & Feature Honesty Mandate
* **Strict Engine-Native Execution Mandate**: Graph traversals, projections, attribute calculations, and filters MUST be evaluated strictly through the native Impulse Graph engine kernels (`.project(...)`, `.filter(...)`, `.out(...)`, `ImpulseStatement`, `ImpulseVM`). AI agents MUST NEVER construct client-side imperative `for` loops that treat the graph merely as an ID lookup table to perform manual point lookups or calculations in user code.
* **Failure Classification of Imperative Substitutes**: Falling back to manual imperative client-side loops or iterative buffer lookups is considered an architectural failure.
* **Honest Engine Capability Reporting**: If a requested traversal feature, state projection expression, or query capability is not yet implemented end-to-end in the current engine/VM runtime, AI agents MUST directly, explicitly, and honestly state that **it is not currently possible given engine functionality**, rather than fabricating imperative workarounds as a substitute.

---

## 4. Package Registry Distribution Matrix

Artifacts are published across **7 public registries**:

1. **Maven Central (JVM)**: `org.impulsegraph:impulse-core`, `impulse-api`, `impulse-spec`, `impulse-kotlin`, `impulse-scala_3`.
2. **Crates.io (Rust)**: `impulse-graph` (engine crate) and `impulse-tools` (CLI optimizer & tooling).
3. **PyPI (Python)**: `impulse-graph` (pre-compiled wheels with PyTorch `mmap` tensor support).
4. **NuGet.org (.NET / C#)**: `ImpulseGraph` (managed API), `ImpulseGraph.Native` (cross-platform native binaries), `ImpulseGraph.FSharp`.
5. **npm (Node.js / Bun)**: `impulse-graph` (TypeScript definitions & N-API native bindings).
6. **Vcpkg / Conan (C/C++)**: `impulse-graph` (C-ABI header `impulse_graph.h` & `libimpulse_graph.so`/`.dylib`/`.dll`).
7. **GHCR / Docker**: `ghcr.org.impulsegraph/impulse-platform-server:latest` & `impulse-tools:latest`.

---

## 5. Development & Verification Commands

### C++ Kernel (`~/impulse/impulse-graph-core/impulse-cpp/`)
```bash
cmake -B build && cmake --build build
```

### Rust Crate (`~/impulse/impulse-graph-core/impulse-rust/`)
```bash
cargo test
```

### Java Engine (`~/impulse/impulse-graph-java/`)
```bash
mvn clean test
```

### Specification & Vectors (`~/impulse/impulse-graph-spec/`)
```bash
cat docs/FORMAT_SPECIFICATION.md
```

### Developer Tools (`~/impulse/impulse-graph-tooling/`)
```bash
cmake -B build && cmake --build build
```

### Documentation Site (`~/impulse/impulse-website/`)
```bash
mkdocs serve
```
