# Impulse Graph Core — Implementation Roadmap & ISA Extensions (`TODO.md`)

This document tracks implementation tasks for the **Core Engine Kernel (`impulse-cpp`, `impulse-rust`, `impulse-compiler`)** and cross-engine alignment with `impulse-graph-java` and `impulse-graph-spec`.

---

## 1. Virtual Machine Opcode Completeness & Vector Math Engine

### 1.1 Multi-Layout Traversal Engine (CSR, CSC, COO, DENSE)
- [x] **CSR (Compressed Sparse Row - Push Path)**:
  - `OP_CSR_WALK` (`0x10`), `OP_CSR_WALK_FILTERED` (`0x11`), `OP_CSR_WALK_REDUCE` (`0x17`), `OP_CSR_WALK_REDUCE_SUM` (`0x16`).
  - `OP_CSR_WALK_DIRECT_STORE` (`0x84`): Injective ($1:1$) direct store lock elimination.
  - `OP_CSR_WALK_DENSE_STREAM` (`0x85`): Surjective streaming store (`_mm512_stream_si512`).
- [x] **CSC (Compressed Sparse Column - Pull Path)**:
  - `OP_CSC_WALK` (`0x18`), `OP_CSC_WALK_FILTERED` (`0x1D`), `OP_CSC_WALK_REDUCE` (`0x1E`), `OP_CSC_WALK_REDUCE_SUM` (`0x1F`).
  - `OP_CSC_WALK_DIRECT_STORE` (`0x87`): Injective ($1:1$) pull-path direct store.
- [x] **COO (Coordinate Format / Edge List - Massively Parallel Edge Path)**:
  - `OP_COO_WALK` (`0x86`): Uniform edge-parallel sweep across flat `src[], dst[]` arrays.
  - `OP_COO_WALK_FILTERED` (`0x8B`): Filtered COO sweep with SIMD vector masks.
  - `OP_COO_WALK_REDUCE` (`0x8C`): COO reduction into destination vector.
  - `OP_COO_WALK_DIRECT_STORE` (`0x8D`): 1:1 injective COO edge store.
- [x] **DENSE (Dense 2D Matrix & Packed BitMatrix)**:
  - `OP_DENSE_WALK` (`0x8E`): Dense matrix-vector BLAS Level 2 GEMV sweep.
  - `OP_DENSE_WALK_BITMATRIX` (`0x8F`): 512-bit SIMD bitwise AND/OR over dense boolean adjacency bitmatrices (64 nodes/word).
  - `OP_DENSE_WALK_REDUCE` (`0x94`): Dense matrix reduction.
  - `OP_DENSE_WALK_DIRECT_STORE` (`0x95`): Dense direct projection.

### 1.2 Vector Masking & Comparison Micro-Ops (Vector Predicates)
- [x] Implement SIMD comparison opcodes:
  - `OP_VEC_CMP_EQ` (`0x20`), `OP_VEC_CMP_GT` (`0x21`), `OP_VEC_CMP_LT` (`0x22`), `OP_VEC_CMP_BETWEEN` (`0x23`).
- [x] Implement bitmask boolean combinators:
  - `OP_MASK_AND` (`0x24`), `OP_MASK_OR` (`0x25`), `OP_MASK_NOT` (`0x26`), `OP_VEC_BLEND` (`0x27`).
- [x] Hardware acceleration: AVX-512 `_mm512_cmp_*_mask` / ARM Neon `vcgtq_*` bitmask register allocation.

### 1.3 Table-Driven 42-Function Vector Math Engine
- [x] Implement Generic Vector Math Opcodes:
  - `OP_VEC_MATH_UNARY` (`0x2D`): `<type_tag> <func_id> <r_dst> <r_src>`
  - `OP_VEC_MATH_BINARY` (`0x2E`): `<type_tag> <func_id> <r_dst> <r_src1> <r_src2>`
  - `OP_VEC_MATH_TERNARY` (`0x2F`): `<type_tag> <func_id> <r_dst> <r_src1> <r_src2> <r_src3>`
