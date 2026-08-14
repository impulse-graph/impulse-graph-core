# Impulse Graph Core — Implementation Roadmap & ISA Extensions (`TODO.md`)

This document tracks implementation tasks for the **Core Engine Kernel (`impulse-cpp`, `impulse-rust`, `impulse-compiler`)** and cross-engine alignment with `impulse-graph-java` and `impulse-graph-spec`.

---

## 1. Virtual Machine Opcode Completeness & Vector Math Engine

### 1.1 Multi-Layout Traversal Engine (CSR, CSC, COO, DENSE)
- [ ] **CSR (Compressed Sparse Row - Push Path)**:
  - `OP_CSR_WALK` (`0x10`), `OP_CSR_WALK_FILTERED` (`0x11`), `OP_CSR_WALK_REDUCE` (`0x17`), `OP_CSR_WALK_REDUCE_SUM` (`0x16`).
  - `OP_CSR_WALK_DIRECT_STORE` (`0x84`): Injective ($1:1$) direct store lock elimination.
  - `OP_CSR_WALK_DENSE_STREAM` (`0x85`): Surjective streaming store (`_mm512_stream_si512`).
- [ ] **CSC (Compressed Sparse Column - Pull Path)**:
  - `OP_CSC_WALK` (`0x18`), `OP_CSC_WALK_FILTERED` (`0x1D`), `OP_CSC_WALK_REDUCE` (`0x1E`), `OP_CSC_WALK_REDUCE_SUM` (`0x1F`).
  - `OP_CSC_WALK_DIRECT_STORE` (`0x87`): Injective ($1:1$) pull-path direct store.
- [ ] **COO (Coordinate Format / Edge List - Massively Parallel Edge Path)**:
  - `OP_COO_WALK` (`0x86`): Uniform edge-parallel sweep across flat `src[], dst[]` arrays.
  - `OP_COO_WALK_FILTERED` (`0x8B`): Filtered COO sweep with SIMD vector masks.
  - `OP_COO_WALK_REDUCE` (`0x8C`): COO reduction into destination vector.
  - `OP_COO_WALK_DIRECT_STORE` (`0x8D`): 1:1 injective COO edge store.
- [ ] **DENSE (Dense 2D Matrix & Packed BitMatrix)**:
  - `OP_DENSE_WALK` (`0x8E`): Dense matrix-vector BLAS Level 2 GEMV sweep.
  - `OP_DENSE_WALK_BITMATRIX` (`0x8F`): 512-bit SIMD bitwise AND/OR over dense boolean adjacency bitmatrices (64 nodes/word).
  - `OP_DENSE_WALK_REDUCE` (`0x94`): Dense matrix reduction.
  - `OP_DENSE_WALK_DIRECT_STORE` (`0x95`): Dense direct projection.

### 1.2 Vector Masking & Comparison Micro-Ops (Vector Predicates)
- [ ] Implement SIMD comparison opcodes:
  - `OP_VEC_CMP_EQ` (`0x20`), `OP_VEC_CMP_GT` (`0x21`), `OP_VEC_CMP_LT` (`0x22`), `OP_VEC_CMP_BETWEEN` (`0x23`).
- [ ] Implement bitmask boolean combinators:
  - `OP_MASK_AND` (`0x24`), `OP_MASK_OR` (`0x25`), `OP_MASK_NOT` (`0x26`), `OP_VEC_BLEND` (`0x27`).
- [ ] Hardware acceleration: AVX-512 `_mm512_cmp_*_mask` / ARM Neon `vcgtq_*` bitmask register allocation.

### 1.3 Table-Driven 42-Function Vector Math Engine
- [ ] Implement Generic Vector Math Opcodes:
  - `OP_VEC_MATH_UNARY` (`0x2D`): `<type_tag> <func_id> <r_dst> <r_src>`
  - `OP_VEC_MATH_BINARY` (`0x2E`): `<type_tag> <func_id> <r_dst> <r_src1> <r_src2>`
  - `OP_VEC_MATH_TERNARY` (`0x2F`): `<type_tag> <func_id> <r_dst> <r_src1> <r_src2> <r_src3>`
