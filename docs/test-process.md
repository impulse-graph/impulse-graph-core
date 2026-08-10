# Impulse Graph Engine — Core Testing Process & Strategy

This document formalizes the multi-tier testing process, PR gate requirements, and specification compliance strategy for **Impulse Graph Engine** (`impulse-graph-core`).

---

## 1. Executive Summary & Testing Philosophy

Testing across Impulse Graph Engine follows a **5-tier testing pyramid**, spanning from language-agnostic binary layout specifications up to cloud infrastructure and empirical performance verification.

```
                    ┌──────────────────────────────────────────┐
                    │ Level 4: Empirical Benchmarks & CI/CD    │  Macro Level
                    │ (JMH, Google Bench, SLSA L3 Attestations)│
                    ├──────────────────────────────────────────┤
                    │ Level 3: Platform Cloud & Showcases      │
                    │ (Kafka WAL, gRPC, Powergrid, AuthZ ReBAC)│
                    ├──────────────────────────────────────────┤
                    │ Level 2: DSL Compilers & Tooling         │
                    │ (ImpK / ImpLog -> ImpScheme -> impOps)   │
                    ├──────────────────────────────────────────┤
                    │ Level 1: Language Engine Kernels & FFI   │
                    │ (C++20, Rust, Java 25 FFM, Python, Go)   │
                    ├──────────────────────────────────────────┤
                    │ Level 0: Agnostic Spec & VM ISA Vectors  │  Micro Level
                    │ (tc01..tc36, vm-impas, 100% Coverage)    │
                    └──────────────────────────────────────────┘
```

