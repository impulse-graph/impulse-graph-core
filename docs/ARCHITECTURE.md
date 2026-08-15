# Impulse Graph Engine — Architecture Guide

This document details the internal architecture of the **Impulse Graph Engine** (`impulse-graph-core`), the design principles behind the register-based **ImpulseVM**, the **ImpScheme** Intermediate Representation (IR) compiler bus, and the lock-free execution model.

---

## 1. Core Philosophy: The SQLite & Arrow for Graphs

Traditional graph databases (e.g. Neo4j, JanusGraph, TigerGraph) run as heavy stateful client-server daemons requiring physical memory allocation for every vertex and edge, garbage-collected graph structures, and high socket serialization overhead.

Impulse Graph decouples analytical graph compute from storage:
* **Single-File Snapshot (`.imps`)**: All adjacency structures (CSR/CSC), node/edge attributes (Structure of Arrays), string tables, and secondary indexes reside in an immutable, 128-byte hardware-aligned binary file.
* **Embedded Zero-Copy Mmap**: Applications map the `.imps` file directly into their address space with zero deserialization and zero GC allocation.
* **Serverless In-Process Execution**: Operates as an embedded library (like SQLite) inside Python, Go, Node.js, C#, Rust, and C++ processes.

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
|  |  |            Frontend DSLs (ImpK, ImpLog, Cypher)           |  |  |
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

---

## 2. The ImpulseVM Execution Engine

The **ImpulseVM** is a register-based virtual machine specifically engineered for graph traversal and linear algebra operations over off-heap memory-mapped data.

### 2.1 Register File Architecture
ImpulseVM provides **64 virtual registers** (`R0`..`R63`) partitioned into polymorphic execution types:
* **Node ID Scalar / Vector Registers**: Holds candidate node indices (`uint64_t`).
* **Bitset Registers**: Compact bitsets tracking reachable frontiers across billions of nodes ($N/64$ words) with hardware POPCNT and AVX-512 / ARM Neon bitwise acceleration.
* **Numeric Float/Double Vectors**: Holds property embeddings, PageRank scores, and feature tensors (`float*`, `double*`).
* **String Pool Registers**: Offsets into the shared string table for zero-copy string attributes.

### 2.2 Bytecode Instruction Set Architecture (`impOps`)
Opcodes (`0x00`..`0x72`) map directly to SIMD vectorized primitives:
* `OP_CSR_WALK` (`0x01`): Forward traversal along Compressed Sparse Row edge lists.
* `OP_CSC_WALK` (`0x02`): Reverse traversal along Compressed Sparse Column target indices.
* `OP_CSR_WALK_2HOP` (`0x03`): Fused 2-hop traversal without intermediate bitset materialization.
* `OP_MXV` (`0x30`): Semiring Matrix-Vector multiplication (GraphBLAS).
* `OP_EWISE_ADD` (`0x34`) / `OP_EWISE_MULT` (`0x35`): Element-wise vector arithmetic.
* `OP_CC_AFFOREST` (`0x40`): SIMD-vectorized Afforest connected components.
* `OP_DEGREE_NORM` (`0x42`): Symmetric degree normalization ($D^{-1/2} A D^{-1/2}$).

### 2.3 Instruction Dispatch Modes
ImpulseVM implements dual execution paths:
1. **Computed Goto (Direct Threading)**: On supported compilers (GCC, Clang, Apple Clang), opcodes jump directly through a static label jump table (`static const void* const dispatch_table[]`), eliminating switch branch misprediction overhead.
2. **Standard Switch-Case Dispatch**: Portable fallback for MSVC and constrained environments.

---

## 3. The ImpScheme IR & Zero-Cost Compilation Bus

To achieve seamless multi-frontend compatibility (ImpLog Datalog, ImpK GraphBLAS, openCypher, CEL filters), all frontend parsers compile down to a universal intermediate representation: **ImpScheme** (`.impscm`).

### 3.1 Homoiconic S-Expression AST
ImpScheme represents graph execution plans as homoiconic S-expressions:
```scheme
;; 4-hop Drug Repurposing Pipeline with CEL Filter
(pipeline
  (input-node 14726)
  (walk "DaG")
  (walk "GpPW")
  (walk-csc "GpPW")
  (walk-csc-filtered "CbG" (cel "<=" edge.affinity ($param "maxAffinity")))
  (collect-bitset R0))
```

### 3.2 On-The-Fly Compilation (Zero Query Cache by Default)
A key architectural discovery in Impulse Graph is that **the C++ AST optimizer and bytecode emitter compile in single-digit microseconds (< 3 µs)**. 

Because AST compilation is faster than thread-safe concurrent LRU hash table lookups and lock synchronization, **Impulse Graph compiles queries on-the-fly for every execution by default**, eliminating query cache memory leaks, thread contention, and cache invalidation bugs.

### 3.3 7-Stage Compiler Optimization Pipeline
1. **Constant Folding & Dead Code Elimination**: Resolves compile-time constants and prunes unreachable traversal branches.
2. **Direction Selection**: Evaluates relation density and degree distributions to choose between top-down CSR push and bottom-up CSC pull traversals.
3. **Kernel Fusion (2-Hop Fusion)**: Fuses consecutive `OP_CSR_WALK` steps into `OP_CSR_WALK_2HOP`, keeping active node frontiers in CPU L1/L2 data caches.
4. **Register Cache Ping-Ponging**: Alternates bitset allocations between `R0` and `R1` to reuse pre-allocated cache lines without OS memory allocation.
5. **Predicate Pushdown & SIMD Filtering**: Translates CEL expressions directly into vectorized range masks before relation materialization.
6. **Seed Inlining**: Encodes starting node IDs directly into the instruction immediate payload.
7. **Early Exit Optimization**: Short-circuits traversals as soon as empty frontiers are encountered.

---

## 4. Hardware Acceleration & Memory Hierarchy

* **Google Highway Portable SIMD**: Dynamically targets AVX-512, AVX2, ARM Neon, or RISC-V Vector instructions based on runtime CPUID detection.
* **128-Byte Section Alignment**: Every section in `.imps` is 128-byte aligned, enabling hardware vector units, GPU Direct Storage (`cuFile`), and DMA engines to read data directly.
* **Lock-Free Read-Only Traversal**: Traversal kernels operate exclusively on read-only memory maps without mutexes, spinlocks, or atomic CAS contention in query hot paths.
