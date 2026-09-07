# Fuzzing and Property-Based Testing Bug Register

This document tracks all bugs, crashes, invariants violations, and edge cases discovered by the fuzzing harnesses and property-based testing suites in `impulse-graph-core`.

---

## Bug Index

| Bug ID | Component | Severity | Discovered By | Status |
| :--- | :--- | :--- | :--- | :--- |
| `CORE-FUZZ-001` | `ImpulseVM` (`impulse_vm.cpp`) | **High** (SIGSEGV / Crash) | `fuzz_vm_bytecode` | ✅ Resolved |
| `CORE-FUZZ-002` | `SnapshotWriter` (`impulse_graph.cpp`) | **Medium** (Invalid Rejection) | `test_property_snapshot` | ✅ Resolved |
| `CORE-FUZZ-003` | `CEL AstOptimizer` (`impulse_cel.h`) | **High** (SIGSEGV / Crash) | `fuzz_cel_parser` | ✅ Resolved |
| `CORE-FUZZ-004` | `ImpulseVM` (`impulse_vm.cpp`) | **High** (SIGSEGV / Crash) | `fuzz_vm_bytecode` | ✅ Resolved |
| `CORE-FUZZ-005` | `ImpulseVM` (`impulse_vm.cpp`) | **High** (SIGSEGV / Crash) | `fuzz_vm_bytecode` | ✅ Resolved |
| `CORE-FUZZ-006` | `ImpulseVM` (`impulse_vm.cpp`) | **High** (SIGSEGV / Crash) | `fuzz_vm_bytecode` | ✅ Resolved |
| `CORE-FUZZ-007` | `ImpulseVM` (`impulse_vm.cpp`) | **High** (SIGSEGV / Crash) | `fuzz_vm_bytecode` | ✅ Resolved |
| `CORE-FUZZ-008` | `Traversal` (`traversal.rs`) | **Medium** (Silent Fallback to Rel 0) | `mutation_test.rs` | ✅ Resolved |

---

### Bug `CORE-FUZZ-007`: Out-of-Bounds / Unallocated BitSet Handle Accesses in Walk, Gather, and Diff Opcodes
* **Date**: 2026-09-07
* **Component**: `impulse-cpp/src/impulse_vm.cpp` (`impOps` Execution Engine)
* **Discovered By**: `fuzz_vm_bytecode` (Deterministic Run, Iteration 2468, Seed 987654321)
* **Crash Signature**:
  ```text
  stop reason = EXC_BAD_ACCESS (code=1, address=0x0)
  frame #0: impulse_vm_execute
  frame #1: LLVMFuzzerTestOneInput
  ```
* **Root Cause**:
  Multiple graph walk, gather, and diff opcodes performed unchecked accesses on bitsets:
  1. `OP_GATHER_NODE_ATTR` (0x81): `int h_front = static_cast<int>(vm_state->registers[frontier_reg]); const auto& bs = vm_state->query_context->bitsets[h_front];` lacked `VALIDATE_BITSET_HANDLE(h_front)`.
  2. `OP_COO_WALK` (0x86) and `OP_COO_WALK_DIRECT_STORE` (0x8D): `int h_src = static_cast<int>(vm_state->registers[src]); const auto& bs_src = vm_state->query_context->bitsets[h_src];` lacked `VALIDATE_BITSET_HANDLE(h_src)`.
  3. `OP_COO_WALK_FILTERED` (0x8B): When `src_is_bitset` or `flt_is_bitset` was true, `static_cast<int>(scalar_src)` and `static_cast<int>(scalar_flt)` indexed `vm_state->query_context->bitsets` without `VALIDATE_BITSET_HANDLE`.
  4. `OP_COO_WALK_REDUCE` (0x8C): `int h_src = static_cast<int>(vm_state->registers[src]); const auto& bs_src = vm_state->query_context->bitsets[h_src];` lacked `VALIDATE_BITSET_HANDLE(h_src)`.
  5. `OP_CSR_WALK_DIRECT_STORE` (0x84), `OP_CSR_WALK_DENSE_STREAM` (0x85), `OP_CSC_WALK_DIRECT_STORE` (0x87), `OP_DENSE_WALK` (0x8E), and `OP_FIXPOINT_KLEENE_STAR` (0x88): Accessed `vm_state->query_context->bitsets[h_src]` without `VALIDATE_BITSET_HANDLE(h_src)`.
  6. `OP_INIT_MOCK_NODE_ATTR` (0x0B) / `OP_INIT_MOCK_EDGE_ATTR` (0x0C): `mock_attr.mask = vm_state->query_context->bitsets[h_mask];` shallow-copied a `VmBitSet` struct whose `words` pointer became dangling or invalid across queries, leading to `EXC_BAD_ACCESS` when subsequent queries called `mock_attr.mask.test(u)`.
