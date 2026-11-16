# Implementation Plan: `vNext` Polyglot SDK Feature Parity

## Goal Description

For `v0.10.0`, **Java (`impulse-graph-java`, including Kotlin/Scala/Clojure)** and **Python (`impulse-python`)** are the first-class SDKs, alongside the core C++20 kernel (`impulse-cpp`) and `impulse-cli`.  
During concurrent REST stress testing (`Minimal Java REST Stress Test` across Java, Python, Go, Rust, C#, and Node.js), we observed that while all FFI bindings execute against the same C++ engine with `0` errors, the non-Python/non-Java bindings have uneven high-level API coverage and context-lifecycle ergonomics.

This plan establishes full **feature comparability** across **Rust, Go, C#/.NET, Node.js, and Python** for `vNext` (building on the `impulse-go` `VMContext` pooling and word-level bitset export already landed in `impulse-graph-core` PR #93).

---

## Current SDK Parity Matrix (Observed)

| Capability | Java (FFM) | Python | Go (`PR #93`) | C# / .NET | Node.js | Rust (`impulse-rust`) |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **`Snapshot` open / schema / key resolution** | Yes | Yes | Yes | Yes | Yes | Partial (`SnapshotReader` + raw `ffi`) |
| **Safe `VmContext` + `Reset()` pooling** | Yes | Yes (GIL/per-worker) | **Yes (`PR #93`)** | Manual (`VmContext` only; no `Reset` P/Invoke) | Manual (`VmContext` only) | Raw `ffi` only |
| **Low-level `QueryBuilder` (register VM)** | Yes | Yes | Yes | Yes | Yes | Missing |
| **Fluent `Traversal` (`out`, `in`, `count`, `toList`)** | Yes | Yes | Yes | Missing | Partial (numeric rel IDs) | Missing |
| **Standalone `.filter(...)` (CEL pushdown)** | Yes | Yes | Missing (`OutFiltered` only) | Missing | Missing | Missing |
| **openCypher (`CypherQuery` / `impulse_stmt_*`)** | Yes | Yes | Yes (`Cypher`) | Missing | Missing | Missing |
| **Bulk bitset `Count` / `ToSlice` (no per-node FFI loop)** | Yes | Yes | **Yes (`PR #93`)** | Missing (`bitset_get_word` P/Invoke) | Partial | Raw `ffi` only |
| **CI test enforcement** | Yes | Yes | **Yes (`PR #93`)** | Missing in `ci.yml` | **Yes (`PR #93`)** | Yes |

---

## Proposed Changes (`vNext`)

### 1. Shared C++ Kernel (`impulse-cpp`) — Lazy `VMContext` Bitset Allocation
- **Problem**: `impulse_vm_context_create` currently clamps `bitset_capacity = max(max_nodes, 65536)` and eagerly allocates `VM_MAX_BITSET_HANDLES` (`1024`) bitsets (`~8 MiB` per context), even for small graphs using 2–8 bitsets.
- **Change**:
  - Pre-allocate a small initial slab (e.g., `16`–`32` bitset handles) and allocate additional handles on demand up to `VM_MAX_BITSET_HANDLES`.
  - Avoid inflating small snapshots (`max_nodes < 65536`) unless mock/test modes explicitly request it.
- **Impact**: Drops per-context RSS from `~8 MiB` to tens of KiB across C++, Go, Rust, C#, Node.js, and Python.

---

### 2. Rust (`impulse-rust`) — Full High-Level SDK Parity
- **Problem**: `impulse-rust/src/` only exposes `SnapshotReader`, `SnapshotWriter`, and raw `unsafe` `ffi` bindings; callers must manually construct `Vec<impulse_instruction_t>`.
- **Changes**:
  - Add safe `Snapshot` wrapper (`open`, `domain_count`, `relation_count`, `relation_index`, `resolve_dense_id`, `max_node_count`) with an internal `Mutex`/`ArrayQueue`-bounded `VmContext` pool calling `impulse_vm_context_reset`.
  - Add `VmContext` (`Drop` + `reset` + word-level `bitset_count` / `bitset_to_vec`), `VmState`, and `QueryBuilder` (`input_node`, `walk_edge`, `walk_edge_filtered`, `csc_walk`, `collect_bitset`, `compile`).
  - Add fluent `Traversal` (`out`, `in_`, `filter`, `count`, `to_vec`) and `CypherQuery` wrapping `impulse_stmt_prepare` / `impulse_stmt_execute`.
  - Update `impulse-rust/examples/02_social_traversal.rs` to use the high-level `Traversal` API.

---

### 3. C# / .NET (`impulse-dotnet`) — Fluent `Traversal`, Pooling & `CypherQuery`
- **Problem**: `impulse-dotnet` has `Snapshot`, `QueryBuilder`, and `VmContext`, but lacks `Reset()`, word-level bitset P/Invoke, `Snapshot`-level pooling, fluent `Traversal`, `CypherQuery`, and CI coverage.
- **Changes**:
  - Add P/Invoke declarations in `Native/` for `impulse_vm_context_reset`, `impulse_vm_context_get_bitset_word_count`, `impulse_vm_context_get_bitset_words`, and `impulse_stmt_*`.
  - Add `ConcurrentBag<VmContext>` pooling on `Snapshot` (`AcquireVmContext` / `ReleaseVmContext` calling `Reset()`).
  - Add `Traversal` (`Out`, `In`, `Filter`, `Count`, `ToList`) and `CypherQuery` in `impulse-dotnet/Vm/`.
  - Add a `dotnet test` job to `.github/workflows/ci.yml` under `test-ffi-bindings`.

---

### 4. Node.js (`impulse-node`) — Named Relations, `Filter`, Pooling & `Cypher`
- **Problem**: `Traversal` lacks string relation-name lookup and standalone `filter()`, `VmContext` is not pooled on `Snapshot`, and `Cypher` (`impulse_stmt_*`) is not exposed.
- **Changes**:
  - Expose `relationIndex(name)` and `reset()`/`bitsetCount()`/`bitsetWords()` in `impulse-node/src/impulse_node.cpp`.
  - Support string relation names and `.filter(celExpr)` in `Traversal`, plus `Snapshot`-level `VmContext` pooling and `CypherQuery`.

---

### 5. Go (`impulse-go`) — Standalone `.Filter(...)` Parity
- **Problem**: `impulse-go` has `OutFiltered` / `InFiltered`, but lacks a standalone `.Filter(celExpr)` node-attribute step on `Traversal`.
- **Changes**:
  - Add `(*Traversal).Filter(celExpr string) *Traversal` compiling node-attribute filter predicates into `impOps` bytecode (matching Python/Java).
  - Add unit tests in `impulse-go/vm_test.go` covering `Filter` + `Cypher` + `Count`/`ToSlice` parity.

---

### 6. Python (`impulse-python`) — Minor UX Polish
- **Change**: Auto-populate `Snapshot` relation-name catalog from the `.imps` relation directory on `open`, so callers do not need to supply a manual `catalog` mapping when relation names are already embedded in the snapshot.

---

## Verification Plan (`vNext`)

1. **Per-SDK Unit & Integration Suites** (all enforced in `.github/workflows/ci.yml` with zero `|| true`):
   - `ctest` (C++ Release + ASan/UBSan + TSan)
   - `cargo test --all-targets` (`impulse-rust` high-level `Traversal`, `QueryBuilder`, `CypherQuery`)
   - `go test -race ./...` (`impulse-go` concurrent `VMContext` pool + `Filter`)
   - `dotnet test` (`impulse-dotnet` `Traversal`, `CypherQuery`, `VmContext` pool)
   - `npm test` (`impulse-node` named `Traversal`, `Filter`, `CypherQuery`)
   - `pytest` (`impulse-python` auto-catalog)
2. **Cross-Language Concurrency & RSS Regression Gate**:
   - Re-run the `256 x 200` `/fluent` REST stress suite across Java, Python, Go, Rust, C#, and Node.js in `impulse-verification`, verifying `0` failures and `< 200 MiB` RSS across all runtimes.