- [ ] C++20 / Rust SIMD Math Table (`math_ops.def`):
  - **Algebraic & Roots**: `abs`, `sqrt`, `rsqrt`, `cbrt`, `pow`, `hypot`, `lerp`.
  - **Exponential & Logarithmic**: `exp`, `exp2`, `exp10`, `expm1`, `log`, `log2`, `log10`, `log1p`.
  - **Trigonometric & Spatial**: `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`, `sinc`.
  - **Hyperbolic & Poincaré GNN**: `sinh`, `cosh`, `tanh`, `asinh`, `acosh`, `atanh`.
  - **Rounding & Clamping**: `floor`, `ceil`, `trunc`, `round`, `clamp`, `copysign`, `fmod`.
  - **GNN & Neural Activations**: `relu`, `leaky_relu`, `sigmoid`, `gelu`, `silu`, `softplus`.
  - **Statistics & Special**: `erf`, `erfc`, `lgamma`.
  - **Discrete & Bitwise**: `popcount`, `clz`, `ctz`, `rotl`, `rotr`.
- [ ] C++ kernel integration: Link SLEEF / Intel SVML / LLVM intrinsics for vectorized transcendental pipelines.
- [ ] Java 25 integration: Map `func_id` directly to `VectorOperators` and `MethodHandle` combinators.

### 1.4 Functional Optics & Columnar Gathers (Late Materialization)
- [ ] Implement `OP_LOAD_COLUMN_VECTOR` (`0x80`): Direct sequential vector load from `.imps` columnar attribute section.
- [ ] Implement `OP_GATHER_NODE_ATTR` (`0x81`): Strided vector gather of node attributes for an active frontier.
- [ ] Implement `OP_GATHER_EDGE_ATTR` (`0x82`): Strided vector gather of edge attributes indexed by edge IDs.
- [ ] Implement `OP_BRIN_ZONE_SKIP` (`0x83`): Hardware-accelerated test against Section 5 BRIN min/max metadata.

### 1.5 Multiplicity-Driven Lock Elimination Fast-Paths
- [ ] Implement `OP_CSR_WALK_DIRECT_STORE` (`0x84`): Injective ($1:1$ / $1:N$) direct memory store bypassing atomic CAS and reduction buffers.
- [ ] Implement `OP_CSR_WALK_DENSE_STREAM` (`0x85`): Surjective streaming store (`_mm512_stream_si512`) over dense destination domain.

### 1.6 Fixpoint & Transitive Closure Acceleration
- [ ] Implement `OP_FIXPOINT_KLEENE_STAR` (`0x88`): Native off-heap fixed-point combinator with ping-pong buffer swapping.
- [ ] Implement `OP_SWAP_REG` (`0x89`): $O(1)$ zero-copy pointer swap between register buffers.
- [ ] Implement `OP_FRONTIER_DIFF` (`0x8A`): Delta-stepping frontier subtraction $\Delta \mathcal{S}_t = \mathcal{S}_t \setminus \mathcal{S}_{t-1}$.

---

## 2. Java 25 `MethodHandle` JIT Combinator Engine (`impulse-graph-java`)

- [ ] Transition `ImpulseMethodHandleCompiler` from Level 1 (Curried Interpreter) to **Level 2 (True JIT Combinator Tree)**:
  - Parse compiled `impOps` bytecode sequence into a chained `MethodHandle` DAG using `MethodHandles.foldArguments()`, `MethodHandles.filterArguments()`, and `MethodHandles.collectArguments()`.
  - Statically resolve vector math `func_id` into monomorphic `VectorOperators` bindings.
  - Enable HotSpot C2 to unroll the bytecode loop into a single, straight-line native AVX-512 machine code kernel.

---

## 3. Concise Infix Expression Language (`ImpExp` / `impulse-expr`)

### 3.1 Motivation
Replacing awkward method-chained fluent builders (`Edge.attr("miles").gt(100.0).and(...)`) with a lightweight, clean, human-readable infix expression syntax.

### 3.2 Specification & Grammar
- [ ] Syntax Examples:
  - **Filter**: `query.filter("e.miles > 100.0 and d.status == 1")`
  - **Spatial**: `query.filter("hypot(s.x - d.x, s.y - d.y) < 15.0")`
  - **Power Grid**: `query.reduceSum("s.v * d.v * (e.g * cos(s.theta - d.theta) + e.b * sin(s.theta - d.theta))")`
  - **AML Time Decay**: `query.reduceSum("e.amount * exp(-0.05 * (now() - e.timestamp))")`
- [ ] Compiler Pipeline:
  - Build zero-dependency Pratt / recursive-descent parser (`impulse-expr` in Rust / Java / C++).
  - Parse infix strings directly into **ImpScheme S-Expression AST** at query definition time.
  - Perform static type checking and type coercion (`Int32` $\sqcup$ `Float64` $\to$ `Float64`) against the snapshot schema.
  - Emit vectorized `impOps` bytecode.