* **Resolution**:
  - Added `VALIDATE_BITSET_HANDLE` across `OP_GATHER_NODE_ATTR`, `OP_COO_WALK`, `OP_COO_WALK_FILTERED`, `OP_COO_WALK_REDUCE`, `OP_CSR_WALK_DIRECT_STORE`, `OP_CSR_WALK_DENSE_STREAM`, `OP_CSC_WALK_DIRECT_STORE`, `OP_DENSE_WALK`, and `OP_FIXPOINT_KLEENE_STAR`.
  - In `MockAttribute`, replaced the shallow `VmBitSet mask;` slice with a dedicated vector `std::vector<uint64_t> mask_words;` and helper `mask_test(bit_idx)`, deep copying mask words on initialization.
  - Implemented `impulse_vm_context_reset(ctx)` to recycle off-heap contexts and clear mock attributes between test executions.
* **Status**: ✅ Resolved and verified across 20,000 iterations in `fuzz_vm_bytecode`.

---

### Bug `CORE-FUZZ-006`: Out-of-Bounds Float/Double Vector Handle Dereference in VM Vector Operations
* **Date**: 2026-09-07
* **Component**: `impulse-cpp/src/impulse_vm.cpp` (`impOps` Execution Engine)
* **Discovered By**: `fuzz_vm_bytecode` (Deterministic Run)
* **Crash Signature**:
  ```text
  stop reason = EXC_BAD_ACCESS (code=2, address=0x100b94000)
  frame #0: hwy::N_NEON::LoadU(...)
  frame #1: impulse::N_NEON::VectorScaleF32(float*, float, unsigned long)
  frame #2: impulse::DispatchVectorScaleF32
  frame #3: impulse_simd_vector_scale_f32
  frame #4: impulse_vm_execute
  ```
* **Root Cause**:
  In vector operations such as `OP_VECTOR_MUL_ATTR`, `OP_FLOAT_VECTOR_SCALE`, `OP_EWISE_ADD`, `OP_EWISE_MULT`, `OP_COALESCE`, `OP_EXTRACT_VALIDITY`, `OP_VECTOR_DIV`, `OP_L1_NORM_DIFF`, etc., registers holding `TYPE_FLOAT_VECTOR`, `TYPE_DOUBLE_VECTOR`, or `TYPE_NODE_VECTOR` can contain arbitrary handle integers (e.g. `0x16fe0a3cd` or `4294967295`) produced by random/forged bytecode.
  When indexing `ctx->float_vectors[handle]` or `ctx->double_vectors[handle]` without verifying `0 <= handle < VM_MAX_VECTOR_HANDLES && ctx->float_vectors_allocated[handle]`, the VM accesses unmapped or corrupted memory pointers passed to Highway SIMD intrinsics, resulting in `EXC_BAD_ACCESS` / Bus Error.
* **Resolution**:
  Defined `VALIDATE_FLOAT_VECTOR_HANDLE(h)` and `VALIDATE_DOUBLE_VECTOR_HANDLE(h)` (checking `0 <= h && static_cast<size_t>(h) < VM_MAX_VECTOR_HANDLES && vm_state->query_context->float_vectors_allocated[h]`) and applied bounds checks across all vector instructions before dereferencing vector handles.
* **Status**: ✅ Resolved and verified in `fuzz_vm_bytecode`.

---

### Bug `CORE-FUZZ-005`: Missing Register Index Bounds Check on `src` in `OP_PROJECT_STATE`
* **Date**: 2026-09-07
* **Component**: `impulse-cpp/src/impulse_vm.cpp` (`impOps` Execution Engine)
* **Discovered By**: `fuzz_vm_bytecode` (Deterministic Run, Program 104, Seed 987654321)
* **Crash Signature**:
  ```text
  stop reason = EXC_BAD_ACCESS (code=1, address=0x16fe0a58d)
  frame #0: impulse_vm_execute(..., inst={0x3a, 0xf, 23, 0x5fc7bcfd}) at impulse_vm.cpp:1901:9
  ```
