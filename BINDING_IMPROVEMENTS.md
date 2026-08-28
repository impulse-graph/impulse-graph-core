# Cross-Language Bindings Review & Improvement Roadmap

## 1. Python (`impulse-python`)
* **Zero-Copy Tensor Integration (DLPack)**: Currently, `get_float_vector` uses `std::memcpy` to return a new `py::array_t`, causing an unnecessary heap allocation and memory copy. 
  * **Fix**: Implement the `__dlpack__` protocol and Python's `__array_interface__` on a lightweight tensor wrapper class. This allows PyTorch (`torch.from_dlpack`) and NumPy to view the VM's off-heap vector memory with zero copies.
  * **Safety**: Use a `py::capsule` to tie the lifecycle of the tensor view directly to the `PyImpulseSnapshot` or `VmContext` to prevent use-after-free segfaults.

## 2. Rust (`impulse-rust`)
* **Lifetimes & Zero-Copy Safety**: Ensure that all returned zero-copy slices (e.g., `&[u32]`, `&[f32]`) explicitly tie their Rust lifetime `<'a>` to the parent `Snapshot<'a>` or `VmContext<'a>`.
* **Async Executor Starvation**: Reading from `mmap` triggers page faults, and complex graph walks can be computationally heavy. Running these synchronously in a `tokio` context will stall the executor thread pool.
  * **Fix**: Expose `async` wrapper functions that utilize `tokio::task::spawn_blocking` to safely offload C++ FFI execution to dedicated OS threads.
* **Rayon Integration (Strictly Opt-In)**: Add `IntoParallelIterator` for bitsets and result frontiers to enable seamless multi-threaded reductions in idiomatic Rust.
  * **IMPORTANT NOTE**: High concurrency (QPS) is prioritized over single-query parallelism. Rayon integration MUST be strictly opt-in:
    1. It must be gated behind an optional Cargo feature (`features = ["rayon"]`) so it doesn't pollute the dependency tree or binary if unused.
    2. The API must require explicitly calling `.par_iter()`, leaving standard `.iter()` single-threaded and undisturbed.

## 3. Golang (`impulse-go`)
* **Memory Safety with `unsafe.Slice`**: Go is correctly using `unsafe.Slice` to return pointers to C++ buffers. However, the Go GC doesn't track C++ object lifetimes. If a user drops the `VmContext` while holding the slice, accessing the slice will panic/segfault.
  * **Fix**: Wrap the returned slice in a Go struct (e.g., `VectorView[T]`) that explicitly holds a reference to the parent `VmContext` struct, ensuring the GC keeps the context alive until all slice views are garbage collected.
* **CGO Scheduler Blocking**: Long VM executions block the OS thread underlying the Goroutine. 
  * **Fix**: Document best practices for chunked execution or implement a gas/fuel limit yield loop to prevent Go scheduler starvation.

## 4. Node.js / Bun (`impulse-node`)
* **Event Loop Blocking**: The Node-API binding executes `impulse_vm_execute` synchronously on the V8 main thread. For deep multi-hop traversals, this will freeze the entire Node.js event loop.
  * **Fix**: Implement `Napi::AsyncWorker` to push the C++ VM execution to the `libuv` thread pool, returning a native JS `Promise<ExecutionResult>`.
* **Zero-Copy `ArrayBuffer` Views**: Like Python, Node.js currently uses `std::memcpy` to construct `Float32Array` objects. 
  * **Fix**: Use `Napi::ArrayBuffer::New(env, external_data, byte_length, finalizer)` to project the C++ memory directly into V8 without copying, wrapping it in a `TypedArray`.

## 5. C# / .NET (`impulse-dotnet`)
* **Missing Vector API**: The current `VmContext` and `ExecutionResult` classes are extremely barebones and only expose the `Registers` array. There is currently no way to extract the float or node vectors back into C#.
  * **Fix**: Implement the missing P/Invoke definitions for `impulse_vm_context_get_float_vector` and related accessors.
* **`Span<T>` Integration**: Do not allocate managed `float[]` arrays.
  * **Fix**: Wrap the returned unmanaged pointers in `ReadOnlySpan<T>` or `ReadOnlyMemory<T>` to provide idiomatic, safe, and zero-allocation memory access to the .NET runtime.