- [x] C++20 / Rust SIMD Math Table (`impulse_math_ops.h` / `math_ops.def`):
  - **Algebraic & Roots**: `abs`, `sqrt`, `rsqrt`, `cbrt`, `pow`, `hypot`, `lerp`.
  - **Exponential & Logarithmic**: `exp`, `exp2`, `exp10`, `expm1`, `log`, `log2`, `log10`, `log1p`.
  - **Trigonometric & Spatial**: `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`, `sinc`.
  - **Hyperbolic & Poincaré GNN**: `sinh`, `cosh`, `tanh`, `asinh`, `acosh`, `atanh`.
  - **Rounding & Clamping**: `floor`, `ceil`, `trunc`, `round`, `clamp`, `copysign`, `fmod`.
  - **GNN & Neural Activations**: `relu`, `leaky_relu`, `sigmoid`, `gelu`, `silu`, `softplus`.
  - **Statistics & Special**: `erf`, `erfc`, `lgamma`.
  - **Discrete & Bitwise**: `popcount`, `clz`, `ctz`, `rotl`, `rotr`.
- [x] C++ kernel integration: Link SLEEF / Intel SVML / LLVM intrinsics for vectorized transcendental pipelines.
- [x] Java 25 integration: Map `func_id` directly to `VectorOperators` and `MethodHandle` combinators.

### 1.4 Functional Optics & Columnar Gathers (Late Materialization)
- [x] Implement `OP_LOAD_COLUMN_VECTOR` (`0x80`): Direct sequential vector load from `.imps` columnar attribute section.
- [x] Implement `OP_GATHER_NODE_ATTR` (`0x81`): Strided vector gather of node attributes for an active frontier.
- [x] Implement `OP_GATHER_EDGE_ATTR` (`0x82`): Strided vector gather of edge attributes indexed by edge IDs.
- [x] Implement `OP_BRIN_ZONE_SKIP` (`0x83`): Hardware-accelerated test against Section 5 BRIN min/max metadata.

### 1.5 Multiplicity-Driven Lock Elimination Fast-Paths
- [x] Implement `OP_CSR_WALK_DIRECT_STORE` (`0x84`): Injective ($1:1$ / $1:N$) direct memory store bypassing atomic CAS and reduction buffers.
- [x] Implement `OP_CSR_WALK_DENSE_STREAM` (`0x85`): Surjective streaming store (`_mm512_stream_si512`) over dense destination domain.

### 1.6 Fixpoint & Transitive Closure Acceleration
- [x] Implement `OP_FIXPOINT_KLEENE_STAR` (`0x88`): Native off-heap fixed-point combinator with ping-pong buffer swapping.
- [x] Implement `OP_SWAP_REG` (`0x89`): $O(1)$ zero-copy pointer swap between register buffers.
- [x] Implement `OP_FRONTIER_DIFF` (`0x8A`): Delta-stepping frontier subtraction $\Delta \mathcal{S}_t = \mathcal{S}_t \setminus \mathcal{S}_{t-1}$.

---

## 2. Java 25 `MethodHandle` JIT Combinator Engine (`impulse-graph-java`)

- [x] Transition `ImpulseMethodHandleCompiler` from Level 1 (Curried Interpreter) to **Level 2 (True JIT Combinator Tree)**:
  - Parse compiled `impOps` bytecode sequence into a chained `MethodHandle` DAG using `MethodHandles.foldArguments()`, `MethodHandles.filterArguments()`, and `MethodHandles.collectArguments()`.
  - Statically resolve vector math `func_id` into monomorphic `VectorOperators` bindings.
  - Enable HotSpot C2 to unroll the bytecode loop into a single, straight-line native AVX-512 machine code kernel.

---

## 3. Google CEL (Common Expression Language) Engine (`ImpExp` / `impulse-cel`)

### 3.1 Motivation & Standard Selection
Adopt **Google CEL (Common Expression Language)** as the official, unified declarative expression language across Impulse for filters, projections, state transitions, and authorization policies.

### 3.2 Standard CEL Grammar + Analytical Graph Extensions
- [x] **Standard Built-in CEL Syntax**:
  - Logical: `&&`, `||`, `!`, ternary `? :`
  - Comparisons: `==`, `!=`, `<`, `<=`, `>`, `>=`
  - Arithmetic: `+`, `-`, `*`, `/`, `%`
  - Field Access: `edge.miles`, `dest.status`, `state.val`
  - Collections & Sets: `dest.status in [1, 2, 5]`, `edge.tags.exists(t, t == 'priority')`
  - Strings: `dest.region.startsWith("us-")`, `edge.label.contains("ship")`, `size(dest.name)`
