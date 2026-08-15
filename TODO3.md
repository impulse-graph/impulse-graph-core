# Impulse Graph Core — Compile-Time Tooling, Build Plugins & Typed State Roadmap (`TODO3.md`)

This document captures the implementation plan for the **Compile-Time CEL Validation Toolchain (`impulse-maven-plugin`, `impulse-gradle-plugin`, `impulse-apt`)**, **Generic Type-Safe State Evolution in the Fluent API**, and **Ahead-Of-Time (AOT) Query Bytecode Compilation**.

---

## 1. Compile-Time CEL Validation & Build Tooling

```
                       Compile-Time CEL Validation Pipeline
                                         │
        ┌────────────────────────────────┴────────────────────────────────┐
        ▼                                                                 ▼
1. JSR-269 Java Annotation Processor                2. impulse-maven-plugin
   (Instant Red Squigglies in IDE)                     (Build-Time CI/CD Verification)
   ───────────────────────────────                     ───────────────────────────────
   • Scans .filter("..."), .map("...")                 • Validates all CEL expressions in repo
   • Validates against @Node / @Edge types             • Fails build with exact line/col errors
   • Autocompletes schema field names                  • Pre-compiles CEL to binary .impb bytecode
```

### 1.1 Goals of `impulse-maven-plugin` & `impulse-gradle-plugin`
- [ ] **`validate-cel` Goal**:
  - Scans all source files in the project for `.filter("...")`, `.map("...")`, and `.reduce("...")` calls.
  - Parses CEL strings via the self-contained ~300 LOC Pratt parser.
  - Type-checks expression ASTs against declared `@Node` / `@EdgeRelation` schemas.
  - Halts the build (`BUILD FAILURE`) with exact line/column compiler diagnostics for:
    - Non-existent field names (e.g. `edge.milees` instead of `edge.miles`).
    - Type mismatches (e.g. comparing `Int32` status to `String` literal).
    - Invalid function calls (e.g. passing strings to `sin()`).
- [ ] **`generate-schema` Goal**:
  - Introspects Java records/classes and `.imps` schema metadata.
  - Generates strongly-typed schema helper classes (`Warehouse_`, `ShipmentEdge_`, `Customer_`) with static field tokens for IDE completion.
- [ ] **`aot-compile` Goal (Ahead-Of-Time ImpScheme S-Expression IR Generation)**:
  - Pre-parses all static CEL queries in the project during `mvn compile` into **ImpScheme S-Expression ASTs (`.impscm`)**.
  - Embeds the serialized ASTs directly into classfiles / JAR resources.
  - **Runtime Benefit**: Zero parsing latency ($0\,\text{ns}$) at runtime; when `query.bind(snapshot)` is called, the pre-built `ImpScheme` AST is instantly specialized against concrete `.imps` metadata to emit `impOps` / `MethodHandle` JIT combinators.

### 1.2 JSR-269 Java Annotation Processor (`impulse-apt`)
- [ ] Implement `ImpulseQueryAnnotationProcessor` running inside `javac`:
  - Provides **real-time red squiggly lines** inside IntelliJ IDEA, VS Code, and Eclipse when typing invalid CEL filter expressions.
  - Generates IDE diagnostic tooltips with suggested field name corrections ("Did you mean 'miles' (Float64)?").

---

## 2. Strongly Typed State Evolution in Fluent API

```java
// Generic Type Signature:
QueryBuilder<CurrentDomain, StateType>
```

### 2.1 Type Transition Mechanics
- [ ] **Phantom Type Tracking**:
  - `.from(Warehouse.class)` $\implies$ `QueryBuilder<Warehouse, Void>` (Pure Reachability).
  - `.attachState(new RouteState(0.0, 0))` $\implies$ `QueryBuilder<Warehouse, RouteState>`.
  - `.walk(ShipmentEdge.class, Customer.class)` $\implies$ `QueryBuilder<Customer, RouteState>`.
  - `.map(Double.class, "state.distance + edge.miles")` $\implies$ `QueryBuilder<Customer, Double>`.
  - `.collect()` $\implies$ Returns `NodeSet<Customer>` if `Void`, or `StateNodeSet<Customer, S>` if state-bearing.
- [ ] **Full IDE Autocomplete**:
  - Type inference in lambda closures: `(state, warehouse, shipment, customer) -> ...` automatically provides IDE completion for domain methods (`s.region()`, `e.miles()`, `d.dockType()`) and state fields (`state.distance()`).

### 2.2 Physical Off-Heap Structure-of-Arrays (SoA) Mapping
- [ ] `Void` state $\implies$ Zero state memory (pure 64-node word AVX-512 bitset).
- [ ] `Double` / `Float` state $\implies$ Contiguous 128-byte aligned primitive array of size $|\mathcal{V}_{\text{Dst}}|$.
- [ ] Custom record / tuple state $\implies$ Automatically flattened into parallel primitive vectors (Structure-of-Arrays) in off-heap memory, preserving AVX-512 SIMD vectorization.

---

## 3. Comprehensive Summary: The Unified Query Architecture

```
 User Interface Options                  Intermediate Representation                Hardware Execution
 ──────────────────────                  ───────────────────────────                ──────────────────
 1. Google CEL String                    ImpScheme S-Expression AST                 Vectorized impOps Bytecode
    .filter("edge.miles > 100.0")  ──>   (and (> (attr e :miles) 100.0)      ──>   OP_GATHER_EDGE_ATTR V1
                                              (== (attr d :status) 1))              OP_VEC_CMP_GT_F64   K1
 2. Strongly-Typed Java Lambdas                                                     OP_MASK_AND         K_ACTIVE
    .walk(..., (s, w, e, d) -> ...)                                                 (AVX-512 / ARM Neon / FFM)
                                                                                            │
 3. ImpLog Datalog (.implog)       ──>   Magic Sets / Semi-Naïve Fixpoint    ──>            ▼
    can_view(U, D) :- member(U, G)       OP_FIXPOINT_KLEENE_STAR (τ*)               HotSpot C2 / C++ Native
                                                                                    (Zero-Allocation Line Rate)
```

---

## 4. Implementation Checklist

### 4.1 Maven & Gradle Plugins (`impulse-graph-tooling` / `impulse-graph-java`)
- [ ] Create `impulse-maven-plugin` module under `impulse-graph-tooling` or `impulse-graph-java`.
- [ ] Implement `ValidateCelMojo` scanning source trees and validating CEL expressions against domain classes.
- [ ] Implement `AotCompileMojo` pre-compiling static queries to binary `.impb` artifacts.
- [ ] Implement `impulse-apt` JSR-269 annotation processor for instant IDE editor error highlighting.

### 4.2 Fluent API & Generic State Builder (`impulse-graph-java` / `impulse-cpp`)
- [ ] Refactor `QueryBuilder<D, S>` to carry generic state types across all traversal steps.
- [ ] Implement `StateNodeSet<D, S>` wrapping off-heap columnar vectors with typed iteration.
- [ ] Add overloaded `.map(Class<T>, String celExpr)` and `.filter(String celExpr)` methods.
- [ ] Implement `ImpulseCarrierThreadPool` in Java 25: pre-allocated static platform threads using deterministic `MemorySegment` range slicing and 0-heap allocation per hop (replacing heavy `ForkJoinPool`).

### 4.3 Core Kernel Zero-Dependency Parser (`impulse-graph-core`)
- [ ] Verify self-contained Pratt parser in C++20 (`~350 LOC`), Java 25 (`~300 LOC`), and Rust (`~300 LOC`).
- [ ] Enforce 0 third-party runtime dependencies across all core engine modules.