* **Root Cause**:
  In `OP_PROJECT_STATE` (`opcode 0x3A`), the instruction payload encodes `src` as `inst.payload & 0xFFFF` (or `(inst.payload >> 16) & 0xFFFF`). In random or forged bytecode, this value can be up to 65535.
  `OP_PROJECT_STATE` immediately accessed `vm_state->register_types[src]` without invoking `VALIDATE_REG(src)`. Because `vm_state->register_types` is a fixed-size array of 64 elements (`uint8_t register_types[64]`), an out-of-bounds index like `src = 0xbcfd` (48,381) reads far outside the stack frame/state struct, triggering an immediate segmentation fault (`EXC_BAD_ACCESS`).
* **Resolution**:
  Added `VALIDATE_REG(src);` and `VALIDATE_REG(dst);` to `OP_PROJECT_STATE` before indexing `vm_state->register_types[src]`.
* **Status**: ✅ Resolved.

---

### Bug `CORE-FUZZ-004`: Out-of-Bounds BitSet / Vector Handle Dereference in VM Set Operations
* **Date**: 2026-09-07
* **Component**: `impulse-cpp/src/impulse_vm.cpp` (`ImpOps` Execution Engine)
* **Discovered By**: `fuzz_vm_bytecode` (Deterministic Run, Iteration 105, Seed 987654321)
* **Crash Signature**:
  ```text
  stop reason = EXC_BAD_ACCESS (code=1, address=0x16fe0a3cd)
  frame #0: impulse_vm_execute
  frame #1: LLVMFuzzerTestOneInput
  ```
* **Root Cause**:
  In `OP_FRONTIER_DIFF`, `OP_SET_UNION`, `OP_SET_INTERSECT`, and related set operations, registers with type `TYPE_BITSET_HANDLE` (or `TYPE_FLOAT_VECTOR`, etc.) can contain arbitrary `uint64_t` scalar values (e.g. `registers[r] = 0x16fe0a3cd`) if forged by arbitrary bytecode sequences. When static casting `h = static_cast<int>(registers[r])` without validating `0 <= h && h < VM_MAX_BITSET_HANDLES`, indexing `ctx->bitsets[h]` reads out-of-bounds heap memory, causing `EXC_BAD_ACCESS`.
* **Resolution**:
  Introduced `VALIDATE_BITSET_HANDLE(h)` checking `0 <= h && static_cast<size_t>(h) < VM_MAX_BITSET_HANDLES && vm_state->query_context->bitsets[h].words != nullptr` (returning `IMPULSE_VM_ERR_OUT_OF_BOUNDS` on invalid handles).
* **Status**: ✅ Resolved and verified in `fuzz_vm_bytecode`.

---

### Bug `CORE-FUZZ-001`: Null Pointer Dereference in `impulse_vm_execute` on Graph Opcodes
* **Date**: 2026-09-07
* **Component**: `impulse-cpp/src/impulse_vm.cpp` (`impOps` Execution Engine)
* **Discovered By**: `fuzz_vm_bytecode` (Deterministic Run, Iteration 11, Seed 987654321)
* **Crash Signature**:
  ```text
  stop reason = EXC_BAD_ACCESS (code=1, address=0x44d8)
  frame #0: std::vector<BoundRelationSlot>::size() const
  frame #1: impulse_vm_execute
  frame #2: LLVMFuzzerTestOneInput
  ```
* **Root Cause**:
  `impulse_vm_execute` was invoked with `vm_state->query_context == nullptr`.
  When arbitrary instruction streams containing graph relation operations (e.g. `OP_CSR_WALK`, `OP_CSC_WALK`, `OP_CSR_DEGREE`, `OP_READ_EDGE_WEIGHT`, etc.) were executed, the VM dispatch handler attempted to access `vm_state->query_context->slots.size()` without verifying that `vm_state->query_context != nullptr`. Offset `0x44d8` is the offset of `slots` inside `struct impulse_vm_context`.