- [x] **CEL Datetime & Temporal Extensions**:
  - Literals & Constructors: `timestamp("2026-08-13T00:00:00Z")`, `now()`, `duration("24h")`, `duration("30m")`
  - Temporal Arithmetic: `edge.timestamp + duration("7d")`, `now() - edge.timestamp > duration("24h")`
  - Date Field Extraction: `t.getFullYear()`, `t.getMonth()`, `t.getDayOfMonth()`, `t.getHours()`
  - Physical execution: 64-bit epoch microsecond integers with hardware SIMD integer addition/comparison (`_mm512_add_epi64`, `_mm512_cmp_epi64_mask`).
- [x] **CEL Vector Math Extensions (`math.*` or Global Primitives)**:
  - **Algebraic & Spatial**: `sqrt(x)`, `rsqrt(x)`, `cbrt(x)`, `pow(x, y)`, `hypot(x, y)`, `lerp(a, b, t)`
  - **Exponential & Log**: `exp(x)`, `exp2(x)`, `expm1(x)`, `log(x)`, `log2(x)`, `log1p(x)`
  - **Trigonometric (Power Grid)**: `sin(x)`, `cos(x)`, `tan(x)`, `asin(x)`, `acos(x)`, `atan2(y, x)`, `sinc(x)`
  - **Hyperbolic & Poincaré**: `sinh(x)`, `cosh(x)`, `tanh(x)`, `asinh(x)`, `acosh(x)`, `atanh(x)`
  - **Rounding & Clamping**: `floor(x)`, `ceil(x)`, `trunc(x)`, `round(x)`, `clamp(x, min, max)`, `fmod(x, y)`
  - **GNN & Neural**: `relu(x)`, `leaky_relu(x, a)`, `sigmoid(x)`, `gelu(x)`, `silu(x)`, `softplus(x)`
  - **Statistical**: `erf(x)`, `erfc(x)`, `lgamma(x)`
  - **Discrete & Bitwise**: `popcount(x)`, `clz(x)`, `ctz(x)`, `rotl(x, k)`, `rotr(x, k)`

### 3.3 Zero-Dependency Parser & Compilation Pipeline
- [x] Implement self-contained Pratt parser (`~350 LOC` in C++20 / Java 25 / Rust) with **0 external dependencies**.
- [x] Parse CEL AST directly into **ImpScheme S-Expression Free Monad IR**.
- [x] Static type checking & automatic type widening against `.imps` schema catalog.
- [x] Emit vectorized `impOps` bytecode (AVX-512 / Neon) and Java 25 `MethodHandle` combinators.

---

## 4. Unified In-Kernel C-ABI Compiler & SQLite-Style Statement API

- [x] **Unified Multi-Frontend In-Kernel C++ Compiler Engine**:
  - Implemented 7-stage compiler pipeline lowering ImpLog, ImpK, openCypher, and ImpScheme to `impOps` bytecode.
  - Implemented C-ABI exports `impulse_compile_query()`, `impulse_compile_to_impas()`, `impulse_compile_and_execute()`.
- [x] **SQLite-Style Canonical Statement Execution API (`impulse_stmt_*`)**:
  - `impulse_stmt_prepare()`: Compiles query and computes deterministic caller buffer sizing.
  - `impulse_stmt_buffer_size()`: Returns exact memory footprint required (scratch area + Apache Arrow-style columnar buffers + null bitmaps).
  - `impulse_stmt_bind_*()`: Binds parameter values (node IDs, dense bitsets, roaring bitmaps, scalar integers, floats, strings, UUIDs, vectors).
  - `impulse_stmt_execute()`: Executes zero-allocation VM traversal directly into caller-allocated memory.
  - `impulse_stmt_column_*()`: Column accessors (`row_count`, `column_count`, `column_name`, `column_type`, `column_dim`, `column_data`, `column_is_null`).
  - `impulse_stmt_finalize()`: Statement resource cleanup.
  - `impulse_exec()`: One-line zero-boilerplate convenience wrapper.
