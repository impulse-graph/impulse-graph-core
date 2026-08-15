# Impulse Graph Engine — C++ Native Core Kernel & SDK (`impulse-cpp`)

The native C++20 zero-copy memory-mapped kernel and `ImpulseVM` bytecode execution engine for the **Impulse Graph Engine** (`impulse-graph`).

---

## 1. Overview & Architectural Highlights

`impulse-cpp` delivers ultra-high-performance graph query dispatch and analytics by eliminating physical data loading, GC pauses, and heap allocations:
* **Zero-Copy Memory-Mapped Snapshots (`.imps`)**: Graph topologies (CSR, CSC, COO, DENSE) and columnar attributes are mapped directly into user space via OS `mmap`.
* **Hardware SIMD Vectorization**: Vector operations use Google Highway with AVX-512, AVX2, and ARM Neon multi-target dispatch.
* **Direct-Threaded `ImpulseVM`**: Bytecode Virtual Machine executing `impOps` opcodes (`OP_CSR_WALK`, `OP_CSC_WALK`, `OP_MXV`, `OP_COLLECT_BITSET`) with register windowing and OpenMP intra-instruction parallelism.
* **Google CEL Query Engine**: Integrated zero-dependency Pratt expression parser compiling Common Expression Language (CEL) filters directly to vector instructions.
* **Zero External Runtime Dependencies**: The core kernel statically embeds Google Highway and requires 0 third-party shared libraries.

---

## 2. Prerequisites & Toolchain Requirements

| Component | Minimum Version | Notes |
| :--- | :--- | :--- |
| **C++ Compiler** | GCC 11+, Clang 14+, MSVC 2022+ | Requires full C++20 standard library support (`<bit>`, `<concepts>`, `<ranges>`). |
| **CMake** | 3.20+ | Standard build generator. |
| **OpenMP** | 3.1+ (Optional) | Multi-threaded intra-instruction parallelization for large graph traversals. |
| **Google Highway** | 1.2.0 (Automated) | Downloaded automatically via CMake `FetchContent` during initial configure. |

---

## 3. Building & Installing from Source

### Quick Build

```bash
# Configure release build inside build/ directory
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Compile all kernel targets and tools
cmake --build build -j
```

### Build Options

| CMake Option | Default | Description |
| :--- | :--- | :--- |
| `CMAKE_BUILD_TYPE` | `Release` | Build profile (`Release`, `Debug`, `RelWithDebInfo`). |
| `IMPULSE_BUILD_TESTS` | `ON` | Build the self-contained CTest unit & regression test suite. |
| `IMPULSE_BUILD_EXAMPLES` | `ON` | Build the standalone runnable example executables. |
| `IMPULSE_SANITIZE` | `OFF` | Enable AddressSanitizer (ASan) and UndefinedBehaviorSanitizer (UBSan). |

### System Installation

```bash
sudo cmake --install build --prefix /usr/local
```

This installs:
* Headers: `/usr/local/include/impulse_graph.h`, `impulse_vm.h`, `impulse_vm_fluent.hpp`, `impulse_compiler.hpp`
* Libraries: `/usr/local/lib/libimpulse_graph.so` (or `.dylib`) and `libimpulse_graph_static.a`
* CMake Package Config: `/usr/local/lib/cmake/ImpulseGraph/ImpulseGraphConfig.cmake`

---

## 4. Running the Self-Contained Test Suite

All unit and integration tests are **100% self-contained** inside this repository and run instantaneously without requiring external dataset downloads or network access:

```bash
# Run all 18 test harnesses via CTest
ctest --test-dir build --output-on-failure
```

To run a specific test suite individually:
```bash
./build/impulse_graph_test     # Snapshot format, header checksums & CRC16 validation
./build/simd_test               # Google Highway SIMD dot-product & bitset operations
./build/vm_test                 # ImpulseVM register allocation & opcode semantics
./build/vm_fluent_test          # QueryBuilder fluent C++ API pipeline
./build/test_cel_parser         # Google CEL expression parser & AST compiler
```

