# Impulse Graph Core — Architectural Master Plan & Roadmap v2 (`TODO2.md`)

This document captures the complete architectural blueprint, mathematical foundations, opcode expansions, Google CEL expression integration, and Java 25 `MethodHandle` JIT engine for the **Impulse Graph Engine Core** (`impulse-graph-core`, `impulse-graph-java`, `impulse-graph-spec`).

---

## 1. Multi-Layout Traversal Engine (`CSR`, `CSC`, `COO`, `DENSE`)

Impulse supports native SIMD execution across all 4 canonical graph matrix storage formats:

```
                                Graph Storage Layout Spectrum
                                              │
        ┌─────────────────────┬───────────────┴───────────────┬─────────────────────┐
        ▼                     ▼                               ▼                     ▼
    1. CSR (Row)          2. CSC (Column)                 3. COO (Edge List)    4. DENSE (2D / BitMatrix)
  • Push-based walks    • Pull-based walks              • Massively parallel   • Dense subgraphs & cliques
  • Sparse source       • Dense destination               streaming/GPU batch  • AVX-512 BitMatrix (64x)
  • Outbound neighbors  • Inbound neighbors             • GNN edge sampling    • GEMV BLAS matrix-vector
```

### Complete Opcode Traversal Matrix
| Matrix Format | Basic Traversal | Filtered Traversal (SIMD Mask) | Monoid Reduced Traversal | Multiplicity Fast-Path (1:1) |
| :--- | :--- | :--- | :--- | :--- |
| **CSR (Row Push)** | `OP_CSR_WALK` (`0x10`) | `OP_CSR_WALK_FILTERED` (`0x11`) | `OP_CSR_WALK_REDUCE` (`0x17`) | `OP_CSR_WALK_DIRECT_STORE` (`0x84`) |
| **CSC (Col Pull)** | `OP_CSC_WALK` (`0x18`) | `OP_CSC_WALK_FILTERED` (`0x1D`) | `OP_CSC_WALK_REDUCE` (`0x1E`) | `OP_CSC_WALK_DIRECT_STORE` (`0x87`) |
| **COO (Edge Stream)**| `OP_COO_WALK` (`0x86`) | `OP_COO_WALK_FILTERED` (`0x8B`) | `OP_COO_WALK_REDUCE` (`0x8C`) | `OP_COO_WALK_DIRECT_STORE` (`0x8D`) |
| **DENSE (2D / BitMatrix)**| `OP_DENSE_WALK` (`0x8E`)| `OP_DENSE_WALK_BITMATRIX` (`0x8F`)| `OP_DENSE_WALK_REDUCE` (`0x94`) | `OP_DENSE_WALK_DIRECT_STORE` (`0x95`) |

---

## 2. Table-Driven 42-Function Vector Math Engine

Instead of 42 individual opcodes cluttering the ISA, the physical instruction set uses **3 Generic Vector Math Opcodes**:

```
OP_VEC_MATH_UNARY   <type_tag> <func_id> <reg_dst> <reg_src>          (0x2D)
OP_VEC_MATH_BINARY  <type_tag> <func_id> <reg_dst> <reg_src1> <reg_src2> (0x2E)
OP_VEC_MATH_TERNARY <type_tag> <func_id> <reg_dst> <r1> <r2> <r3>    (0x2F)
```

### Complete 42-Function Taxonomy & Domain Mapping

| Family | Function Primitives | Hardware Implementation | Target Impulse Domain |
| :--- | :--- | :--- | :--- |
| **1. Algebraic & Spatial** | `abs`, `sqrt`, `rsqrt`, `cbrt`, `pow`, `hypot`, `lerp` | HW Instructions (`_mm512_sqrt_pd`, `_mm512_fmadd_pd`) | Spatial Logistics, Euclidean Distance |
| **2. Exp & Logarithmic** | `exp`, `exp2`, `exp10`, `expm1`, `log`, `log2`, `log10`, `log1p` | Vectorized Remez/Chebyshev Polynomials (SVML/SLEEF) | AML Risk Decay, PageRank Damping |
| **3. Trigonometric** | `sin`, `cos`, `tan`, `asin`, `acos`, `atan`, `atan2`, `sinc` | Vectorized Angle Reduction FMAs | Power Grid AC Flow ($V_i V_j \sin(\Delta \theta)$) |
| **4. Hyperbolic & Poincaré** | `sinh`, `cosh`, `tanh`, `asinh`, `acosh`, `atanh` | Vector Exponential Pipelines | Hyperbolic GNNs (HGCN), Tree Embeddings |
| **5. Rounding & Clamping**| `floor`, `ceil`, `trunc`, `round`, `clamp`, `copysign`, `fmod` | Single-Cycle HW (`_mm512_roundscale_pd`, `_mm512_min/max`) | Time Buckets, Boundary Constraints |
| **6. GNN & Neural** | `relu`, `leaky_relu`, `sigmoid`, `gelu`, `silu`, `softplus` | Fused Hardware SIMD Activations | In-Graph Neural Message Passing (`impulse-gnn`) |
| **7. Statistics & Error** | `erf`, `erfc`, `lgamma` | Rational Minimax Polynomials | Tail-Risk AML, Bayesian Priors |
| **8. Discrete & Bitwise** | `popcount`, `clz`, `ctz`, `rotl`, `rotr` | Single-Cycle HW (`_mm512_popcnt_epi64`, `lzcnt`) | Roaring Bitmaps, Motif Intersections |

