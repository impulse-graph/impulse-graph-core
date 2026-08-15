# Impulse Graph Engine Core Rust Crate (`impulse-rust`)

The official Rust engine crate for **Impulse Graph Engine** (`impulse-graph`), delivering zero-copy memory-mapped graph analytics, pure zero-dependency C-ABI binary parsing, Structure-of-Arrays (SoA) attribute accessors, and single-pass binary snapshot streaming.

---

## 1. Features & Architectural Highlights

* **Zero-Copy Memory-Mapped Snapshots (`.imps`)**: Off-heap graph reading via native OS virtual memory (`mmap`).
* **Zero Runtime Dependencies**: Strictly zero third-party crate dependencies per ecosystem architectural rules (`AGENTS.md §3.2`).
* **Hardware Alignment**: 128-byte aligned sections optimized for AVX-512 SIMD vectorization and GPU Direct Storage (`cuFile`).
* **Structure of Arrays (SoA) Attributes**: Direct typed slice accessors for columnar floats, integers, and fixed-width vector embeddings.
* **Fluent Multi-Hop Traversal Pipeline**: High-speed typed traversal and neighbor expansion (`traverse(start_node).out("relation")`).
* **Single-Pass Snapshot Writer**: Create binary snapshot files directly from Rust streaming pipelines.

---

## 2. Prerequisites & Building from Source

### Prerequisites

| Tool | Minimum Version | Notes |
| :--- | :--- | :--- |
| **Rust Toolchain** | 1.70+ | `rustc` and `cargo` (Edition 2021). |

### Build from Source

```bash
cd impulse-graph-core/impulse-rust

# Build release artifacts
cargo build --release
```

---

## 3. Running the Self-Contained Test Suite

All unit tests are **100% self-contained** in the crate and execute completely offline without requiring dataset downloads:

```bash
# Run unit tests
cargo test
```

Test coverage includes:
* `test_sha256_known_vector` / `test_crc16_known_vector`: Cryptographic baseline checksum verification.
* `test_v09_roundtrip_write_read`: Full binary serialization and deserialization roundtrip.
* `test_rust_fluent_traversal_multihop`: Multi-hop graph traversal.
* `test_rust_simd_*`: Native SIMD dot products and sorted intersection vector math.

---

## 4. Running Examples & Sample Datasets

The `examples/` directory contains standalone Rust examples demonstrating real-world workflows.

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
cargo run --example 01_snapshot_basics

# 2. Multi-hop traversal on social network graph
cargo run --example 02_social_traversal

# 3. Relationship-Based Access Control (ReBAC) authorization
cargo run --example 03_rbac_reachability

# 4. Attribute vectors, SoA slices, and analytical SIMD math
cargo run --example 04_analytical_vectors
```

---

## 5. Rust API Quick Reference

### Fluent Traversal Pipeline

```rust
use impulse_graph::SnapshotReader;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    // Open snapshot with zero-copy mmap
    let reader = SnapshotReader::open("social_graph.imps")?;

    // 2-Hop Traversal: Seed(0) -> follows -> follows
    let reachable_users: Vec<u64> = reader
        .traverse(0)
        .out("follows")
        .out("follows")
        .to_vec();

    println!("2-Hop Reachable Users from Node 0: {:?}", reachable_users);
    Ok(())
}
```

### Snapshot Writer

```rust
use impulse_graph::{SnapshotWriter, KeyType};

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let mut writer = SnapshotWriter::new("my_graph.imps");
    writer.add_domain(0, KeyType::Int64, "User");

    // CSR: Node 0 -> [1, 2], Node 1 -> [2, 3], Node 2 -> [3], Node 3 -> []
    let row_offsets = vec![0u32, 2, 4, 5, 5];
    let col_indices = vec![1u32, 2, 2, 3, 3];

    writer.add_relation(0, 0, 4, 5, row_offsets, col_indices);
    writer.finalize()?;

    println!("Successfully wrote immutable snapshot.");
    Ok(())
}
```
