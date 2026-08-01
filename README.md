# Impulse Graph Core (`impulse-graph-core`)

[![License](https://img.shields.io/badge/License-Apache%202.0-blue.svg)](LICENSE)

Polyglot native core engine workspace for **Impulse Graph Engine**. Houses the high-performance C++20 zero-copy memory-mapped kernel, the Rust core engine crate, and multi-language native FFI bindings.

## Subdirectories & Language Modules

* `impulse-cpp/`: High-performance C++20 zero-copy memory-mapped snapshot kernel with Ed25519 signature checks, portable SHA-256 header validation, and zero-copy C-ABI neighborhood sampler.
* `impulse-rust/`: Pure Rust engine crate implementing C-ABI Binary Snapshot v2.4 spec reader/writer.
* `impulse-python/`: Python PyTorch / PyG zero-copy `mmap` tensor integration and native C-ABI bindings.
* `impulse-dotnet/`: C# / .NET 9 FFI bindings for `libimpulse_graph`.
* `impulse-go/`: Go cgo bindings for `libimpulse_graph`.
* `impulse-node/`: Node.js / Bun N-API C++ native addon bindings.

## Ecosystem Repositories

* **[impulse-graph-spec](https://github.com/impulse-graph/impulse-graph-spec)**: Normative C-ABI Binary Snapshot v2.4 Specification and shared test vectors.
* **[impulse-graph-java](https://github.com/impulse-graph/impulse-graph-java)**: Java 25 FFM off-heap core engine implementation.
* **[impulse-graph-tooling](https://github.com/impulse-graph/impulse-graph-tooling)**: Developer utilities (`impulse-opt`, `impulse-inspect`, `impulse-compile`).

## Build Instructions

### C++ Kernel
```bash
cd impulse-cpp
cmake -B build && cmake --build build
```

### Rust Crate
```bash
cd impulse-rust
cargo test
```

## License

Apache License 2.0. See [LICENSE](LICENSE) for details.
