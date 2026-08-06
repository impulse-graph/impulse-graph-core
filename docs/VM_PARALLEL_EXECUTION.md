# Impulse VM Parallel Execution Design & Benchmark Findings

This document summarizes our findings on parallel query execution inside the Impulse VM and outlines design strategies for optionally switching between single-threaded (ST) and multi-threaded (MT) execution modes.

---

## 1. Summary of Benchmark Findings (Twitter-2010 Graph)

Running a sequential and parallel Breadth-First Search (BFS) over the **1.47-Billion Edge `twitter-2010` Graph** from root node `0` yields the following performance comparison:

| Configuration / Mode | threads | Min Execution Time | Avg Execution / Throughput |
| :--- | :--- | :--- | :--- |
| **VM Top-Down (Sequential)** | 1 | 10,633.60 ms | 10,633.60 ms / 138.08 MTEPS |
| **VM Hybrid (Sequential)** | 1 | 1,229.30 ms | 2,864.81 ms / 512.55 MTEPS |
| **VM Hybrid (Fully Parallel)** | 10 | 1,418.67 ms | 2,110.13 ms / 695.86 MTEPS |
| **VM Hybrid (Selective Parallel)**| 10 | **917.95 ms** | **2,668.75 ms / 550.21 MTEPS** |
| **GAPBS Stock Hybrid (Parallel)** | 10 | **533.83 ms** | **533.83 ms / 2,750.62 MTEPS** |

### Insights:
* **Fully Parallel Overhead:** Running both `OP_CSR_WALK` (Top-Down) and `OP_CSC_WALK` (Bottom-Up) in parallel increases the minimum execution time (from 1,229 ms to 1,418 ms) due to OpenMP thread startup latency and atomic write contention on small frontiers.
* **Selective Parallelization Benefit:** Reverting `OP_CSR_WALK` to sequential execution while keeping `OP_CSC_WALK` parallelized yielded our fastest execution time of **917.95 ms** (a **25.3% speedup** on minimum execution time over sequential, and a **35.3% speedup** over fully parallel).

---

## 2. Options for Optionally Switching ST vs MT in Production

In high-concurrency microservice architectures (e.g., Spring gRPC server running in `impulse-platform`), multiple queries run in parallel. Forcing every query to use all OpenMP threads causes CPU thrashing. Conversely, offline analytics batch jobs require full CPU utilization.

We propose three methods to control single-threaded vs. multi-threaded execution:

### Option A: Context-Level Thread Configuration (RECOMMENDED)
Add a thread count parameter to `impulse_vm_context_t` (which holds thread-local resources for query execution). 

#### Implementation:
```cpp
// include/impulse_vm.h
IMPULSE_API void impulse_vm_context_set_concurrency(impulse_vm_context_t* ctx, int thread_count);
```

In the opcode loop handlers:
```cpp
#if defined(_OPENMP)
    int num_threads = vm_state->query_context->threads;
    #pragma omp parallel for schedule(dynamic, 1024) num_threads(num_threads)
#endif
    for (size_t i = 0; i < words; ++i) {
        // parallel or sequential work
    }
```
* **Pros:** Extremely clean. Leverages OpenMP's built-in `num_threads` parameter. Setting it to `1` automatically avoids thread spawning and synchronization overhead.
* **Cons:** OpenMP runtime still checks the `num_threads` expression at runtime.

---

### Option B: Dynamic Opcode Flags (Instruction-Level Control)
Define an instruction-level flag bit inside `impulse_instruction_t::flags`.

#### Implementation:
```cpp
#define IMPULSE_VM_OP_FLAG_PARALLEL 0x02
```

In `impulse_vm.cpp`:
```cpp
case OP_CSC_WALK: {
    bool run_parallel = (inst.flags & IMPULSE_VM_OP_FLAG_PARALLEL);
    if (run_parallel) {
#if defined(_OPENMP)
        #pragma omp parallel for schedule(dynamic, 1024)
#endif
        for (size_t i = 0; i < words; ++i) { ... }
    } else {
        for (size_t i = 0; i < words; ++i) { ... }
    }
}
```
* **Pros:** Highly granular. The query planner can choose to parallelize only specific instructions when input sizes are known to exceed a threshold.
* **Cons:** Duplicate loop structures or conditional compilation in code; slight bytecode size inflation.

---

### Option C: Separate Opcodes (`OP_CSC_WALK` vs `OP_CSC_WALK_MT`)
Define unique opcodes for sequential and parallel executions.

* **Pros:** Hard-coded compilation. No branch prediction overhead at runtime.
* **Cons:** Bloats VM bytecode instruction catalog; duplicate implementation loops.

---

## 3. Recommended Choice: Context-Level Thread Configuration
**Option A** is the most robust and matches modern FFI execution models. The query execution context (`impulse_vm_context_t`) is already designed to be thread-local and lightweight. Exposing a simple `concurrency` setting on the context allows the calling application (e.g., Java gRPC worker or Go web server) to dynamically set the maximum threads per query based on CPU load and tenant SLA.
