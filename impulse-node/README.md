# Impulse Graph Engine Node.js & Bun Native SDK (`impulse-node`)

The official Node.js, Bun, and TypeScript SDK for **Impulse Graph Engine** (`impulse-graph`), providing zero-copy memory-mapped binary graph analytics, native C++20 bytecode VM bindings (`node-addon-api` / N-API), and high-throughput multi-hop traversals.

---

## 1. Features & Architectural Highlights

* **Zero-Copy Memory-Mapped Snapshots (`.imps`)**: Query multi-terabyte immutable binary graph files directly off-heap without V8 garbage collection pauses.
* **Native C++ Bytecode VM Execution**: Fast N-API bindings into the `ImpulseVM` register architecture and SIMD instruction set.
* **Fluent `QueryBuilder` & `Traversal` API**: Chain multi-hop edge walks, filters, and bitset collections in idiomatic JavaScript/TypeScript.
* **Zero Runtime Dependencies**: Powered entirely by N-API native bindings with zero external npm runtime dependencies.
* **TypeScript First**: Full TypeScript definitions included out of the box in `index.d.ts`.

---

## 2. Prerequisites & Building from Source

### Prerequisites

| Tool | Minimum Version | Notes |
| :--- | :--- | :--- |
| **Node.js** | 18+ | Tested on Node 18, 20, 22, and Bun. |
| **C++ Compiler** | GCC 11+, Clang 14+, MSVC 2022+ | C++20 standard required. |
| **node-gyp / npm** | Recent | `npm install -g node-gyp`. |

### Build from Source

```bash
cd impulse-graph-core/impulse-node

# Install development dependencies and build native add-on
npm install
npm run build
```

---

## 3. Running the Self-Contained Test Suite

All unit tests are **100% self-contained** and execute offline without external dataset dependencies:

```bash
# Run unit tests
npm test
```

Test coverage includes:
* Export definitions and ISA constants.
* Off-heap `VmContext` memory pools and bitset manipulations.
* `VmState` register frames and program counter.
* Programmatic `.imps` writing with `Writer`.
* Fluent `QueryBuilder` compilation and bytecode execution.
* Scalar and bitset VM instruction evaluations.

---

## 4. Running Examples & Sample Datasets

The `examples/` directory contains end-to-end Node.js sample scripts.

### Standard Sample Datasets Setup

Examples that load sample snapshots (`social_graph.imps`, `rbac_snapshot.imps`, etc.) automatically locate `.imps` files via the **`IMPULSE_DATASETS_DIR`** environment variable:

1. **Download & Extract Sample Datasets**:
   ```bash
   curl -fsSLO https://github.com/impulse-graph/impulse-graph-core/releases/download/v0.9.0/impulse-sample-datasets.tar.gz
   tar -xzf impulse-sample-datasets.tar.gz -C ~/impulse-datasets/
   ```

2. **Export Environment Variable**:
   ```bash
   export IMPULSE_DATASETS_DIR=~/impulse-datasets
   ```

*(Note: If `IMPULSE_DATASETS_DIR` is unset, examples automatically search local fallback paths or generate local sample graphs).*

### Running Examples

```bash
# 1. Snapshot creation and zero-copy inspection
node examples/01_snapshot_basics.js

# 2. Multi-hop traversal on social network graph
node examples/02_social_traversal.js

# 3. Relationship-Based Access Control (ReBAC) authorization
node examples/03_rbac_reachability.js

# 4. Low-level bytecode compilation & analytical VM queries
node examples/04_analytical_queries.js
```

---

## 5. JavaScript / TypeScript API Quick Reference

### Fluent Traversal Example

```javascript
const { Snapshot } = require('impulse-graph');

// Open snapshot with zero-copy mmap
const snap = new Snapshot('social_graph.imps');

// 2-Hop Friends-of-Friends traversal starting from User 0
const results = snap.traverse(0)
  .out(0) // 1-hop follows
  .out(0) // 2-hop follows
  .toArray();

console.log('2-Hop Reachable Users from User 0:', results);
snap.close();
```

### Snapshot Writer Example

```javascript
const { Writer } = require('impulse-graph');

const writer = new Writer('sample_graph.imps');
writer.addDomain(0, 4, 'User'); // Domain 0: User (Key Type: INT64)

// CSR: Node 0 -> [1, 2], Node 1 -> [2, 3], Node 2 -> [3], Node 3 -> []
const rowOffsets = new Uint32Array([0, 2, 4, 5, 5]);
const colIndices = new Uint32Array([1, 2, 2, 3, 3]);

writer.addRelation(
  0, // srcDomain
  0, // tgtDomain
  0, // encoding (CSR)
  4, // nodeCount
  5, // edgeCount
  0, // sectionFeatures
  rowOffsets,
  colIndices
);

writer.finalize();
```

## Asynchronous Non-Blocking Execution
Impulse Graph fully embraces the Node.js async idiom. By executing `ctx.executeAsync(bytecode)` instead of the synchronous variant, the V8 main thread immediately yields. The multi-terabyte C++ VM traversal executes entirely on the `libuv` background thread pool, completely unblocking your HTTP/WebSocket server event loop during massive network scans.

## Zero-Copy `ArrayBuffer` Views
Impulse returns `Float32Array` and `BigUint64Array` buffers without performing a single memory copy (`memcpy`). Memory maps are projected natively into V8 via `Napi::ArrayBuffer::New(external_data)`, offering raw bare-metal memory speeds directly in Javascript.
