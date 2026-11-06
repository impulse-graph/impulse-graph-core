# Impulse Graph Tooling (`impulse-graph-tooling`)

[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE)

Unified developer utilities, dataset snapshot builder, DSL compiler suite, layout optimizer, synthetic graph data generator, and execution runtime for the **Impulse Graph Engine** (Spec v0.9.0).

- **Canonical Binary**: `impulse-graph`
- **Symlink / CLI Alias**: `impulse`

---

## Table of Contents

- [Quick Start](#quick-start)
- [Building Binary Snapshots (`impulse build`)](#building-binary-snapshots-impulse-build)
  - [Manifest Schema Specification](#manifest-schema-specification)
  - [Supported Input Formats](#supported-input-formats)
  - [Structure-of-Arrays (SoA) Attribute Encoding](#structure-of-arrays-soa-attribute-encoding)
  - [Step-by-Step Build Examples](#step-by-step-build-examples)
- [Graph Data Generator (`impulse generate`)](#graph-data-generator-impulse-generate)
  - [Supported Topology Profiles](#supported-topology-profiles)
  - [Generator CLI Examples](#generator-cli-examples)
- [CLI Command Reference](#cli-command-reference)
  - [Snapshot Operations](#snapshot-operations)
  - [Compiler & VM Operations](#compiler--vm-operations)
  - [Cryptographic Verification](#cryptographic-verification)
- [DSL Compiler Suite & Pipeline](#dsl-compiler-suite--pipeline)
- [Building & Installation](#building--installation)
- [Docker Container](#docker-container)
- [License](#license)

---

## Quick Start

### 1. Build the Tooling CLI

```bash
cargo build --release
export PATH="$PWD/target/release:$PATH"
```

### 2. Build an Immutable Snapshot from a Manifest

Build a zero-copy `.imps` binary snapshot from a dataset manifest and edge lists:

```bash
impulse build manifest.json graph.imps
```

### 3. Generate a Synthetic Benchmark Graph

Generate a Graph500 (R-MAT) benchmark graph with $2^{12} = 4,096$ vertices, edge factor $16$ ($65,536$ edges), and synthetic float32 weights directly into an immutable `.imps` binary snapshot:

```bash
impulse generate \
  --profile graph500 \
  --scale 12 \
  --edge-factor 16 \
  --attributes "weight:f32,timestamp:i64" \
  -o graph500_s12.imps
```

### 4. Inspect Snapshot Topology & Schema

Inspect binary header metadata, domain catalogs, CSR relation tables, and attribute nullability:

```bash
impulse inspect graph.imps
```

### 5. Compute Topology, Multiplicity & CBO Statistics

Compute degree percentiles ($P_{50}, P_{90}, P_{99}$), supernodes, multiplicity classification, attribute zone maps, and CBO sketches:

```bash
impulse stats graph.imps
```

### 6. Validate Spec v0.9.0 Compliance

Verify header checksums, 128-byte hardware memory alignment, and CSR array bounds:

```bash
impulse snapshot validate graph.imps
```

### 7. Optimize Snapshot Layout

Apply Reverse Cuthill-McKee (RCM) bandwidth reduction and convert CSR column indices to Delta-VByte encoding for optimal L1/L2 cache locality:

```bash
impulse snapshot optimize \
  --input graph.imps \
  --output graph_rcm.imps \
  --rcm \
  --encoding delta_vbyte
```

---

## Building Binary Snapshots (`impulse build`)

The `impulse build` (or `impulse snapshot build`) command ingests raw graph datasets (TSV, CSV, or Apache Parquet) described by a JSON manifest and compiles them into a single, immutable, zero-copy `.imps` binary snapshot file adhering to the **Impulse Binary Snapshot Specification v0.9.0**.

```bash
impulse build <MANIFEST.json> <OUTPUT.imps>
```

### Manifest Schema Specification

A build manifest (`manifest.json`) defines the domain catalog, edge relations, input file sources, and optional typed property attributes:

```json
{
  "version": "0.9.0",
  "domains": [
    {
      "id": 0,
      "name": "User",
      "key_type": "string",
      "file": "users.parquet",
      "format": "parquet",
      "id_column": "user_id",
      "attributes": [
        { "name": "age", "column": "age", "type": "int32" }
      ]
    },
    {
      "id": 1,
      "name": "Document",
      "key_type": "string"
    }
  ],
  "relations": [
    {
      "name": "CAN_VIEW",
      "src_domain": 0,
      "tgt_domain": 1,
      "file": "permissions.parquet",
      "format": "parquet",
      "src_column": "user_id",
      "tgt_column": "doc_id",
      "attributes": [
        { "name": "weight", "column": "weight", "type": "float32" },
        { "name": "created_at", "column": "timestamp", "type": "int64" }
      ]
    }
  ]
}
```

#### Manifest Fields Reference

| Field | Location | Type | Description |
| :--- | :--- | :--- | :--- |
| `version` | Top-level | `string` | Snapshot format version target (e.g. `"0.9.0"`). |
| `domains` | Top-level | `array` | List of Node Domain definitions. |
| `domains[].id` | Domain | `uint16` | Dense 0-indexed Domain ID ($0, 1, \dots$). |
| `domains[].name` | Domain | `string` | Canonical domain name (e.g. `"User"`, `"Document"`). |
| `domains[].key_type` | Domain | `string` | Primitive key type: `"string"`, `"uint64"`, `"int64"`, `"uuid"`, `"int32"`. |
| `domains[].file` | Domain | `string` (opt) | Optional node catalog file for predefined nodes & isolated degree-0 vertices. |
| `domains[].format` | Domain | `string` (opt) | Format of node file: `"tsv"`, `"csv"`, `"parquet"`. |
| `domains[].id_column` | Domain | `string` (opt) | Primary ID column in node file (default: `"id"`). |
| `relations` | Top-level | `array` | List of directed edge relation definitions. |
| `relations[].name` | Relation | `string` (opt) | Relation descriptor name (e.g. `"FOLLOWS"`, `"AUTHORED"`). |
| `relations[].src_domain` | Relation | `uint16` | Source Domain ID. |
| `relations[].tgt_domain` | Relation | `uint16` | Target Domain ID. |
| `relations[].file` | Relation | `string` | Path to edge dataset file (relative to manifest directory). |
| `relations[].format` | Relation | `string` (opt) | Edge file format: `"tsv"`, `"csv"`, `"parquet"`. |
| `relations[].src_column` | Relation | `string` (opt) | Source node column name in Parquet/CSV (default: `"src"`). |
| `relations[].tgt_column` | Relation | `string` (opt) | Target node column name in Parquet/CSV (default: `"tgt"`). |
| `relations[].attributes` | Relation | `array` (opt) | List of Structure-of-Arrays (SoA) edge attribute descriptors. |

---

### Supported Input Formats

1. **TSV / CSV Edge Lists**:
   - Whitespace-delimited (TSV) or comma-delimited (CSV) text files.
   - Lines beginning with `#`, `//`, or `%` are automatically treated as comments and ignored.
   - Automatic discovery and dense 0-indexed ID mapping of all source and target keys.
2. **Apache Parquet (`.parquet`, `.pq`)**:
   - High-throughput zero-copy columnar ingestion using `parquet` and `arrow`.
   - Supports typed String, Int32, Int64, Float32, and Float64 columns for nodes, edges, and properties.

---

### Structure-of-Arrays (SoA) Attribute Encoding

Edge and node attributes are stored off-heap in contiguous, 128-byte hardware-aligned Structure-of-Arrays (SoA) memory buffers:

| Attribute Type Code | Manifest Type Name | Physical Representation |
| :--- | :--- | :--- |
| `0x01` | `"int32"`, `"i32"`, `"uint32"` | 32-bit signed/unsigned integer (`4` bytes/edge) |
| `0x02` | `"int64"`, `"i64"`, `"uint64"` | 64-bit integer or UNIX timestamp (`8` bytes/edge) |
| `0x03` | `"float32"`, `"f32"`, `"float"` | IEEE 754 32-bit single-precision float (`4` bytes/edge) |
| `0x04` | `"float64"`, `"f64"`, `"double"` | IEEE 754 64-bit double-precision float (`8` bytes/edge) |
| `0x05` | `"string"`, `"str"`, `"utf8"` | Null-terminated string pool with 32-bit index offsets |

---

### Step-by-Step Build Examples

#### Example 1: Single-Domain Social Network (TSV)

1. Create an edge TSV file `follows.tsv`:
```tsv
# user_A -> user_B
alice	bob
alice	charlie
bob	charlie
charlie	alice
```

2. Create `manifest.json`:
```json
{
  "version": "0.9.0",
  "domains": [
    { "id": 0, "name": "User", "key_type": "string" }
  ],
  "relations": [
    { "src_domain": 0, "tgt_domain": 0, "file": "follows.tsv", "name": "FOLLOWS" }
  ]
}
```

3. Build and validate the snapshot:
```bash
impulse build manifest.json social_graph.imps
impulse inspect social_graph.imps
impulse snapshot validate social_graph.imps
```

---

#### Example 2: Multi-Domain Enterprise ReBAC Graph (TSV)

1. Create relation files:
   - `user_group.tsv`: Mapping Users to Groups (`user_1	security_team`)
   - `group_doc.tsv`: Mapping Groups to Documents (`security_team	doc_alpha`)

2. Create `rebac_manifest.json`:
```json
{
  "version": "0.9.0",
  "domains": [
    { "id": 0, "name": "User", "key_type": "string" },
    { "id": 1, "name": "Group", "key_type": "string" },
    { "id": 2, "name": "Document", "key_type": "string" }
  ],
  "relations": [
    { "src_domain": 0, "tgt_domain": 1, "file": "user_group.tsv", "name": "MEMBER_OF" },
    { "src_domain": 1, "tgt_domain": 2, "file": "group_doc.tsv", "name": "CAN_VIEW" }
  ]
}
```

3. Build and inspect:
```bash
impulse build rebac_manifest.json rebac.imps
impulse stats rebac.imps
```

---

#### Example 3: Direct Streaming to Cloud Storage via stdout

Stream snapshot generation direct to stdout (`-`) without creating intermediate disk files, perfect for containerized CI/CD or direct S3 uploads:

```bash
impulse build manifest.json - | aws s3 cp - s3://my-graph-bucket/snapshots/prod.imps
```

---

## Graph Data Generator (`impulse generate`)

The `generate` subcommand synthesizes zero-copy `.imps` binary snapshots, TSV edge lists, or CSV files across 7 realistic and benchmark topology profiles.

```bash
impulse generate --profile <PROFILE> [OPTIONS] -o <OUTPUT>
```

### Supported Topology Profiles

| Profile Name | Model / Algorithm | Primary Use Case | Key CLI Parameters |
| :--- | :--- | :--- | :--- |
| **`graph500`** (or `rmat`, `kronecker`) | Kronecker / R-MAT recursive quadrant power law | Graph500 benchmark traversals (BFS, SSSP, PageRank) | `-s, --scale <U8>` ($N = 2^S$ vertices)<br>`-e, --edge-factor <U32>` (edges/vertex, default `16`)<br>`--a`, `--b`, `--c`, `--d` (initiator probabilities) |
| **`social`** (or `barabasi`) | Barabási–Albert preferential attachment | Scale-free social follow networks ($P(k) \sim k^{-3}$) | `-n, --nodes <U64>` (total vertices)<br>`--edges-per-node <U32>` (attachment degree, default `8`) |
| **`social-sbm`** (or `communities`) | Stochastic Block Model (SBM) | Clustered social circles & community structure | `-n, --nodes <U64>`<br>`-k, --communities <U32>` (cluster count)<br>`--p-intra <F64>` (intra-cluster probability)<br>`--p-inter <F64>` (inter-cluster probability) |
| **`erdos-renyi`** (or `random`) | Erdős–Rényi uniform random $G(N, M)$ / $G(N, p)$ | Baseline random graph verification | `-n, --nodes <U64>`<br>`-m, --edges <U64>` (or `--p <F64>`) |
| **`grid`** (or `mesh`, `lattice`) | 2D / 3D regular spatial mesh | Power grids, PDE meshes, spatial networks | `--dim-x <U32>`, `--dim-y <U32>`, `--dim-z <U32>`<br>`--toroidal` (periodic boundary wrap-around) |
| **`tree`** (or `hierarchy`) | Balanced $K$-ary tree or star topology | Hierarchies, DAG reachability, ReBAC taxonomies | `--branching <U32>` (branching factor $K$)<br>`--depth <U32>` (tree depth $D$)<br>`--star` (single hub star) |
| **`bipartite`** | Multi-domain 2-mode graph | Multi-domain relations (Users $\rightarrow$ Documents) | `--src-nodes <U64>`, `--tgt-nodes <U64>`, `-m, --edges <U64>` |

### Generator CLI Examples

#### Generate a Social Network with Community Clusters
```bash
impulse generate \
  --profile social-sbm \
  --nodes 50000 \
  --communities 20 \
  --p-intra 0.01 \
  --p-inter 0.0001 \
  --domain-name "User" \
  --relation-name "FRIENDS_WITH" \
  -o social_sbm.imps
```

#### Generate a Multi-Domain Bipartite Graph for Authorization Testing
```bash
impulse generate \
  --profile bipartite \
  --src-nodes 10000 \
  --tgt-nodes 50000 \
  --edges 200000 \
  --domain-name "User" \
  --tgt-domain-name "Document" \
  --relation-name "CAN_ACCESS" \
  -o rebac_bipartite.imps
```

---

## CLI Command Reference

### Snapshot Operations

* **`impulse build <MANIFEST> <OUTPUT>`**: Compile `.imps` binary snapshot from a JSON manifest and dataset files (TSV, CSV, Parquet).
* **`impulse generate`**: Synthesize `.imps` snapshots, TSV, or CSV edge lists across 7 graph topology profiles.
* **`impulse inspect <SNAPSHOT>`**: Output human-readable or JSON details of Page 0 header, section offsets, domain catalogs, Section 2.6 Index Tables, and attribute nullability.
* **`impulse stats <SNAPSHOT>`**: Compute structural graph statistics, in/out degree percentiles ($P_{50}, P_{90}, P_{99}$), supernode hub classification, relation multiplicity ($1:1, M:1, 1:M, M:M$), attribute zone maps, and Cost-Based Optimizer (CBO) sketches.
* **`impulse snapshot validate <SNAPSHOT>`**: Verify 128-byte hardware alignment, monotonic CSR row offsets, column index range bounds, duplicate relation rejection, and metadata integrity.
* **`impulse snapshot optimize <OPTIONS>`**: Offline topology optimizer:
  - `--rcm`: Apply Reverse Cuthill-McKee vertex renumbering to reduce matrix bandwidth.
  - `--degree-sort`: Sort nodes by descending degree for L1/L2 cache locality on hub nodes.
  - `--encoding <raw_uint32|delta_vbyte|simdcomp|sliced_ellpack>`: Compress CSR column indices.
  - `--csc`: Generate reverse Compressed Sparse Column auxiliary index sections.
* **`impulse snapshot diff <BASE> <TARGET>`**: Compare schema, domain catalogs, and structural edge diffs between two `.imps` snapshots.
* **`impulse snapshot export <SNAPSHOT> --out-dir <DIR> --format <tsv|csv|jsonl>`**: Export snapshot relations back to edge list files.
* **`impulse snapshot merge --base <BASE> --deltas <DELTAS...> --output <OUT>`**: Ingest WAL delta log files (`.impdelta`) to produce an updated snapshot.
* **`impulse snapshot convert-tensors --input <MODEL.safetensors> --output <OUT.imps>`**: Ingest raw weight tensors from HuggingFace `.safetensors` files directly into `.imps` property blocks.

### Compiler & VM Operations

* **`impulse compile <INPUT> [-o <OUTPUT>]`**: Compile high-level DSL sources (`.impk`, `.implog`, `.impscm`, `.cypher`) into `.impas` assembly or `.impb` binary bytecode.
* **`impulse assemble -i <INPUT.impas> -o <OUTPUT.impb>`**: Assemble textual assembly into binary opcode bytecode.
* **`impulse disassemble -i <INPUT.impb>`**: Disassemble binary bytecode into annotated assembly text.
* **`impulse run --snapshot <SNAPSHOT.imps> --bytecode <QUERY.impb> --input-val <ROOT_ID>`**: Execute compiled query bytecode directly against a memory-mapped binary snapshot.

### Cryptographic Verification

* **`impulse crypto keygen --out <KEY_PREFIX>`**: Generate Ed25519 public/private keypair (`.pub`, `.priv`).
* **`impulse crypto sign <SNAPSHOT.imps> --key <KEY.priv>`**: Compute SHA-256 digest over snapshot data sections and produce an Ed25519 signature (`.sig`).
* **`impulse crypto verify <SNAPSHOT.imps> --key <KEY.pub>`**: Verify cryptographic authenticity of a signed snapshot file.

---

## DSL Compiler Suite & Pipeline

The compiler translates frontends into `ImpulseVM` bytecode (`impOps`):

```
+--------------------+   +--------------------+
|  ImpK (.impk)      |   |  ImpLog (.implog)  |
|  Matrix & Vector   |   |  Datalog & ReBAC   |
+---------+----------+   +---------+----------+
          |                        |
          +----------+  +----------+
                     |  |
                     v  v
        +----------------------------+
        |  ImpScheme (.impscm)       |
        |  Universal S-Expr AST IR   |
        +-------------+--------------+
                      |
                      v
        +----------------------------+
        |  ImpAsm (.impas)           |
        |  Text Assembly Format      |
        +-------------+--------------+
                      |
                      v
        +----------------------------+
        |  impOps (.impb)            |
        |  VM Bytecode Binary ISA    |
        +----------------------------+
```

* **`ImpK` (`.impk`)**: Matrix math, PageRank, connected components, SIMD vector math.
* **`ImpLog` (`.implog`)**: Declarative Datalog rules, Relationship-Based Access Control (ReBAC / Zanzibar), transitive reachability.
* **`ImpScheme` (`.impscm`)**: Homoiconic S-Expression AST compiler Intermediate Representation (IR).
* **`ImpAsm` (`.impas`)**: Register-based assembly format (`R0`..`R63`).
* **`impOps` (`.impb`)**: Bytecode instruction set architecture (opcodes `0x01`..`0x95`).

---

## Building & Installation

### Prerequisites
- Rust 1.75+ (Cargo)
- CMake 3.20+ (for C-ABI engine kernel integration)

### Build Steps

```bash
# Debug build
cargo build

# Release build with optimizations
cargo build --release

# Run comprehensive test suite
cargo test
```

---

## Docker Container

```bash
# Build the Docker image
docker build -t ghcr.io/impulse-graph/impulse-tools:latest .

# Run the CLI container
docker run --rm -v "$PWD:/data" ghcr.io/impulse-graph/impulse-tools:latest inspect /data/snapshot.imps
```

---

## License

Apache License 2.0. See [LICENSE](LICENSE) for details.