---

## 3. Google CEL (Common Expression Language) Engine

### 3.1 Syntax & Capabilities
Google CEL is the official declarative expression language for filters, projections, and authorization policies across Impulse:

```java
// Complete Analytical CEL Traversal Example:
var results = impulse.from(BankAccount.class)
    .walk(TransactionEdge.class, BankAccount.class)
    // 1. CEL Boolean Guard with Temporal & Value Filter:
    .filter("now() - edge.timestamp < duration('48h') && edge.amount > 10000.0 && dest.country != 'US'")
    // 2. CEL Vector Math State Mapping:
    .map("edge.amount * exp(-0.02 * (now() - edge.timestamp).toHours())")
    // 3. Algebraic Monoid Reduction:
    .reduce(Monoids.SUM)
    .collect();
```

### 3.2 Datetime, Timestamps & Durations
- **Storage**: Unboxed `int64` epoch microseconds in `.imps` memory-mapped pages.
- **Constructors**: `timestamp("2026-08-13T00:00:00Z")`, `now()`, `duration("24h")`, `duration("30m")`.
- **Temporal Math**: `edge.timestamp + duration("7d")` compiles to single-cycle hardware vector integer additions (`_mm512_add_epi64`) with **zero heap object allocations**.

---

## 4. Mathematical Foundations & Semantic Alignment

```
                        The RWST Monad Transformer Pipeline
                                         │
 ┌───────────────────────────────────────┴───────────────────────────────────────┐
 ▼                                       ▼                                       ▼
1. ReaderT (Attributes)            2. Option / Guard (CEL Filter)          3. WriterT (Monoid Σ, ⊕)
Direct zero-copy mmap pointers     Drops non-matching edges                Folds incoming fiber states
Offsets resolved at bind-time      Subobject classifier pullback           Bounds size: |S| ≤ |V_dest|
```

### 4.1 Set Semantics vs. Multiset Explosion
- SQL and Cypher operate in the **Multiset (Bag) Monad** $\mathcal{M}(X) = X \to \mathbb{N}$, emitting $O(d^k)$ duplicate path tuples.
- Impulse operates in the **Semiring-Weighted Monad** $\mathcal{W}_R(X) = X \to R$.
- Every edge traversal forces a monoid fold ($\bigoplus$) over the destination fiber $\pi_2^{-1}(d)$, strictly guaranteeing the **Frontier Bounding Theorem**:
  $$|\mathcal{S}_{\text{Dst}}| \le |\mathcal{V}_{\text{Dst}}|$$

### 4.2 Multiplicity Fast-Paths & Deforestation
- **Injective ($1:1$ or $1:N$ Inbound)**: Destination fiber size $|\pi_2^{-1}(d)| \le 1$. The compiler deletes atomic CAS and reduction buffers, emitting direct stores (`OP_CSR_WALK_DIRECT_STORE`).
- **Bijective ($1:1$ Isomorphism)**: Deforestation fuses consecutive hops $(g \circ f)$ into a single traversal without allocating intermediate frontier memory.
- **Idempotent Semirings ($a \oplus a = a$)**: Enables lock-free chaotic parallel iteration (Gauss-Seidel style) with guaranteed convergence.

---

## 5. Java 25 `MethodHandle` Level 2 JIT Combinator Engine

```
 S-Expression AST                   Snapshot Binding (Stage 2)               HotSpot C2 Native Code
 ────────────────                   ──────────────────────────               ──────────────────────
 (sin (+ (attr e) 1.0)) ─────────>  MethodHandle Combinator Tree  ─────────> Single Inlined AVX-512 Block:
                                    • exactInvoker()                         vmovupd  zmm0, [rdi]
                                    • insertArguments()                      vaddpd   zmm0, zmm0, zmm1
                                    • lanewise(SIN)                          call     sleef_sin
```