### Core Principles
1. **Language-Agnostic Single Source of Truth**: The specification repository (`impulse-graph-spec`) defines canonical test vectors (`tc01`..`tc36`) and Virtual Machine assembly scripts (`vm-impas`) decoupled from any single programming language.
2. **Separation of Engine Logic vs. FFI Marshaling**: Kernel logic bugs are tested directly against compiled native libraries (`impulse-cpp`, `impulse-rust`), while binding tests (Python, Go, Node.js, C#) focus strictly on FFI boundary safety, pointer sizing, struct alignment, and memory lifecycle.
3. **Mandatory PR Gates**: No Pull Request may be merged to `main` without passing the full engine spec suite and the multi-language FFI contract suite.

---

## 2. Level 0: Normative Spec & ISA Test Vectors (`impulse-graph-spec`)

The `impulse-graph-spec` repository provides the foundational test suite.

### 2.1 Physical Binary Snapshot Vectors (`tc01` – `tc36`)
* **Positive Vectors (`tc01`–`tc16`, `tc19`–`tc24`, `tc31`)**: Validates 4KB Page 0 alignment, 128-byte hardware vector alignment, CSR/CSC layouts, raw/delta-vbyte/simdcomp encodings, key types (UUID128, string, int64), and Structure-of-Arrays (SoA) attribute fields.
* **Negative & Corruption Vectors (`tc17`–`tc18`, `tc25`–`tc30`, `tc32`–`tc36`)**: Validates SHA-256 binary corruption traps, unsupported feature flags, out-of-bounds section offsets, missing UTF-8 null terminators, and payload truncation handling.

### 2.2 Self-Contained VM Assembly Vectors (`vm-impas`)
Assembly scripts in `test-vectors/vm-impas/` are self-contained `.impas` files that embed test expectations directly in structured comment headers:

```asm
; ====================================================================
; TC01: Scalar Operations & Indirect Loads (Positive Test)
; ====================================================================
; {EXPECT: STATUS = IMPULSE_VM_OK}
; {EXPECT: R0 = 42}
; {EXPECT: R1 = 100}
; {EXPECT: R3 = 42}
; {EXPECT: FLAG = !ZF}

.text
0x00: OP_LOAD_CONST_INT   R0, 42
0x01: OP_LOAD_CONST_INT   R1, 100
0x04: OP_MOV              R7, R0
0x09: OP_HALT
```

### 2.3 Opcode Coverage & Test-First ISA Mandate
* **Test-First ISA Rule**: When introducing new `impOps` opcodes or changing ISA behavior, `.impas` test vectors MUST be defined in `impulse-graph-spec` first or alongside the opcode specification.
* **Coverage Enforcement**: The `run_vm_asm_suite.py` test runner enforces **100% Opcode Coverage** (`0x00`..`0x72`) and requires every opcode to appear in **at least 2 distinct test files** (Multi-File Threshold Requirement).

---

## 3. Level 1: Core Engine Kernels & FFI Binding Strategy (`impulse-graph-core`)

### 3.1 Engine Logic vs. FFI Boundary Testing

| Testing Concern | Primary Target | Scope & Responsibility |
| :--- | :--- | :--- |
| **Engine Kernel Logic** | `impulse-cpp`, `impulse-rust`, `impulse-graph-java` | Bytecode opcode execution, bitset operations, SIMD matrix multiplication, memory mapping, algorithm correctness. |
| **FFI Binding Marshaling** | `impulse-python`, `impulse-go`, `impulse-node`, `impulse-dotnet` | C-ABI function call signature safety, struct padding, string conversion, pointer lifecycle, GC safety, exception propagation. |

Running 100% of all 80+ VM test vectors through every binding wrapper on *every single PR build* is redundant for catching engine bugs. Instead, we enforce a **Tiered Binding Strategy**.

---

## 4. CI/CD Pipeline Gates & Execution Schedule

```
  PR Created / Updated               Merge to main / Nightly Build
 ┌─────────────────────────┐        ┌─────────────────────────┐
 │ Stage 1: Engine Spec    │        │ Stage 1: Engine Spec    │
 │ (100% run_vm_asm_suite) │        │ (100% run_vm_asm_suite) │
 └────────────┬────────────┘        └────────────┬────────────┘
              │                                  │
              ▼                                  ▼
 ┌─────────────────────────┐        ┌─────────────────────────┐
 │ Stage 2: FFI Contract   │        │ Stage 2: Exhaustive     │
 │ (Targeted Binding Suite)│        │ Multi-Language Matrix   │
 └─────────────────────────┘        └─────────────────────────┘
```

### 4.1 Mandatory PR Gate (Pull Requests to `impulse-graph-core`)
1. **Stage 1: Full Engine Spec Compliance**:
   * Runs `python3 tools/run_vm_asm_suite.py` against the native kernel shared object (`libimpulse_graph.so` / `.dylib`).
   * Must achieve **100% pass rate** and **100% opcode coverage**.
2. **Stage 2: FFI Binding Contract Suite**:
   * Runs targeted boundary tests for Python, Go, Node.js, and C# (.NET).
   * Verifies memory safety, null-pointer handling, string/array marshaling, and representative positive/negative VM query execution across all bindings.

### 4.2 Nightly & Pre-Release Gate (Merge to `main`)
* **Exhaustive Multi-Language Matrix**: Runs 100% of all binary (`tc01`..`tc36`) and VM assembly (`vm-impas`) test vectors through **every language binding wrapper** (Python, Go, Node.js, C#, Rust, Java FFM).
* **Attestations & Signing**: Generates SLSA Level 3 build provenance attestations (`actions/attest-build-provenance`), signs macOS binaries via Developer ID, and signs Windows DLLs via Authenticode.

---

## 5. Local Execution Commands

### Native C++ Kernel (`impulse-cpp`)
```bash
cd impulse-cpp
cmake -B build && cmake --build build
ctest --test-dir build --output-on-failure
```

### Rust Engine Crate (`impulse-rust`)
```bash
cd impulse-rust
cargo test
```

### Spec VM Assembly Test Runner (`impulse-graph-spec`)
```bash
cd impulse-graph-spec
python3 tools/run_vm_asm_suite.py
```

### Python Bindings (`impulse-python`)
```bash
cd impulse-python
pytest
```

### Go Bindings (`impulse-go`)
```bash
cd impulse-go
go test -v ./...
```

### Node.js Bindings (`impulse-node`)
```bash
cd impulse-node
npm test
```

### C# / .NET Bindings (`impulse-dotnet`)
```bash
cd impulse-dotnet
dotnet test
```
