# Multi-Lane Physical Width Packing (`WIDTH_8`/`16`/`32`/`64`), `32 B/node` Byte-Arena Slab, $O(\text{active})$ Reset & Statement Buffer Tightening

* **Repository**: `impulse-graph-core` (`impulse-cpp`, `impulse-cli`, C-ABI bindings)
* **Tracking Issue**: [`impulse-graph-core#83`](https://github.com/impulse-graph/impulse-graph-core/issues/83)
* **Companion Issues**:
  * Java 25 Core Engine: [`impulse-graph-java#41`](https://github.com/impulse-graph/impulse-graph-java/issues/41)
  * `v0.11+` Specification (`TYPE_LANE_STATE`): [`impulse-graph-spec#23`](https://github.com/impulse-graph/impulse-graph-spec/issues/23)
  * Historical Context: [`impulse-graph-core#29`](https://github.com/impulse-graph/impulse-graph-core/issues/29) (commit `59ccd37`)
* **Target Branch**: `feat/multilane-physical-packing`
* **Specification Impact**: **Zero ISA or binary format changes** (`VM_SPECIFICATION.md` v0.10.0 and `opcodes.json` remain unchanged).

---

## 1. Motivation & Root-Cause Analysis

### 1.1 Why Closed Issue `#29` (`59ccd37`) Left Physical Width Unpacked
Commit `59ccd37` (*"perf(vm): implement sparse active_words bitset and hardware-width walk tiers (#29)"*) added two partial optimizations to `impulse-cpp`:
1. Added `std::vector<uint64_t> active_words` (`1 bit/node`) to `VmLaneFrontier` (`impulse-cpp/src/impulse_vm_internal.hpp:216`) and skipped inactive source words in `OP_LANE_CSR_WALK` (`0xC3`) and `OP_LANE_CSC_WALK` (`0xC4`) via `std::countr_zero`.
2. Templated `execute_walk_tiered` in `impulse-cpp/src/opcodes/shared/lane_walk_common.hpp:96-98` on `WordT` (`uint8_t`, `uint16_t`, `uint32_t`, `uint64_t`).

However, `VmLaneFrontier::lanes` (`impulse_vm_internal.hpp:215`), `ctx->scratch_lanes`, and `ctx->scratch_lanes_b` (`impulse_vm_internal.hpp:276-277`) remained `std::vector<uint64_t>` (`8 B/node` across all four `impulse_lane_width_t` tiers). Inside `execute_walk_tiered`, `static_cast<WordT>(cur_lanes[u])` truncated values in CPU registers while still striding memory at 8 bytes per node:
* Random neighbor scatter writes (`next_lanes[v] |= prop`) touch **4x more cache lines** than necessary for $K \le 8$ (`WIDTH_8`) and **2x more** for $K \le 16$ (`WIDTH_16`).
* On an $N = 10\text{M}$-node domain, a single `WIDTH_8` frontier occupies `80 MB` instead of `10 MB` (exceeding typical L3 cache slices).
* `impulse_vm_context_create` (`impulse-cpp/src/impulse_vm_context.cpp:420-429`) eagerly pre-reserves `32` frontier handles plus `2` scratch vectors at `8 B/node` (`34 * 8 = 272 B/node`), consuming **`2.72 GB` per VM worker context** on a 10M-node snapshot.
* `acquire_lane_frontier` (`impulse_vm_internal.hpp:488`) executes `lf.lanes.assign(node_count, 0ULL)` ($O(N)$ `memset`) on every multi-lane opcode (`0xC0`–`0xC5`), and `impulse_vm_context_reset` (`impulse_vm_context.cpp:736-740`) calls `.clear()` across all 32 handles.

### 1.2 Relationship to MITra (PVLDB 2023)
Evaluation against *Li et al., "MITra: A Framework for Multi-Instance Graph Traversal", PVLDB 16(10): 2551–2564, 2023* confirmed two high-ROI runtime improvements compatible with `ImpulseVM` v0.10.0:
1. **Physical Sub-Word Packing (`1 / 2 / 4 / 8 B/node`)**: Matching physical storage density to `select_lane_width(num_lanes)` shrinks random-scatter working sets by **8x / 4x / 2x / 1x** so more of the target domain fits in L2/L3 cache.
2. **$O(\text{active})$ Sparse Frontier Reset**: Using `active_words` (`std::countr_zero`) to clear only touched nodes on context reset eliminates $O(N)$ `memset` overhead on sparse multi-seed queries.

> **Note on `>64`-bit SIMD Lanes (`128/256/512`-bit)**: Intentionally deferred. Once 64 lanes already amortize CSR `offsets`/`targets` reads by 64x, widening per-node slots to 16/32/64 bytes shrinks effective L2/L3 node capacity on random CSR target scatters by 2x–8x. Any future `>64`-bit lane tier requires empirical benchmark validation across both `arm64` (128-bit NEON) and `x86_64` (AVX2/AVX-512) first.

---

## 2. Architectural Design & Zero-Allocation Invariants

All changes strictly obey `AGENTS.md` §3.2 (zero third-party runtime dependencies in `impulse-core`) and §3.19 (zero heap allocations on the `impulse_vm_execute` hot path).

### 2.1 Shared `32 B/node` 64-Byte-Aligned Context Byte-Arena Slab

Instead of pre-reserving 32 independent `uint64_t[max_nodes]` vectors (`256 B/node` + `16 B/node` scratch = `272 B/node`), `impulse_vm_context` allocates **one contiguous 64-byte-aligned raw `uint8_t` arena slab** in `impulse_vm_context_create`:

$$\text{Slab Capacity} = \text{align}_{64}(32 \times \text{max\_nodes})\text{ bytes}$$

Plus packed `scratch_lanes_bytes` and `scratch_lanes_b_bytes` (`8 B/node` max each, sliced at `1/2/4/8 B/node` per walk).

| Metric ($N = 10\text{M}$ nodes) | Current (`uint64_t[N] x 34`) | Proposed (`32 B/node` Slab + `16 B/node` Scratch) | Improvement |
| :--- | ---: | ---: | ---: |
| Frontier Handle Pool | `2,560 MB` | `320 MB` | **8.0x reduction** |
| Walk Scratch Buffers (`a` + `b`) | `160 MB` | `160 MB` (max `WIDTH_64`), sliced to `20 MB` active for `WIDTH_8` | **Up to 8x active L3 footprint reduction** |
| Total Multi-Lane Context Footprint | `2,720 MB` | `480 MB` | **5.67x total / 8.0x handle pool** |
| Concurrent Live Handles Supported | 32 of any width | **4** concurrent `WIDTH_64`, **8** `WIDTH_32`, **16** `WIDTH_16`, or **32** `WIDTH_8` | Covers all standard Pass 4 pipelines (2–3 live handles) |

#### Bump Allocation & Overflow Guard
* `ctx->lane_slab_memory` (`uint8_t*`, allocated once via `new (std::align_val_t{64}) uint8_t[slab_cap]` in `impulse_vm_context_create` and freed in `impulse_vm_context_destroy`) is bump-allocated at 64-byte alignment by `acquire_lane_frontier(ctx, node_count, num_lanes)`:
  ```cpp
  constexpr size_t lane_width_bytes(impulse_lane_width_t w) noexcept {
      switch (w) {
          case IMPULSE_LANE_WIDTH_8:  return 1;
          case IMPULSE_LANE_WIDTH_16: return 2;
          case IMPULSE_LANE_WIDTH_32: return 4;
          case IMPULSE_LANE_WIDTH_64: return 8;
      }
      return 8;
  }
  ```
* Each slice requires `bytes_needed = ((node_count * elem_bytes) + 63ULL) & ~63ULL`.
* **Invariant Slab Zeroing**: Every byte in `ctx->lane_slab_memory` starts at `0` on context creation (`std::memset` once in `impulse_vm_context_create`) and is restored to `0` in `impulse_vm_context_reset()` via sparse `active_words` cleanup (§2.3). Consequently, `acquire_lane_frontier` becomes **$O(1)$ pointer arithmetic with zero `memset`**.
* **Synthetic Contract Test Fallback**: If a synthetic unit test ever acquires more than `32 B/node` simultaneously (e.g., 10 live `WIDTH_64` handles on a tiny 64-node mock graph without resetting), `VmLaneFrontier` retains an optional `std::vector<uint8_t> overflow_storage` fallback used **only** when `lane_slab_offset + bytes_needed > lane_slab_capacity`. Normal queries never hit overflow and perform **0 heap allocations**.

### 2.2 C++20 Strict-Aliasing Safety (`-fstrict-aliasing` at `-O3`)

Storing `std::vector<uint64_t>` and casting its pointer via `reinterpret_cast<uint16_t*>` or `reinterpret_cast<uint32_t*>` is **Undefined Behavior** under C++20 `[basic.lval]` strict aliasing rules and risks silent miscompilation or `build_on` (`-O3`) vs `build_off` (`-O0`) divergence in `four_way_validation.py`.

To remain strictly conforming C++20:
1. Backing storage in `lane_slab_memory`, `scratch_lanes_bytes`, and `scratch_lanes_b_bytes` is typed `uint8_t*` (`unsigned char*`), which may legally alias object representations.
2. Each `acquire_lane_frontier` binds a fresh 64-byte-aligned slice exclusively as **one concrete unsigned integer array** (`uint8_t*`, `uint16_t*`, `uint32_t*`, or `uint64_t*`) for the entire lifetime of that handle until `impulse_vm_context_reset()` clears it using the **exact same `WordT`**.
3. `VmLaneFrontier` exposes typed accessors and an inline width-neutral reader/writer for non-hot inspection paths:
   ```cpp
   struct VmLaneFrontier {
       uint8_t* data_ptr = nullptr;          // Points into ctx->lane_slab_memory (64B aligned) or overflow_storage
       size_t node_count = 0;
       size_t active_word_count = 0;
       std::vector<uint64_t> active_words;   // Pre-reserved to words_per_bitset in context_create
       std::vector<uint8_t> overflow_storage;// Cold fallback only if slab exhausted in synthetic tests
       uint32_t num_lanes = 0;
       impulse_lane_width_t width = IMPULSE_LANE_WIDTH_8;
       uint64_t active_mask = 0;
       uint16_t edge_trip_active_mask = 0;
       std::array<std::vector<uint64_t>, VM_MAX_REL_SLOTS> edge_trip_masks;

       template <typename WordT>
       IMPULSE_VM_FORCE_INLINE WordT* data_as() noexcept {
           return reinterpret_cast<WordT*>(data_ptr);
       }
       template <typename WordT>
       IMPULSE_VM_FORCE_INLINE const WordT* data_as() const noexcept {
           return reinterpret_cast<const WordT*>(data_ptr);
       }
       IMPULSE_VM_FORCE_INLINE uint64_t read_lane_u64(size_t u) const noexcept {
           switch (width) {
               case IMPULSE_LANE_WIDTH_8:  return static_cast<uint64_t>(data_as<uint8_t>()[u]);
               case IMPULSE_LANE_WIDTH_16: return static_cast<uint64_t>(data_as<uint16_t>()[u]);
               case IMPULSE_LANE_WIDTH_32: return static_cast<uint64_t>(data_as<uint32_t>()[u]);
               case IMPULSE_LANE_WIDTH_64: return data_as<uint64_t>()[u];
           }
           return 0ULL;
       }
       IMPULSE_VM_FORCE_INLINE void or_lane_u64(size_t u, uint64_t mask) noexcept {
           uint64_t m = mask & active_mask;
           if (m == 0ULL) return;
           switch (width) {
               case IMPULSE_LANE_WIDTH_8:  data_as<uint8_t>()[u]  |= static_cast<uint8_t>(m);  break;
               case IMPULSE_LANE_WIDTH_16: data_as<uint16_t>()[u] |= static_cast<uint16_t>(m); break;
               case IMPULSE_LANE_WIDTH_32: data_as<uint32_t>()[u] |= static_cast<uint32_t>(m); break;
               case IMPULSE_LANE_WIDTH_64: data_as<uint64_t>()[u] |= m;                        break;
           }
           active_words[u >> 6] |= (1ULL << (u & 63));
       }
   };
   ```

### 2.3 Handle Aliasing Safety (`OP_MOV`) & $O(\text{active})$ Sparse Reset

* **No Mid-Query Handle Recycling**: Because `OP_MOV R1, R0` (`0x90`) copies the integer handle ID without refcounting (`R0` and `R1` alias the same `VmLaneFrontier` slot), opcodes (`0xC2`–`0xC5`) MUST NOT release or mutate `src_handle` in-place even when `dst_reg == src_reg` (an earlier instruction may have aliased `src_handle` into another register). Every mutating multi-lane opcode continues to allocate a fresh handle from the bump slab and updates `vm_state->registers[dst]` only after reading `lf_src`.
* **$O(\text{active})$ Cleanup on `impulse_vm_context_reset()`**:
  Instead of `memset`ting the entire `32 B/node` slab on reset or on next acquire, `impulse_vm_context_reset()` iterates `0 .. ctx->lane_frontiers_count - 1` and zeroes **only** the entries flagged in `lf.active_words`:
  ```cpp
  template <typename WordT>
  inline void clear_packed_lanes_sparse(WordT* data, size_t node_count, uint64_t* active_words, size_t word_count) noexcept {
      for (size_t w_idx = 0; w_idx < word_count; ++w_idx) {
          uint64_t w = active_words[w_idx];
          if (w == 0ULL) continue;
          while (w != 0ULL) {
              uint32_t bit = static_cast<uint32_t>(std::countr_zero(w));
              w &= (w - 1ULL);
              size_t u = (w_idx << 6) + bit;
              if (u < node_count) {
                  data[u] = WordT{0};
              }
          }
          active_words[w_idx] = 0ULL;
      }
  }
  ```
  After sparse zeroing all allocated handles, `ctx->lane_slab_offset = 0` and `ctx->lane_frontiers_count = 0`.
  * **Strict Bi-Directional `active_words` Invariant**: Every write `data[u] = non_zero` across **all** opcodes (`0xC0`–`0xC5`, `0x93`, `clone_lane_frontier`) MUST set `active_words[u >> 6] |= (1ULL << (u & 63))`, and any prune (`0xC5`) that clears `data[u]` to `0` clears the bit in `active_words[u >> 6]`. Otherwise, a non-zero slot unmarked in `active_words` would survive `impulse_vm_context_reset()` and corrupt a subsequent query's slab slice!

### 2.4 Cross-Domain Node-Count Bounds & Sub-Word Scatter Concurrency Guard

1. **Cross-Domain Bounds Separation (`AGENTS.md` §3.14)**:
   In `lane_walk_common.hpp:46`, `n_nodes` is currently computed via `resolve_multilane_node_count(ctx, rel_id, lf_src.lanes.size())` (taking `max(src, dst)`). With physical packing and multi-domain relations `(DomainA -> DomainB)`:
   * Forward CSR walk (`0xC3`) source node count is `src_limit = std::min(lf_src.node_count, s.node_count)` and destination node count is `dst_node_count` (`s.tgt_node_count` when non-zero, or `max` across virtual children; when `accumulate || is_fixpoint` on an endomorphic relation, `std::max(dst_node_count, lf_src.node_count)`).
   * Reverse CSC walk (`0xC4`) source node count is `std::min(lf_src.node_count, csc_dom_n)` and destination node count is `s.node_count`.
   * Every source read checks `u < lf_src.node_count` and every target write checks `v < lf_dst.node_count`.
2. **Sub-Word RMW Tearing Guard (`max_dop > 1`)**:
   `0xC3` and `0xC4` currently execute single-threaded across all 8–64 lanes simultaneously. Document an explicit `static_assert` / architectural guard comment in `lane_walk_common.hpp`: on `WIDTH_8` (`uint8_t`) and `WIDTH_16` (`uint16_t`), concurrent OpenMP threads scattering to adjacent nodes `v` and `v + 1` share the same 32-bit/64-bit word on weak-memory-model ISAs (`arm64`) and cannot use non-atomic RMW (`next_lanes[v] |= prop`). Any future intra-instruction `max_dop > 1` parallelization on `0xC3`/`0xC4` MUST either use `std::atomic_ref<WordT>(next_lanes[v]).fetch_or(prop, std::memory_order_relaxed)` + atomic `fetch_or` on `next_active_words[v >> 6]`, or partition target vertices cleanly via CSC pull.

### 2.5 C-ABI Stability & `impulse_stmt_prepare` Buffer Tightening

1. **C-ABI Inspection Functions (`impulse-cpp/include/impulse_vm.h`)**:
   * Add a zero-overhead raw pointer accessor:
     ```c
     IMPULSE_API const void* impulse_vm_context_lane_frontier_raw_data(
         const impulse_vm_context_t* ctx,
         size_t handle
     );
     ```
     Returns `const uint8_t*` / `const uint16_t*` / `const uint32_t*` / `const uint64_t*` according to `impulse_vm_context_lane_frontier_width(ctx, handle)`.
   * Preserve backwards compatibility for `impulse_vm_context_lane_frontier_data(const impulse_vm_context_t* ctx, size_t handle)` (`const uint64_t*`):
     * When `lf.width == IMPULSE_LANE_WIDTH_64`, return `lf.data_as<uint64_t>()` directly ($O(1)$).
     * When `lf.width` is `WIDTH_8`/`16`/`32`, widen on-demand into pre-allocated `ctx->scratch_u64_a` (`impulse_vm_internal.hpp:284`, using `const_cast` or `mutable` staging buffer sized to `ctx->max_nodes` at context creation) using `active_words` so existing C/Rust/test callers expecting `uint64_t[node_count]` continue working with zero heap allocation on pre-sized snapshots.
2. **Statement Execution (`impulse-cpp/src/impulse_statement_c_api.cpp`)**:
   * **Raw `TYPE_LANE_FRONTIER` Output Unchanged**: Keep `impulse_stmt_execute` (`impulse_statement_c_api.cpp:538-556`) emitting `col.element_size = sizeof(uint64_t)` (`8 B/row`, widened `uint64_t[N]`) so C/C++/Rust callers inspecting `TYPE_LANE_FRONTIER` columns do not need a 4-way switch on `col.element_size`. Use `active_words` + `std::memset(out_u64, 0, n_copy * sizeof(uint64_t))` to widen only active entries cleanly.
   * **Tightened `return_bytes_needed` in `impulse_stmt_prepare` (`impulse_statement_c_api.cpp:367-373`)**:
     Inspect the compiled program's terminal result-producing instruction (the instruction writing `compiled_program.result_register`, or the last non-`OP_HALT` instruction):
     * `OP_LANE_EXTRACT_BITSET` (`0xC6`) or `OP_COLLECT_BITSET` / bitset ops $\implies$ `min_ret_bytes = stmt->words_per_bitset * sizeof(uint64_t)` (**64x smaller** than `max_nodes * 8`: `1.25 MB` instead of `80 MB` on $N = 10\text{M}$).
     * `OP_LANE_POPCOUNT` (`0xC7`) or scalar count/reduce ops $\implies$ `min_ret_bytes = sizeof(uint64_t)` (`128 B` aligned).
     * `OP_LANE_REDUCE_COUNTS` (`0xC8`) $\implies$ `min_ret_bytes = 64 * sizeof(uint64_t)` (`512 B`, since $K \le 64$).
     * All other terminal instructions (including raw `TYPE_LANE_FRONTIER`, `TYPE_NODE_VECTOR`, `TYPE_FLOAT_VECTOR`, or dynamic control flow where the terminal register type is statically ambiguous) retain the safe upper bound `std::max(words_per_bitset * 8, max_nodes * 8)`.

---

## 3. File-by-File Implementation Plan

### 3.1 `impulse-cpp/include/impulse_vm.h`
* Declare `impulse_vm_context_lane_frontier_raw_data(const impulse_vm_context_t* ctx, size_t handle)` and `impulse_vm_context_lane_frontier_byte_size(const impulse_vm_context_t* ctx, size_t handle)` (`node_count * elem_bytes`, for white-box physical-packing tests).
* Document on `impulse_vm_context_lane_frontier_data` that for `WIDTH_8`/`16`/`32` handles, the returned `const uint64_t*` points to a context-owned staging buffer (`scratch_u64_a`) overwritten by subsequent calls on the same context.

### 3.2 `impulse-cpp/src/impulse_vm_internal.hpp`
* Replace `std::vector<uint64_t> lanes` in `VmLaneFrontier` (`lines 214-222`) with `uint8_t* data_ptr`, `size_t node_count`, `size_t active_word_count`, `std::vector<uint64_t> active_words`, and `std::vector<uint8_t> overflow_storage`, plus `lane_width_bytes()`, `data_as<WordT>()`, `read_lane_u64(u)`, `write_lane_u64(u, val)`, and `or_lane_u64(u, val)`.
* On `impulse_vm_context` (`lines 272-285`):
  * Add `uint8_t* lane_slab_memory = nullptr;`, `size_t lane_slab_capacity = 0;`, and `size_t lane_slab_offset = 0;`.
  * Replace `std::vector<uint64_t> scratch_lanes` and `scratch_lanes_b` with 64-byte-aligned raw byte buffers (`uint8_t* scratch_lanes_memory = nullptr; uint8_t* scratch_lanes_b_memory = nullptr; size_t scratch_lanes_byte_cap = 0;`), sliced as `WordT*` in `lane_walk_common.hpp`.
  * Mark `scratch_u64_a` `mutable` so `impulse_vm_context_lane_frontier_data(const impulse_vm_context_t*, size_t)` can widen sub-word frontiers without heap allocation.
* Update `acquire_lane_frontier(ctx, node_count, num_lanes)` (`lines 477-505`):
  * Compute `width = select_lane_width(clamped_k)` and `elem_bytes = lane_width_bytes(width)`.
  * Slice `bytes_needed = ((node_count * elem_bytes) + 63ULL) & ~63ULL` from `ctx->lane_slab_memory + ctx->lane_slab_offset` (or fallback to `lf.overflow_storage` if `lane_slab_offset + bytes_needed > lane_slab_capacity`).
  * Ensure `lf.active_words` has capacity `>= (node_count + 63) / 64` (already pre-reserved at context creation; zeroed at prior reset) and set `lf.active_word_count = (node_count + 63) / 64`.
* Update `clone_lane_frontier(ctx, src_handle)` (`lines 513-543`):
  * Copy only `src.node_count * lane_width_bytes(src.width)` bytes (or copy sparsely via `src.active_words` when sparse) and copy `src.active_word_count` words of `active_words`.

### 3.3 `impulse-cpp/src/impulse_vm_context.cpp`
* **`impulse_vm_context_create` (`lines 420-435`)**:
  * Allocate `ctx->lane_slab_memory` with `std::align_val_t{64}` of size `((32ULL * lane_cap) + 63ULL) & ~63ULL` (`32 B/node`) and zero-initialize once.
  * Allocate `ctx->scratch_lanes_memory` and `ctx->scratch_lanes_b_memory` with `std::align_val_t{64}` of size `((8ULL * lane_cap) + 63ULL) & ~63ULL` each and zero-initialize once.
  * Pre-size `lf.active_words.assign(lane_word_cap, 0ULL)` across all 32 handles, plus `scratch_active_words.assign(lane_word_cap, 0ULL)` and `scratch_active_words_b.assign(lane_word_cap, 0ULL)`.
* **`impulse_vm_context_destroy` (`lines 700-706`)**:
  * `::operator delete[](ctx->lane_slab_memory, std::align_val_t{64});` (and same for `scratch_lanes_memory` / `scratch_lanes_b_memory`).
* **`impulse_vm_context_reset` (`lines 736-741`) & `impulse_vm_context_release_lane_frontier` (`lines 964-974`)**:
  * Dispatch `clear_packed_lanes_sparse<WordT>` on each allocated handle (`0 .. ctx->lane_frontiers_count - 1`) according to `lf.width`, clear `edge_trip_masks` only when `edge_trip_active_mask != 0`, and reset `ctx->lane_slab_offset = 0` and `ctx->lane_frontiers_count = 0`.
* **C-ABI Accessors (`lines 997-1010`)**:
  * `impulse_vm_context_lane_frontier_node_count` returns `lf.node_count`.
  * `impulse_vm_context_lane_frontier_raw_data` returns `lf.data_ptr`.
  * `impulse_vm_context_lane_frontier_byte_size` returns `lf.node_count * lane_width_bytes(lf.width)`.
  * `impulse_vm_context_lane_frontier_data` returns `lf.data_as<uint64_t>()` for `WIDTH_64`, or widens `lf` into `ctx->scratch_u64_a` (`assign`/`memset` + sparse `active_words` scatter) for `WIDTH_8`/`16`/`32`.
* **ICTX v1 State Hash (`lines 1288-1315`)**:
  * Replace `lf.lanes.size()` with `lf.node_count` and `hs.write_u64_le(lf.lanes[n])` with `hs.write_u64_le(lf.read_lane_u64(n) & lf.active_mask)`. Preserves **100% bit-exact ICTX v1 hash parity** with `impulse-ocaml` and `impulse-graph-java`.

### 3.4 `impulse-cpp/src/opcodes/shared/lane_walk_common.hpp` (`0xC3` & `0xC4`)
* Separate `src_node_count = lf_src.node_count` from `dst_node_count = resolve_multilane_target_node_count<IsCsc>(ctx, rel_id, (accumulate || is_fixpoint) ? lf_src.node_count : 0)`.
* Parameterize `step_slot_once` directly on `const WordT* cur_lanes, size_t cur_n, const uint64_t* cur_active_words, size_t cur_words, WordT* next_lanes, size_t next_n, uint64_t* next_active_words`:
  * Every source load `WordT u_mask = cur_lanes[u] & active_mask_w` reads native `1/2/4/8`-byte elements.
  * Every target scatter `WordT prev = next_lanes[v]; WordT nw = static_cast<WordT>(prev | prop); if (nw != prev) { next_lanes[v] = nw; next_active_words[v >> 6] |= (1ULL << (v & 63)); }` writes native `1/2/4/8`-byte elements with **zero narrowing/widening memory casts**.
* In `FIXPOINT` (`IMP_FLAG_DELTA_FIXPOINT`):
  * Slice `WordT* delta = reinterpret_cast<WordT*>(ctx->scratch_lanes_memory)` and `WordT* step_out = reinterpret_cast<WordT*>(ctx->scratch_lanes_b_memory)` (`node_count * sizeof(WordT)` bytes).
  * Zero `delta` and `step_out` sparsely via `delta_active_words` / `step_active_words` after each iteration and before returning so `scratch_lanes_memory` remains clean (`0`) for subsequent walks without $O(N)$ `memset`!
* Add the `max_dop > 1` sub-word RMW tearing guard comment at the top of `execute_walk_tiered`.

### 3.5 Remaining Multi-Lane & Register Opcodes
* **`op_c0_lane_init_seeds.inc` & `op_c1_lane_init_groups.inc`**:
  * Write seed/group bits via `lf.or_lane_u64(u, 1ULL << b)` (which writes native `WordT` and sets `active_words[u >> 6]`).
* **`op_c2_lane_set_edge_mask.inc`**:
  * Clone `lf_src` into `lf_dst` by copying only active words (`active_words` sparse loop on `WordT*`) rather than scanning `0 .. N-1`.
* **`op_c5_lane_prune_mask.inc`**:
  * Dispatch on `WordT` (`uint8_t`/`uint16_t`/`uint32_t`/`uint64_t`), iterate only `lf_src.active_words`, apply node bitset / scalar attr / mock attr / edge attr filter, write surviving `m != 0` into `lf_dst.data_as<WordT>()[u]`, and set `lf_dst.active_words[u >> 6] |= (1ULL << bit)` **if and only if** `m != 0`.
* **`op_c6_lane_extract_bitset.inc`, `op_c7_lane_popcount.inc`, `op_c8_lane_reduce_counts.inc`, `op_c9_vector_score_batch.inc`, `op_ca_vector_topk_bitset.inc`, `op_cb_vector_rerank_dot.inc`**:
  * Dispatch on `WordT` and iterate strictly over `lf.active_words` (`std::countr_zero`) instead of dense `0 .. N-1` loops where any remain.
* **`op_91_clear_reg.inc`, `op_93_type_cast.inc`, `op_a9_frontier_unpack.inc`, `op_aa_frontier_map.inc`, `impulse_vm_fluent_multilane.cpp`**:
  * Update any direct `lf.lanes` references to `lf.node_count` / `lf.read_lane_u64(u)` / `lf.or_lane_u64(u, mask)` (note: `op_91_clear_reg` must keep `lf` clean via `clear_packed_lanes_sparse` only if safe under aliasing, or simply clear the register tag without mutating shared slab handles—matching `OP_MOV` aliasing semantics!).

### 3.6 `impulse-cpp/src/impulse_statement_c_api.cpp`
* In `impulse_stmt_prepare` (`lines 367-373`), add static terminal-instruction inspection helper `estimate_return_bytes(const CompiledProgram&, size_t words_per_bitset, size_t max_nodes)`:
  * Terminal `OP_LANE_EXTRACT_BITSET` (`0xC6`) / `OP_COLLECT_BITSET` $\implies$ `words_per_bitset * sizeof(uint64_t)`.
  * Terminal `OP_LANE_POPCOUNT` (`0xC7`) $\implies$ `sizeof(uint64_t)`.
  * Terminal `OP_LANE_REDUCE_COUNTS` (`0xC8`) $\implies$ `64 * sizeof(uint64_t)`.
  * Otherwise $\implies$ `std::max(words_per_bitset * sizeof(uint64_t), max_nodes * sizeof(uint64_t))`.
* In `impulse_stmt_execute` (`lines 538-556`), keep `TYPE_LANE_FRONTIER` output column at `col.element_size = sizeof(uint64_t)` (`uint64_t[n_copy]`), populated via zero-fill + sparse `active_words` scatter.

---

## 4. Verification & Required Test Plan

### 4.1 Existing White-Box Tests to Update / Verify
* `impulse-cpp/tests/test_opcode_contracts.cpp`
* `impulse-cpp/tests/test_issues_49_50_51_52.cpp`
* `impulse-cpp/tests/test_security_hardening.cpp`
* All `impulse-graph-spec/test-vectors/vm-impas/` multi-lane vectors (`0xC0`–`0xCB`).

### 4.2 Five Mandatory New Unit Tests (`impulse-cpp/tests/test_multilane_physical_packing.cpp`)
1. **`PhysicalByteFootprintAndSlabPackingTest`**:
   * Acquire frontiers at all 17 normative boundary seed counts $\{0, 1, 7, 8, 9, 15, 16, 17, 31, 32, 33, 63, 64, 65, 127, 128, 129\}$ (` clamped` to 64 per opcode contract).
   * Assert `impulse_vm_context_lane_frontier_byte_size(ctx, h)` equals `N * 1` (`K <= 8`), `N * 2` (`9 <= K <= 16`), `N * 4` (`17 <= K <= 32`), and `N * 8` (`33 <= K <= 64`).
   * Acquire 32 concurrent `WIDTH_8` handles on the same context and verify zero heap allocations (`overflow_storage.empty()` across all 32 handles, all backed by the `32 B/node` slab).
2. **`SubWordHighBitAndNeighborClobberCanaryTest`**:
   * Exercise MSB lanes `7` (`0x80` in `WIDTH_8`), `15` (`0x8000` in `WIDTH_16`), `31` (`0x80000000` in `WIDTH_32`), and `63` (`0x8000000000000000ULL` in `WIDTH_64`) across `0xC0` $\to$ `0xC2` $\to$ `0xC3` (single + `ACCUMULATE` + `FIXPOINT`) $\to$ `0xC4` $\to$ `0xC5` $\to$ `0xC6`/`0xC7`/`0xC8`.
   * Seed adjacent odd/even node IDs `u - 1`, `u`, `u + 1` with distinct bit patterns and assert writes to `u` never clobber or bleed into `u - 1` or `u + 1`.
3. **`ActiveWordsBiDirectionalInvariantAndSparseResetCanaryTest`**:
   * On $N = 100{,}000$, run a sparse 2-hop walk touching $< 50$ nodes across all 4 widths (`WIDTH_8`/`16`/`32`/`64`), asserting `active_words` bit `(u >> 6, u & 63)` is `1` **iff** `read_lane_u64(u) != 0` (including after `0xC5` prunes a subset of active nodes back to `0`).
   * Call `impulse_vm_context_reset(ctx)`, re-acquire handles across different width tiers (e.g., `WIDTH_8` reusing slab bytes previously touched by `WIDTH_64` and vice-versa), and assert 100% zeroed initial state with zero stale-bit leakage across 50 randomized rounds.
4. **`InPlaceRegisterAliasingAndMovSafetyTest`**:
   * Execute `OP_LANE_INIT_SEEDS R0` $\to$ `OP_MOV R1, R0` $\to$ `OP_LANE_CSR_WALK R0, R0, rel0` $\to$ `OP_LANE_PRUNE_MASK R0, R0, ...` across all 4 widths (`K = 8, 16, 32, 64`).
   * Assert `R1` preserves the unmodified pre-walk seed frontier while `R0` holds the post-walk pruned frontier, matching distinct-register execution bit-for-bit.
5. **`CrossDomainBoundsAndStatementBufferTighteningTest`**:
   * Execute a multi-domain walk `DomainA (N = 4096) -> DomainB (N = 512) -> DomainA (N = 4096)` at `WIDTH_8` and `WIDTH_16` (both non-accumulating and with high node IDs `> 512` in `DomainA`), verifying zero out-of-bounds reads/writes under AddressSanitizer / UndefinedBehaviorSanitizer.
   * Verify `impulse_stmt_buffer_size(stmt)` for queries ending in `0xC6` (`TYPE_BITSET_HANDLE`), `0xC7` (`TYPE_INT64`), and `0xC8` (`K <= 64` counts) requests `words_per_bitset * 8` / scalar / `512 B` return areas rather than `max_nodes * 8`, while raw `TYPE_LANE_FRONTIER` queries still return `col.element_size == 8` (`uint64_t[N]`).

### 4.3 Polyglot Parity & Build Gates
* Build both `build_on` (`-O3`) and `build_off` (`-O0`) shared libraries and run `python3 ~/impulse/impulse-verification/tools/four_way_validation.py` — 100% bitwise register, `ZF`, `PC`, status, and ICTX v1 hash parity across C++ ON, C++ OFF, Java 25, and OCaml.
* Run `ctest --output-on-failure` under ASan + UBSan (`-fsanitize=address,undefined`) to prove C++20 strict-aliasing and bounds cleanliness.