* **Resolution**:
  Added an RAII `ContextGuard` inside `impulse_vm_execute` that checks if `!vm_state->query_context`. If null, it instantiates a lightweight fallback context (`impulse_vm_context_create(nullptr)`) and safely destroys it upon function exit.
* **Status**: ✅ Resolved and verified in `fuzz_vm_bytecode`.

---

### Bug `CORE-FUZZ-003`: Null Child Node Dereference in `AstOptimizer::fold_function_call`
* **Date**: 2026-09-07
* **Component**: `impulse-cpp/include/impulse_cel.h` (`AstOptimizer`)
* **Discovered By**: `fuzz_cel_parser` (Deterministic Run, Iteration 152, Seed 555555555)
* **Crash Signature**:
  ```text
  stop reason = EXC_BAD_ACCESS (code=1, address=0x0)
  frame #0: impulse::cel::AstOptimizer::fold_function_call(std::shared_ptr<impulse::cel::AstNode> const&)
  frame #1: impulse::cel::AstOptimizer::optimize
  ```
* **Root Cause**:
  In `AstOptimizer::fold_function_call`, `node->children.size() == 1` / `2` / `3` checks did not check whether `node->children[i]` was a `nullptr`. When parser recovery, syntax mutations, or nested expressions produce AST nodes with null argument children (e.g., failed subexpression parse inside `func(bad_token)`), accessing `arg->kind` dereferences null (`address=0x0`), causing an immediate segmentation fault.
* **Resolution**:
  Added defensive null-check guards (`if (!child) return node;`) across all folder routines in `AstOptimizer` (`fold_unary`, `fold_binary`, `fold_ternary`, and `fold_function_call`).
* **Status**: ✅ Resolved and verified across 50,000 iterations in `fuzz_cel_parser`.

---

### Bug `CORE-FUZZ-002`: Rejection of Empty (0-Edge) Graphs in `impulse_writer_add_relation`
* **Date**: 2026-09-07
* **Component**: `impulse-cpp/src/impulse_graph.cpp` (`SnapshotWriter`)
* **Discovered By**: `test_property_snapshot` (Trial 45, randomized empty edge topology)
* **Symptom**: `impulse_writer_add_relation` returned `IMPULSE_ERR_INVALID_ARGUMENT` when writing a graph with 0 edges.
* **Root Cause**:
  The guard check in `impulse_writer_add_relation` was:
  ```cpp
  if (!writer || !row_offsets_data || !col_indices_data) return IMPULSE_ERR_INVALID_ARGUMENT;
  ```
  For empty graphs ($|E| = 0$), `col_indices_bytes == 0` and the caller passes `col_indices_data == nullptr`. The unconditional check `!col_indices_data` falsely classified this legitimate edge case as an invalid argument.
* **Resolution**:
  Updated the check to:
  ```cpp
  if (!writer || !row_offsets_data || (col_indices_bytes > 0 && !col_indices_data)) return IMPULSE_ERR_INVALID_ARGUMENT;
  ```
  and guarded `rel.col_indices.resize/memcpy` with `if (col_indices_bytes > 0 && col_indices_data)`.
* **Status**: ✅ Resolved and verified across 100 trials in `test_property_snapshot`.

---

### Bug `CORE-FUZZ-008`: Unchecked Silent Fallback to Relation 0 in Rust `resolve_relation_index`
* **Date**: 2026-09-07
* **Component**: `impulse-rust/src/traversal.rs` (`resolve_relation_index`)
* **Discovered By**: `mutation_test.rs` (Mutant 2.6: non-existent relation traversal)
* **Symptom**: Querying a non-existent relation (e.g. `reader.traverse(0).out("non_existent")`) silently executed against relation 0 instead of returning `Err(ImpulseError::NotFound)`.
* **Root Cause**:
  In `resolve_relation_index(&self, name_or_idx: &str)`:
  ```rust
  if self.reader.relation_count() > 0 {
      return Ok(0);
  }
  Err(ImpulseError::NotFound)
  ```
  This legacy fallback unconditionally resolved any invalid or unrecognized relation string to relation `0` whenever `relation_count() > 0`.
* **Resolution**:
  Removed the unconditional fallback, strictly returning `Err(ImpulseError::NotFound)` when `name_or_idx` fails numeric parsing and does not match any existing relation name.
* **Status**: ✅ Resolved and verified in `mutation_test.rs`.