- [x] **Ecosystem Tooling & FFI Integration**:
  - `impulse-graph-tooling` updated to lower DSLs via the C-ABI compiler bridge.
  - `impulse-graph-core/impulse-rust` FFI updated with statement and compiler bindings.
  - Redundant standalone `impulse-compiler` crate successfully retired and deleted.

---

## 5. Multi-Hop Loop, Sparse Frontier Watermark & Context Pooling Optimizations (Port from `impulse-graph-java`)

### Applicability Matrix (`impulse-graph-java` $\to$ `impulse-graph-core`)

| Optimization | `impulse-graph-java` Status | `impulse-cpp` / `impulse-rust` Status | Applicability & Impact in `impulse-graph-core` |
| :--- | :---: | :---: | :--- |
| **1. `O(1)` Array-Indexed Relation Slot Lookup** | Implemented (`GraphSnapshot.getRelationSnapshot(int)`) | **Already Present** (`ctx->slots[rel]`) | `impulse_vm.cpp` already pre-indexes `std::vector<BoundRelationSlot> slots`; however, slot construction is repeated per `impulse_vm_context_create()` instead of cached per `impulse_snapshot_t*`. |
| **2. In-Place Destination BitSet Handle Reuse (`dst != src`)** | Implemented (`executeCsrWalk`, `handleCscWalk`, `handleMov`) | **Already Present** (`impulse_vm.cpp:946,1297,1581`) | Already implemented in C++ `OP_MOV`, `OP_CSR_WALK`, and `OP_CSC_WALK`. |
| **3. Direct Instruction Dispatch (No `MethodHandle` Boxing)** | Implemented (`JitDriver` pre-decoded array) | **Already Present** (`HAS_COMPUTED_GOTO`) | `impulse_vm.cpp` already uses direct computed-goto (`goto *dispatch_table[op]`) over flat `impulse_instruction_t*`. |
| **4. Active Word-Range Watermarks (`[min_word .. max_word]`) & 2-Level Summary Words in `VmBitSet`** | Implemented (`OffHeapBitSet`) | **TODO (`impulse_vm.cpp`)** | **High Impact**: `VmBitSet` currently scans/zeroes all `words_per_bitset` ($\ge 1,024$ words) on every `clear()`, `OP_CSR_WALK`, `OP_SET_UNION`, `OP_SET_INTERSECT`, `OP_SET_DIFFERENCE`, `OP_SET_CARDINALITY`, and `OP_STABLE_CHECK`. |
| **5. Thread-Local `impulse_vm_context_t` Pooling & Lazy Vector Allocation** | Implemented (`VmQueryContext.acquire`, `TL_STATE_POOL`, `TL_BITSET_POOL`) | **TODO (`impulse_vm.cpp`, `impulse_vm_fluent.cpp`)** | **Critical Impact**: `impulse_vm_context_create()` eagerly allocates `32` float/double/node vectors of length `max_nodes` ($\sim 32\ \text{MiB}$ at $50\text{k}$ nodes) on every `CompiledQuery::execute()`. |
| **6. Delta-Frontier `repeatUntilStable` & Reflexive Bounded `repeat(k)`** | Implemented (`ImpOpsBytecodeEmitter`) | **TODO (`impulse_vm_fluent.cpp`, `impulse_compiler.hpp`)** | **High Impact**: Eliminates $O(k \cdot |\text{interior}|)$ redundant CSR row re-expansions on undirected/corridor/H3 $k$-ring traversals ($30\times$ fewer CSR expansions in `62`-hop isochrones). |
| **7. Rust BitSet Frontier & Native CSC Pull Traversal** | N/A | **TODO (`impulse-rust/src/traversal.rs`)** | **High Impact**: `Traversal::to_vec()` currently allocates a `HashSet<u64>` per hop and scans all CSR rows in $O(V + E)$ for `StepDirection::In` instead of walking Section 6 CSC arrays. |