---

## 5. Running Examples & Sample Datasets

The `examples/` directory contains end-to-end sample programs demonstrating common usage patterns.

### Standard Sample Datasets Setup

Examples that load pre-built snapshots (`02_social_traversal`, `03_rbac_reachability`, `04_cel_transactions`) look for canonical `.imps` files in the directory specified by the **`IMPULSE_DATASETS_DIR`** environment variable:

1. **Download & Extract Sample Datasets**:
   ```bash
   # Download the sample datasets bundle from GitHub Releases
   curl -fsSLO https://github.com/impulse-graph/impulse-graph-core/releases/download/v0.9.0/impulse-sample-datasets.tar.gz
   tar -xzf impulse-sample-datasets.tar.gz -C ~/impulse-datasets/
   ```

2. **Export Environment Variable**:
   ```bash
   export IMPULSE_DATASETS_DIR=~/impulse-datasets
   ```

*(Note: If `IMPULSE_DATASETS_DIR` is unset, examples will check relative local fallback paths like `../../datasets`).*

### Running the Example Binaries

```bash
# 1. Self-Bootstrapping: Create snapshot with Writer and query with zero-copy mmap
./build/examples/01_snapshot_basics

# 2. Multi-Hop Traversal on Social Network Graph (social_graph.imps)
./build/examples/02_social_traversal

# 3. Relationship-Based Access Control (ReBAC) on rbac_snapshot.imps
./build/examples/03_rbac_reachability

# 4. Google CEL Filter & Vector Math on Financial Transactions (financial_transactions.imps)
./build/examples/04_cel_transactions
```

---

## 6. Integrating `impulse-cpp` into Your Project

### Option A: CMake `find_package` (Installed)

```cmake
cmake_minimum_required(VERSION 3.20)
project(my_graph_app CXX)

set(CMAKE_CXX_STANDARD 20)

find_package(ImpulseGraph REQUIRED)

add_executable(my_graph_app main.cpp)
target_link_libraries(my_graph_app PRIVATE ImpulseGraph::impulse_graph_static)
```

### Option B: CMake `FetchContent` (Direct from Git)

```cmake
include(FetchContent)
FetchContent_Declare(
    impulse_graph_core
    GIT_REPOSITORY https://github.com/impulse-graph/impulse-graph-core.git
    GIT_TAG        v0.9.0
    SOURCE_SUBDIR  impulse-cpp
)
FetchContent_MakeAvailable(impulse_graph_core)

target_link_libraries(my_graph_app PRIVATE impulse_graph_static)
```

---

## 7. C++ API Quick Reference

### Fluent `QueryBuilder` API Example

```cpp
#include "impulse_vm_fluent.hpp"
#include <iostream>

int main() {
    using namespace impulse::vm;

    // 1. Construct multi-hop query plan via fluent builder
    QueryBuilder builder;
    CompiledQuery query = builder
        .inputNode(0)            // R0: Seed node ID 0
        .walkEdge(1)             // R1: 1-hop CSR neighbors on relation 1
        .walkEdge(1)             // R2: 2-hop CSR neighbors on relation 1
        .collectBitset()         // R3: Collect destination frontier as bitset
        .compile();

    // 2. Open snapshot and execute
    impulse_status_t status = IMPULSE_OK;
    auto* snapshot = impulse_snapshot_open("graph.imps", &status);
    if (!snapshot || status != IMPULSE_OK) {
        std::cerr << "Failed to open snapshot\n";
        return 1;
    }

    QueryResult result = query.execute(snapshot, /*input_param=*/0);
    if (result.isOk()) {
        std::cout << "Query executed successfully. Result register: R" 
                  << result.result_register << "\n";
    }

    impulse_snapshot_close(snapshot);
    return 0;
}
```
