# Impulse Graph Engine — Cross-Language Fluent API Specification & Guide

> **Document Status**: Normative Ecosystem Standard  
> **Target Version**: `v0.9.0` / `v1.0.0`  
> **Applies To**: C++20, Rust, Python, Java 25, Node.js, C# (.NET)

---

## 1. Executive Summary & Architectural Philosophy

The **Impulse Graph Engine** provides a unified, type-safe **Fluent API** designed to construct, optimize, and execute graph traversals with **zero heap allocations** on hot paths and **sub-microsecond execution latencies**.

To satisfy both high-level application developers and low-level systems/compiler engineers, the Fluent API is partitioned into **two distinct tiers**:

```
┌────────────────────────────────────────────────────────────────────────────────────────┐
│                        Impulse Graph Fluent API Architecture                           │
├───────────────────────────────────────────┬────────────────────────────────────────────┤
│ Tier 1: High-Level Traversal Pipeline     │ Tier 2: Low-Level VM Query Assembler       │
│ (Declarative, Path & Hop Centric)         │ (Register-Based, Bytecode & Kernel Centric)│
├───────────────────────────────────────────┼────────────────────────────────────────────┤
│ • snapshot.traverse(seed)                 │ • qb = QueryBuilder()                      │
│ • .out("knows").in_("liked")              │ • qb.inputNode(0).walkEdge(relId, 1)       │
│ • .filter("edge.weight > 0.8")            │ • qb.afforest().matrixVectorMul(...)       │
│ • .to_list() / .count() / .contains()     │ • qb.collectBitset().compile()             │
├───────────────────────────────────────────┼────────────────────────────────────────────┤
│ Target: App Devs, Data Science, AuthZ/ReBAC│ Target: Engine Kernels, DSL Compilers, JIT │
└───────────────────────────────────────────┴────────────────────────────────────────────┘
```

---

## 2. Standardized Method Naming Matrix Across All Languages

To ensure developers experience a predictable, idiomatic API regardless of language, all method names follow strict cross-language mapping rules.

### Tier 1: High-Level Traversal Pipeline

| Traversal Operation | C++20 | Rust | Python | Java 25 | Node.js / TS | C# (.NET) |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **Start (Single Seed)** | `snap->traverse(42)` | `snap.traverse(42)` | `snap.traverse(42)` | `snap.traverse(42)` | `snap.traverse(42)` | `snap.Traverse(42)` |
| **Start (Batch Seeds)** | `snap->traverse({45,46,47})` | `snap.traverse(&[45,46,47])` | `snap.traverse([45,46,47])` | `snap.traverse(45, 46, 47)` | `snap.traverse([45,46,47])` | `snap.Traverse([45,46,47])` |
| **Forward Walk** | `out(rel)` | `out(rel)` | `out(rel)` | `out(rel)` | `out(rel)` | `Out(rel)` |
| **Incoming Walk** | `in_step(rel)` | `in_step(rel)` | `in_(rel)` | `in(rel)` / `inStep(rel)` | `in(rel)` | `In(rel)` |
| **CEL Attribute Filter** | `filter(expr)` | `filter(expr)` | `filter(expr)` | `filter(expr)` | `filter(expr)` | `Filter(expr)` |
| **Filtered Forward Walk** | `out_filtered(rel, expr)` | `out_filtered(rel, expr)` | `out(rel, filter=...)` | `outFiltered(rel, expr)` | `outFiltered(rel, expr)` | `OutFiltered(rel, expr)` |
| **Filtered Incoming Walk**| `in_filtered(rel, expr)` | `in_filtered(rel, expr)` | `in_(rel, filter=...)` | `inFiltered(rel, expr)` | `inFiltered(rel, expr)` | `InFiltered(rel, expr)` |
| **Bind Scalar Parameter**| `with_param(name, val)` | `with_param(name, val)` | `with_param(name, val)` | `withParam(name, val)` | `withParam(name, val)` | `WithParam(name, val)` |
| **Collect List / Array** | `to_vector()` | `to_vec()` | `to_list()` | `toList()` / `collectArray()` | `toArray()` | `ToList()` / `ToArray()` |
| **Collect Unique Set**   | `to_set()` | `to_hashset()` | `to_set()` | `toSet()` / `collectSet()` | `toSet()` | `ToHashSet()` |
| **Collect Node Count**   | `count()` | `count()` | `count()` | `count()` / `collectCount()` | `count()` | `Count()` |
| **Test Reachability**    | `contains(tgt)` | `contains(tgt)` | `contains(tgt)` | `contains(tgt)` | `contains(tgt)` | `Contains(tgt)` |
| **Emit Text Assembly**   | `to_impas()` | `to_impas()` | `to_impas()` | `toImpas()` | `toImpas()` | `ToImpas()` |

---

### Tier 2: Low-Level VM Bytecode Assembler (`QueryBuilder`)