### 5.1 Active Word-Range Watermarks (`[min_word .. max_word]`) in `VmBitSet` (`impulse-cpp/src/impulse_vm.cpp`)
- [ ] Extend `struct VmBitSet` (`impulse_vm.cpp:34-62`) with `size_t min_word = word_count;` and `int64_t max_word = -1;` watermarks plus `max_possible_cardinality()` and `tighten_bounds()`.
- [ ] Bound `VmBitSet::clear()` to `std::memset(words + min_word, 0, (max_word - min_word + 1) * sizeof(uint64_t))` and reset `min_word = word_count; max_word = -1`.
- [ ] Update `bitset_add()` and `VmBitSet::set()` / `reset()` to maintain `[min_word .. max_word]`.
- [ ] In `op_CSR_WALK` (`impulse_vm.cpp:1325-1445`):
  - Skip the `std::popcount` frontier cardinality pass when `bs_src.max_possible_cardinality() <= 15000`.
  - Restrict serial word iteration (`for (size_t w = bs_src.min_word; w <= bs_src.max_word; ++w)`) and `ZF` emptiness checks to `[min_word .. max_word]`.
- [ ] Restrict `op_SET_UNION`, `op_SET_INTERSECT`, `op_SET_DIFFERENCE`, `op_SET_CARDINALITY`, `op_STABLE_CHECK`, and `op_COLLECT_ARRAY` loops to the active `[min_word .. max_word]` intervals.

### 5.2 Lazy Vector Allocation & Thread-Local Context Pooling (`impulse-cpp/src/impulse_vm.cpp`, `impulse_vm_fluent.cpp`)
- [ ] Make `float_vectors[i]`, `double_vectors[i]`, and `node_vectors[i]` lazy in `impulse_vm_context_create()` (`impulse_vm.cpp:326-339`): allocate `.resize(ctx->max_nodes)` on demand inside `acquire_float_vector()`, `acquire_double_vector()`, and `acquire_node_vector()` so pure bitset/CSR traversal queries allocate `0` bytes of numeric vector buffers.
- [ ] Cache `std::vector<BoundRelationSlot>` and `std::vector<std::vector<BoundAttributeSlot>>` per `impulse_snapshot_t*` so context initialization shares the immutable slot metadata table across queries.
- [ ] Add a `thread_local` context pool (or reuse handle in `CompiledQuery::execute()` in `impulse_vm_fluent.cpp:41-63`) so single-shot `CompiledQuery::execute(snapshot, input_param)` calls do not `new[]` / `delete[]` the 64-bitset arena on every invocation.

### 5.3 Delta-Frontier Loops in `QueryBuilder` & `ImpulseCompiler` (`impulse-cpp/src/impulse_vm_fluent.cpp`, `include/impulse_compiler.hpp`)
- [ ] Update `QueryBuilder::repeatUntilStable` (`impulse_vm_fluent.cpp:423-431`) and `ImpulseCompiler` to emit the delta-frontier fixed-point sequence (`OP_MOV acc, start`, `body`, `OP_STABLE_CHECK acc, sub_out`, `OP_JZ exit`, `OP_SET_DIFFERENCE start, sub_out, acc`, `OP_SET_UNION acc, acc, start`, `OP_JMP loop_start`, `OP_MOV start, acc`).
- [ ] Add reflexive relation detection ($u \in N(u)$) to `QueryBuilder::repeat(int count, ...)` and `ImpulseCompiler` so bounded $k$-hop walks over reflexive relations emit bounded delta-frontier propagation (`OP_SET_DIFFERENCE` + `OP_JZ` early exit + `OP_SET_UNION` + `OP_LOOP_DECR` + `OP_SWAP_REG`).

### 5.4 Word-Bounded BitSet Frontier & CSC Pull Traversal in Rust (`impulse-rust/src/traversal.rs`)
- [ ] Replace per-hop `std::collections::HashSet<u64>` allocations and `.sort_unstable()` in `Traversal::to_vec()` (`impulse-rust/src/traversal.rs:89-134`) with a reusable `u64` word bitset tracking `[min_word .. max_word]`.
- [ ] Update `StepDirection::In` (`impulse-rust/src/traversal.rs:115-127`) to walk Section 6 CSC (`get_csc_col_offsets` / `get_csc_row_indices`) directly in $O(\sum_{v \in F} \text{deg}_{\text{in}}(v))$ instead of scanning all CSR rows in $O(V + E)$.