1. **`impOps` is Always Emitted**: Binary `impOps` is the universal, language-agnostic IR.
2. **Combinator Tree Unrolling**: The Java engine parses `impOps` into a `MethodHandle` chain using `MethodHandles.foldArguments()`, `MethodHandles.filterArguments()`, and `MethodHandles.collectArguments()`.
3. **Monomorphic Inline Caching**: The vector math `func_id` is statically resolved at query bind time into a direct `MethodHandle` to `VectorOperators.SIN` or SVML.
4. **HotSpot C2 Native Machine Code**: C2 eliminates the interpreter loop and `switch` statement entirely, compiling the query into a single, straight-line AVX-512 loop matching C++ AOT speed.

---

## 6. Zero-Dependency Architectural Guarantee

Every component in the core kernel is strictly self-contained without third-party dependencies:

| Component | Java Engine (`impulse-graph-java`) | C++ Kernel (`impulse-cpp`) | Rust Kernel (`impulse-rust`) |
| :--- | :--- | :--- | :--- |
| **CEL Pratt Parser** | Pure Java standard library (`~300 LOC`) | Pure C++20 standard (`~350 LOC`) | Pure Rust `core` (`~300 LOC`) |
| **Vector Math Engine** | JDK 25 `jdk.incubator.vector` & FFM | Native `<immintrin.h>` & inlined FMAs | `core::arch::x86_64` intrinsics |
| **JIT Combinators** | JDK 25 `java.lang.invoke` built-in | Function pointer jump tables | Native assembly emission |
| **Memory Mappings** | JDK 25 `java.lang.foreign` (FFM) | Standard POSIX `mmap` / Win32 | Standard `libc::mmap` |
| **External Runtime Dependencies**| **0 External JARs (`pom.xml`)** | **0 External Shared Libraries** | **0 Runtime Crates (`Cargo.toml`)** |

*Heavy connectors (Kafka, Parquet, Debezium, AWS S3, Spring Boot, Protobuf) reside exclusively in `impulse-platform` and `impulse-graph-tooling`.*

---

## 7. Implementation Checklist & Action Items

### 7.1 Virtual Machine Opcodes (`impulse-graph-spec` & `impulse-graph-core`)
- [ ] Add `OP_CSC_WALK_FILTERED` (`0x1D`), `OP_CSC_WALK_REDUCE` (`0x1E`), `OP_CSC_WALK_REDUCE_SUM` (`0x1F`), `OP_CSC_WALK_DIRECT_STORE` (`0x87`).
- [ ] Add `OP_COO_WALK` (`0x86`), `OP_COO_WALK_FILTERED` (`0x8B`), `OP_COO_WALK_REDUCE` (`0x8C`), `OP_COO_WALK_DIRECT_STORE` (`0x8D`).
- [ ] Add `OP_DENSE_WALK` (`0x8E`), `OP_DENSE_WALK_BITMATRIX` (`0x8F`), `OP_DENSE_WALK_REDUCE` (`0x94`), `OP_DENSE_WALK_DIRECT_STORE` (`0x95`).
- [ ] Add `OP_VEC_CMP_*` (`0x20`..`0x23`), `OP_MASK_AND/OR/NOT` (`0x24`..`0x26`), `OP_VEC_BLEND` (`0x27`).
- [ ] Add `OP_VEC_MATH_UNARY` (`0x2D`), `OP_VEC_MATH_BINARY` (`0x2E`), `OP_VEC_MATH_TERNARY` (`0x2F`).
- [ ] Add `OP_GATHER_EDGE_ATTR` (`0x82`), `OP_GATHER_NODE_ATTR` (`0x81`), `OP_LOAD_COLUMN_VECTOR` (`0x80`), `OP_BRIN_ZONE_SKIP` (`0x83`).
- [ ] Add `OP_FIXPOINT_KLEENE_STAR` (`0x88`), `OP_SWAP_REG` (`0x89`), `OP_FRONTIER_DIFF` (`0x8A`).

### 7.2 Core C++ Kernel (`impulse-cpp`)
- [ ] Implement `math_ops.def` X-macro table linking all 42 math primitives to AVX-512 / ARM Neon / SLEEF vector routines.
- [ ] Implement self-contained zero-dependency C++20 CEL Pratt parser (`~350 LOC`).
- [ ] Implement OpenMP parallel loops for `CSC`, `COO`, and `DENSE_BITMATRIX` traversals.

### 7.3 Java 25 Engine (`impulse-graph-java`)
- [ ] Implement zero-dependency Java 25 CEL Pratt parser (`~300 LOC`).
- [ ] Implement `MethodHandle` Level 2 JIT Combinator compiler unrolling `impOps` into HotSpot C2 inlined pipelines.
- [ ] Bind 42 math functions to `jdk.incubator.vector.VectorOperators`.

### 7.4 Spec & Test Vectors (`impulse-graph-spec`)
- [ ] Add positive and negative test vectors in `test-vectors/vm-impas/` for all new opcodes.
- [ ] Verify 100% opcode test coverage across the multi-file threshold in `run_vm_asm_suite.py`.