| VM Assembler Step | C++20 | Rust | Python | Java 25 | C# (.NET) |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **Input Node Seed** | `inputNode(dstReg)` | `input_node(dst_reg)` | `input_node(dst_reg)` | `inputNode(dstReg)` | `InputNode(dstReg)` |
| **Input Set / BitSet**| `inputSet(dstReg)` | `input_set(dst_reg)` | `input_set(dst_reg)` | `inputSet(dstReg)` | `InputSet(dstReg)` |
| **Load Constant Int** | `loadConstInt(val, dst)` | `load_const_int(val, dst)` | `load_const_int(val, dst)` | `loadConstInt(val, dst)` | `LoadConstInt(val, dst)` |
| **Load Constant Float**| `loadConstFloat(val, dst)` | `load_const_float(val, dst)` | `load_const_float(val, dst)` | `loadConstFloat(val, dst)` | `LoadConstFloat(val, dst)` |
| **CSR Forward Walk** | `walkEdge(relId)` | `walk_edge(rel_id)` | `walk_edge(rel_id)` | `walkEdge(relId)` | `WalkEdge(relId)` |
| **CSC Incoming Walk**| `walkCsc(relId)` | `walk_csc(rel_id)` | `walk_csc(rel_id)` | `walkCsc(relId)` | `CscWalk(relId)` |
| **Degree Computation** | `walkDegree(relId)` | `walk_degree(rel_id)` | `walk_degree(rel_id)` | `walkDegree(relId)` | `Degree(relId)` |
| **Set Union** | `unionWith(srcReg)` | `union_with(src_reg)` | `union_with(src_reg)` | `unionWith(srcReg)` | `SetUnion(r1, r2, dst)` |
| **Set Intersection** | `intersectWith(srcReg)` | `intersect_with(src_reg)`| `intersect_with(src_reg)`| `intersectWith(srcReg)` | `SetIntersect(r1, r2, dst)` |
| **Set Difference** | `differenceWith(srcReg)` | `difference_with(src_reg)`| `difference_with(src_reg)`| `differenceWith(srcReg)` | `SetDifference(r1, r2, dst)` |
| **Set Cardinality** | `cardinality()` | `cardinality()` | `cardinality()` | `cardinality()` | `SetCardinality(src, dst)` |
| **Matrix-Vector Mul** | `matrixVectorMul(matReg)` | `matrix_vector_mul(mat)` | `matrix_vector_mul(mat)` | `matrixVectorMul(matReg)` | `MxV(relId, vecReg, dst)` |
| **Connected Comps** | `afforest()` | `afforest()` | `afforest()` | `afforest()` | `AfforestCC(relId, dst)` |
| **Sample Neighbors** | `sampleNeighbors(rel, k)` | `sample_neighbors(rel, k)`| `sample_neighbors(rel, k)`| `sampleNeighbors(rel, k)` | `SampleNeighbors(rel, k)` |
| **ReBAC Rule Check** | `rebacCheck(permId)` | `rebac_check(perm_id)` | `rebac_check(perm_id)` | `rebacCheck(permId)` | `RebacCheck(rel, tgtReg)` |
| **Collect BitSet** | `collectBitset()` | `collect_bitset()` | `collect_bitset()` | `collectBitset()` | `CollectBitset(reg)` |
| **Collect Array** | `collectArray()` | `collect_array()` | `collect_array()` | `collectArray()` | `CollectArray(reg)` |
| **Compile Bytecode** | `compile()` | `compile()` | `compile()` | `compile()` | `Compile()` |

---

## 3. Language-Specific Reserved Keyword Handling

Several languages have reserved keywords that collide with graph traversal directions (`in` in Python and Rust). The ecosystem enforces the following canonical conventions:

1. **Python**:
   - Forward walk: `.out("relation")`
   - Reverse walk: `.in_("relation")` (trailing underscore, following PEP 8 convention for reserved keywords).
2. **Rust**:
   - Forward walk: `.out("relation")`
   - Reverse walk: `.in_step("relation")` (avoids keyword collision while remaining clear).
3. **Java & TypeScript / Node.js**:
   - Forward walk: `.out("relation")`
   - Reverse walk: `.in("relation")` (`in` is valid as a method name in Java and property name in JS).
4. **C# (.NET)**:
   - Forward walk: `.Out("relation")`
   - Reverse walk: `.In("relation")` (PascalCase avoids keyword collision).

---

## 4. Parameter Binding & CEL Expression Conventions

Filter expressions use the industry-standard **Google CEL (Common Expression Language)** grammar.

### Parameter Syntax
- Named query parameters can be referenced inside CEL strings as `$param` or `@param`.
- When binding parameters via `.with_param("name", value)` (or `.withParam("name", value)`), the leading `$` or `@` is optional and stripped automatically.

```python
# Both syntaxes produce identical execution plans:
snap.traverse(101).out("transfers").filter("edge.amount > $min_val").with_param("min_val", 5000.0)
snap.traverse(101).out("transfers").filter("edge.amount > @min_val").with_param("@min_val", 5000.0)
```

### Supported CEL Identifiers on Traversals
- `src.<attr>`: Source node attributes on the active frontier.
- `edge.<attr>`: Relationship attributes along traversed edges.
- `tgt.<attr>`: Target candidate node attributes.
- Mathematical functions: `min(a, b)`, `max(a, b)`, `abs(x)`, `sqrt(x)`, `log(x)`, `clamp(x, low, high)`.

---

## 5. Input Frontier Flexibility: Single Seed vs. Batch Seeds & Domain Scoping

In Impulse Graph Engine, **dense node IDs are strictly per-domain ($0 \dots N_d-1$)**. In a multi-domain snapshot, traversal pipelines bind to a specific domain:

```python
# 1. Single Seed Node within a Domain:
snap.domain("User").traverse(42).out("knows").count()

# 2. Batch Sparse Node Array / List within a Domain:
snap.domain("User").traverse([45, 46, 47, 58]).out("knows").count()

# 3. Frontier Attribute Filtering (Evaluates against the active domain's attribute table):
snap.domain("User").traverse([1, 2, 3, 4]).filter("age > 5").to_list()

# 4. Zero-Hop Cardinality (Count of Initial Seed Frontier):
snap.domain("User").traverse([45, 46, 47, 58]).count()  # Returns 4
```

*(Note: On single-domain snapshots, `snap.traverse(...)` defaults directly to domain 0).*

### Execution Mechanics of Batch Frontiers:
1. **Domain-Bound Attribute Evaluation**: Calling `.filter("age > 5")` evaluates the `age` attribute array belonging specifically to the active domain (`User.age`) across candidate dense IDs $\{1, 2, 3, 4\}$ via SIMD predicates and prunes non-matching nodes before (or without) walking edges.
2. **Multi-Seed Parallel Walk & Relation Domain Transition**: When followed by traversal steps (`.out("PURCHASED")`), the relation descriptor's `SrcDomainID` and `TgtDomainID` map the surviving `User` dense IDs to the target domain's dense ID space (e.g. `WineInventory`), running vectorized SIMD chunks in parallel without loop overhead.

---

## 6. End-to-End Idiomatic Examples

### 5.1 Python Example
```python
import impulse_graph as imp

# Zero-copy memory-map snapshot
snapshot = imp.Snapshot.open("hetionet.imps")

# High-Level Fluent Traversal: Compound -> Gene -> Disease
treatments = (
    snapshot.traverse(start_node=1053)      # Aspirin
    .out("Compound::binds::Gene")            # Targets
    .in_("Disease::associates::Gene")        # Associated Diseases
    .filter("tgt.severity > $min_sev")       # CEL filter
    .with_param("min_sev", 3.5)
    .to_set()
)

print(f"Reachable disease indications: {len(treatments)}")
```

### 5.2 Java 25 Example
```java
import org.impulsegraph.api.*;
import org.impulsegraph.storage.csr.BinarySnapshotLoader;

var snapshot = BinarySnapshotLoader.load("social_graph.imps");

// High-Level Fluent ReBAC Access Check
boolean hasAccess = snapshot.traverse(userId)
    .out("memberOf")                         // User -> Group
    .outFiltered("hasPermission", "edge.scope == @scope") // Group -> Perm
    .withParam("scope", "admin:write")
    .contains(targetResourceId);

System.out.println("Access granted: " + hasAccess);
```

### 5.3 Rust Example
```rust
use impulse_graph::reader::SnapshotReader;

let reader = SnapshotReader::open("security_graph.imps")?;

// Fluent multi-hop traversal with candidate count
let reachable_hosts = reader.traverse(seed_user_id)
    .out("member_of")
    .out("assigned_role")
    .out_filtered("can_ssh", "edge.port == 22")
    .to_vec()?;

println!("Total reachable hosts: {}", reachable_hosts.len());
```

### 5.4 Node.js / TypeScript Example
```typescript
import { Snapshot } from 'impulse-graph';

const snapshot = Snapshot.open('e_commerce.imps');

// Multi-hop product recommendation
const recommendations = snapshot.traverse(customerNodeId)
  .out('PURCHASED')
  .in('PURCHASED')
  .outFiltered('CONTAINS', 'edge.rating >= 4.5')
  .toArray();

console.log(`Recommended product IDs: ${recommendations.length}`);
```

### 5.5 C# (.NET) Example
```csharp
using ImpulseGraph;

using var snapshot = Snapshot.Open("powergrid.imps");

// 2-hop grid contingency walk
var islandedBuses = snapshot.Traverse(seedBusId)
    .Out("Branch")
    .Filter("edge.status == 1 && edge.kv >= @minKv")
    .WithParam("minKv", 115.0)
    .ToHashSet();

Console.WriteLine($"Active energized buses: {islandedBuses.Count}");
```

---

## 6. Execution Lifecycle & Performance Guidelines

1. **Lazy Bytecode Compilation**:
   - Calling `.out()`, `.in_()`, `.filter()` records AST step descriptors without executing traversals.
   - Calling terminal collectors (`.to_list()`, `.count()`, `.contains()`) triggers bytecode emission and execution in **one fused zero-allocation pass**.
2. **Re-Usable Query Objects**:
   - For high-frequency query endpoints (e.g. 100,000 requests/sec), call `.compile()` to produce an immutable `CompiledQuery`.
   - Re-execute the compiled query across worker threads by passing different seed node IDs into `.execute(seed)` with **0ns recompilation overhead**.
