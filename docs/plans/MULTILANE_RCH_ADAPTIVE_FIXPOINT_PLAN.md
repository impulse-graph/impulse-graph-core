# Multi-Lane `MITra-RCH` Gauss-Seidel `FIXPOINT` Coalescing & Adaptive Push-CSR / Pull-CSC Walk (`C++20`)

* **Repository**: `impulse-graph-core` (`impulse-cpp`)
* **Tracking Issue**: [`impulse-graph-core#85`](https://github.com/impulse-graph/impulse-graph-core/issues/85)
* **Release Horizon**: Post-`v0.10.0` feature lockdown (`v0.10.x` / `v0.11.0`), stacked on [`impulse-graph-core#83`](https://github.com/impulse-graph/impulse-graph-core/issues/83) (`feat/multilane-physical-packing`)
* **Companion Issues**:
  * Java 25 Counterpart: [`impulse-graph-java#42`](https://github.com/impulse-graph/impulse-graph-java/issues/42) (stacked on [`impulse-graph-java#41`](https://github.com/impulse-graph/impulse-graph-java/issues/41))
  * `v0.11+` Numeric State Projection Spec (Lesson 4.2): [`impulse-graph-spec#23`](https://github.com/impulse-graph/impulse-graph-spec/issues/23)
* **Target Branch**: `feat/multilane-rch-adaptive-fixpoint`
* **Binary / ISA Impact**: **Zero `.imps` or `impOps` bytecode encoding changes**; coordinates 4-way `FIXPOINT` fuel and flag invariants across C++, Java, and OCaml (`AGENTS.md` §3.18).

---

## 1. Empirical Evidence & Root-Cause Analysis

### 1.1 Findings from `mitra_aside_bench` (`K = 2..512`, Apple Silicon ARM64, `threads=1`)
Empirical runs recorded in `impulse-benchmarks/BENCHMARK_FINDINGS.md` (`c331379`) compared `ImpulseVM` (`OP_LANE_INIT_SEEDS` `0xC0` + `OP_LANE_CSR_WALK` `0xC3` with `IMP_FLAG_DELTA_FIXPOINT = 0x04`) against clean-room `MITra-BFS` (Jacobi level-synchronous) and `MITra-RCH` (`vid`-ordered Gauss-Seidel) across two snapshot scales:

| Snapshot | Nodes ($N$) | Edges ($E$) | `K=8` Impulse / BFS / RCH (ms) | `K=16` Impulse / BFS / RCH (ms) | `K=64` Impulse / BFS / RCH (ms) | `K=512` Impulse / BFS / RCH (ms) |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: |
| `synth_medium.imps` | `100,000` | `1,000,000` | `5.579` / `4.864` / **`1.591`** | `6.931` / `6.172` / **`1.716`** | `8.081` / `7.331` / **`2.106`** | `64.076` / `30.476` / **`15.452`** |
| `synth_large.imps` | `1,000,000` | `10,000,000` | `72.939` / `62.472` / **`19.931`** | `93.854` / `79.885` / **`21.646`** | `112.082` / `98.869` / **`31.872`** | `890.898` / `1160.480` / **`501.417`** |

Two root causes explain the `1.78x–5.45x` gap vs. `MITra-RCH` (`3.52x` at `K=64` on `synth_large`):

1. **Redundant Hub Re-Expansions & 3-Buffer Footprint in Level-Synchronous `FIXPOINT` (`lane_walk_common.hpp:480-678`)**:
   * Current `FIXPOINT` maintains **three** per-node lane buffers (`lf_dst.lanes`, `delta`, `next_delta`) and **three** `active_words` bitsets (`lf_dst.active_words`, `delta_active_words`, `next_delta_active_words`).
   * Whenever lane $b_1$ reaches high-degree hub $h$ at hop 2 and lane $b_2$ reaches $h$ at hop 3, synchronous Jacobi expands $h$'s outgoing CSR adjacency list **twice**.
   * Because boolean reachability `(WordT, |, 0)` is an associative, commutative, idempotent **join-semilattice**, updating `lanes[v]` and `delta[v]` **in-place** while sweeping `active_words` in topological/vertex-ID order (`MITra-RCH`) cascades forward edges ($v > u$ on `0xC3`, $u < v$ on `0xC4`) within the **same sweep**, coalescing staggered hub arrivals and eliminating `next_delta` + `next_delta_active_words` (`-33%` working set).
2. **Random Scatter RMW Cache/TLB Thrashing on Dense Sweeps**:
   * On `synth_large`, middle rounds reach nearly 100% of vertices, issuing `10M` random cache-line read-modify-writes (`lanes[v] |= nb`, `delta[v] |= nb`).
   * When active vertex density exceeds `N / 20` (or `active_edges >= E / 20`, matching `OP_ADAPTIVE_WALK` `0x1D` in `op_1d_adaptive_walk.inc:27-28`) and `.imps` CSC segments are present, switching that sweep to **sequential-write Pull-CSC (`v = 0..N-1`)** with immediate lane-saturation skip (`lanes[v] == active_mask_w`) replaces `10M` random writes with at most `N` sequential writes and skips already-saturated vertices in $O(1)$ before touching `csc_offsets[v]`.

---

## 2. Mathematical Soundness & Five Mandatory Correctness Guards

### 2.1 Semilattice Fixed-Point Equivalence
Let $G_b = (V, E \setminus T_b)$ be the static directed graph visible to lane $b \in [0, K)$ after applying optional counterfactual edge trip masks $T_b$ (`OP_LANE_SET_EDGE_MASK` `0xC2`). Define the monotone inflation operator $F: \mathcal{P}(V)^K \to \mathcal{P}(V)^K$:
$$F_b(X_b) = X_b \cup \{v \in V \mid \exists (u, v) \in E \setminus T_b,\; u \in X_b\}$$
By Tarski's / chaotic-iteration fixed-point theorem on finite join-semilattices, any fair asynchronous/Gauss-Seidel schedule that expands every newly set `(u, b)` bit until `delta == 0` converges to the **unique least fixed point** $X^* = \bigcup_{t \ge 0} F^t(S)$ — **100% bit-for-bit identical** to level-synchronous Jacobi (`MITra-BFS`), including on cyclic graphs, self-loops, disconnected components, and `0xC2` counterfactual topologies.

### 2.2 Five Mandatory Engineering Guards

#### Guard 1 — Bounded Hops (`ctx->has_fixpoint_max_hops`) MUST Use Synchronous Jacobi
* In `lane_walk_common.hpp:646-648`, `vm_state->query_context->has_fixpoint_max_hops` restricts closure to at most $k = \text{fixpoint\_max\_hops}$ hops (`*1..k`).
* In-place Gauss-Seidel cascades arbitrarily long forward chains `0 -> 1 -> ... -> m` in **sweep 1**, which would include nodes at distance $> k$.
* **Hard Rule**:
  ```cpp
  const bool use_gauss_seidel = !vm_state->query_context->has_fixpoint_max_hops;
  ```
  When `has_fixpoint_max_hops == true`, execute the existing 2-scratch synchronous `delta -> next_delta` loop verbatim.

#### Guard 2 — Sweep Polarity for `0xC3` (`CSR`) vs. `0xC4` (`CSC`)
* **`0xC3` (`OP_LANE_CSR_WALK`, `IsCsc == false`)**: Forward DAG / creation-ordered edges predominantly satisfy `u < v`. Sweep `w_idx` **ascending** (`0 .. num_words - 1`), extracting bits low-to-high via `std::countr_zero(word)`:
  * When target `v` gains `nb = prop & ~lanes[v]` (`nb != 0`):
    * `lanes[v] |= nb; delta[v] |= nb;`
    * `size_t vw = v >> 6; uint64_t vbit = 1ULL << (v & 63);`
    * If `v <= u`: `delta_active_words[vw] |= vbit; any_back_edge = true;` (expands on next sweep).
    * Else if `vw == w_idx`: `word |= vbit;` (expands later in the **current** 64-node word iteration!).
    * Else (`vw > w_idx`): `delta_active_words[vw] |= vbit;` (expands later in the **current** ascending `w_idx` sweep!).
* **`0xC4` (`OP_LANE_CSC_WALK`, `IsCsc == true`)**: Reverse edges `v -> u` on the same graph predominantly satisfy `u < v` (descending IDs). Sweep `w_idx` **descending** (`num_words - 1` down to `0`), extracting bits high-to-low via `63u - static_cast<uint32_t>(std::countl_zero(word))`:
  * If `u >= v`: `delta_active_words[uw] |= ubit; any_back_edge = true;`
  * Else if `uw == w_idx`: `word |= ubit;` (expands later in the current descending word!).
  * Else (`uw < w_idx`): `delta_active_words[uw] |= ubit;` (expands later in the current descending sweep!).

#### Guard 3 — Direction-Optimizing Push-CSR / Pull-CSC Gate & Saturation Short-Circuit
On an unbounded `FIXPOINT` sweep of a single physical relation `s` (non-virtual, `!s.is_raw_coo`):
* **Eligibility Gate**:
  1. `!IsCsc`: Pull uses CSC (`s.csc_offsets_ptr != nullptr && s.csc_targets_ptr != nullptr`) when `cur_trip == nullptr` (or `s.csc_to_csr_edge_idx` is populated) and `csr_n == dst_n`.
  2. `IsCsc`: Pull uses forward CSR (`s.offsets_ptr != nullptr && s.targets_ptr != nullptr`) when `cur_trip == nullptr` and `csc_n == dst_n`.
* **Density Heuristic (Zero Allocation, $O(N/64)$ Popcount)**:
  Count `active_nodes` across `delta_active_words[0 .. num_words - 1]` via `std::popcount`. Switch to Pull for the current sweep iff:
  $$\text{active\_nodes} \ge \max\!\left(64,\; \frac{N}{20}\right)$$
  (matching `OP_1D_PULL_NODE_DIVISOR = 20u` in `op_1d_adaptive_walk.inc:28`).
* **Pull Kernel with Early Saturation Skip**:
  ```cpp
  // Forward reachability Pull-CSC sweep over packed WordT
  bool any_new = false;
  for (size_t v = 0; v < dst_n; ++v) {
      WordT cur_v = lanes[v];
      if (cur_v == active_mask_w) continue; // All K lanes already reached v!
      WordT acc = cur_v;
      uint64_t start = csc_off[v];
      uint64_t end   = csc_off[v + 1];
      for (uint64_t e = start; e < end; ++e) {
          uint32_t u = csc_tgt[e];
          acc = static_cast<WordT>(acc | lanes[u]); // Gauss-Seidel: reads live lanes[u]!
          if (acc == active_mask_w) break;          // Saturated early!
      }
      WordT nb = static_cast<WordT>((acc & active_mask_w) & static_cast<WordT>(~cur_v));
      if (nb != 0) {
          lanes[v] = static_cast<WordT>(cur_v | nb);
          delta[v] = static_cast<WordT>(delta[v] | nb);
          delta_active_words[v >> 6] |= (1ULL << (v & 63));
          lf_dst.active_words[v >> 6] |= (1ULL << (v & 63));
          any_new = true;
      }
  }
  ```
  Notice a subtle interaction when transitioning **Pull $\to$ Push** on a subsequent sparse sweep:
  * During a Pull sweep, `delta` must first clear stale bits from nodes expanded by Pull (or Pull reads `delta[u]` and clears `delta` beforehand, writing only `nb` into `next` `delta`).
  * **Cleanest Exact Invariant**: Before running a Pull sweep, clear `delta` sparsely via `delta_active_words` ($O(\text{active})$), then let Pull read `lanes[u]` directly (`acc |= lanes[u]` — valid because $(X \cup \Delta)$ in monotone reachability equals `lanes`!) and write **only newly discovered bits** `nb = (acc & active_mask_w) & ~lanes[v]` into both `lanes[v]` and `delta[v]` + `delta_active_words`! That way, when the next sweep drops back below `N / 20` into sparse `MITra-RCH` Push, `delta` and `delta_active_words` hold the **exact** frontier of newly reached `(v, lane)` pairs!

#### Guard 4 — Scratch Memory Reduction (Stacked on `#83`)
* Under `#83`, `impulse_vm_context` allocates two `8 B/node` scratch slabs (`scratch_lanes_memory` and `scratch_lanes_b_memory`).
* Unbounded `MITra-RCH` + Pull-CSC needs **only `scratch_lanes_memory` (`delta`)**; `scratch_lanes_b_memory` is touched **only** when `ctx->has_fixpoint_max_hops == true`!
* On exit (including early return on `IMPULSE_VM_ERR_GAS_EXHAUSTED`), sparse-clear `delta` via `delta_active_words` so `scratch_lanes_memory` maintains `#83`'s all-zero invariant without $O(N)$ `memset`.

#### Guard 5 — 4-Way Polyglot Fuel, `DEFINED_FLAGS` & `ZF` Alignment (`AGENTS.md` §3.18)
Auditing `0xC3`/`0xC4` across C++, Java, and OCaml surfaced three pre-existing discrepancies that MUST be unified alongside `MITra-RCH`:
1. **`DEFINED_FLAGS` Macro (`op_c3_lane_csr_walk.inc:18`, `op_c4_lane_csc_walk.inc:18`)**:
   Update `0x03u` $\to$ `0x07u` (`IMP_FLAG_ACCUMULATE_DEST | 0x02u | IMP_FLAG_DELTA_FIXPOINT`) so contract/fuzzer flag masks recognize `0x04` (`FIXPOINT`).
2. **`ZF` Flag Semantics on `FIXPOINT` (`lane_walk_common.hpp:737-742`)**:
   `VM_SPECIFICATION.md` §6.11.2 (`line 500`) normatively specifies: *"Sets `flags[ZF] = 1` iff all `out_lanes[u] == 0`."* Java (`VmMultiLaneHandlers.java:980`) and OCaml (`vm_multilane.ml:584`) follow the spec (`!any_active`), whereas C++ currently sets `ZF = !any_changed` when `is_fixpoint` is true. Align C++ `ZF` to `!any_set` (keeping `ST = !any_changed` if needed for internal convergence checks, or verify no `.impas` vector expects `ZF == !any_changed`).
3. **Deterministic Fuel Accounting Across Engines**:
   * Currently C++ (`lane_walk_common.hpp:651-657`) and Java (`VmMultiLaneHandlers.java:957`) charge 1 fuel unit per inner `FIXPOINT` round, while OCaml (`vm_multilane.ml:554`) charges 0 inner-loop fuel (only 1 instruction-level fuel in `vm.ml`).
   * If C++ switches from Jacobi ($D$ rounds) to Gauss-Seidel ($S \le D$ sweeps, or Pull sweeps), charging per outer sweep without matching OCaml/Java would break 4-way parity on low-fuel tests.
   * **Unified Rule**: Charge **1 fuel unit per outer `FIXPOINT` sweep** across **all three engines** (C++, Java, and OCaml — implementing the exact same ascending/descending `MITra-RCH` sweep order in `vm_multilane.ml` when `not has_fixpoint_max_hops`, while Pull-CSC is gated so fuel per sweep remains 1 sweep = 1 fuel, OR charge fuel strictly at instruction dispatch + cap `max_iter = 1024` identically across all 4 engines).
   * **Recommended Choice**: Charge fuel at instruction dispatch (matching OCaml's current behavior and `OP_CC_AFFOREST` / `OP_LOUVAIN_STEP`) while enforcing a deterministic `max_sweeps = 1024` safety bound (and checking an interrupt/watchdog flag if configured). That makes fuel consumption **100% invariant to Push vs. Pull-CSC crossover heuristics** across C++, Java, and OCaml!

---

## 3. File-by-File Implementation Plan

1. **`impulse-cpp/src/opcodes/op_c3_lane_csr_walk.inc` & `op_c4_lane_csc_walk.inc`**:
   * Update `OP_C3_DEFINED_FLAGS` and `OP_C4_DEFINED_FLAGS` to `0x07u`.
2. **`impulse-cpp/src/opcodes/shared/lane_walk_common.hpp`**:
   * Refactor `if (is_fixpoint)` into two clean branches over `#83`'s packed `WordT*` (`uint8_t`, `uint16_t`, `uint32_t`, `uint64_t`):
     * **Branch A (`ctx->has_fixpoint_max_hops == true`)**: Existing level-synchronous Jacobi (`delta` $\to$ `next_delta`) bounded by `ctx->fixpoint_max_hops`.
     * **Branch B (`!ctx->has_fixpoint_max_hops`)**: `execute_fixpoint_rch_adaptive<WordT, IsCsc>()`:
       * Single scratch `WordT* delta` (`ctx->scratch_lanes_memory`) + `ctx->scratch_active_words`.
       * Per-sweep `std::popcount` active-node check (`>= max(64, dst_n / 20)`) dispatching Pull-CSC (`!IsCsc`) / Pull-CSR (`IsCsc`) when eligible, otherwise ascending (`!IsCsc`) / descending (`IsCsc`) in-place Gauss-Seidel `MITra-RCH` push sweep.
       * Sparse `delta` cleanup before return.
   * Align `IMP_VM_SET_ZF(vm_state, !any_set)` with `VM_SPECIFICATION.md` §6.11.2.
3. **`impulse-benchmarks/cpp-benchmarks/src/mitra_aside_bench.cpp`** *(verification benchmark update)*:
   * Pre-allocate `MITra-BFS` and `MITra-RCH` scratch vectors outside `t0..t1` (eliminating OS page-fault bias at `K=256..512`) and add an optional `--no-copyout` / `OP_LANE_REDUCE_AND` mode to measure pure `ImpulseVM` kernel execution alongside end-to-end C-API copy-out.

---

## 4. Required Tests & Verification Gates

1. **`MitraRchTopologyEquivalenceTest` (`impulse-cpp/tests/test_multilane_rch_fixpoint.cpp`)**:
   * Compare `MITra-RCH` (`!has_fixpoint_max_hops`) against a reference synchronous Jacobi oracle across all 4 widths (`K = 7, 8, 15, 16, 31, 32, 63, 64`) on:
     1. Forward topological DAG (`0 -> 1 -> ... -> 255` — verifies 1-sweep convergence).
     2. Worst-case reverse-numbered chain (`255 -> 254 -> ... -> 0` — verifies `any_back_edge` multi-sweep correctness across 64-bit word boundaries `64 -> 63`, `128 -> 127`).
     3. Intersecting cycles + self-loops + high-degree hubs (`deg > 128`).
     4. Virtual super-relations (`s.is_virtual` with 3 child relations).
     5. Counterfactual `0xC2` `N-1` and `N-2` edge trip masks (verifying tripped edges never leak across forward cascades).
2. **`BoundedMaxHopsIsolationTest`**:
   * On chain `0 -> 1 -> 2 -> ... -> 20`, set `fixpoint_max_hops = 3` and assert nodes `4..20` remain `0` (verifying Jacobi fallback).
3. **`AdaptivePullCscCrossoverAndSaturationTest`**:
   * Construct a `2,048`-node graph (`N / 20 = 102`) where hop 1 activates `150` nodes (triggering Pull-CSC) and hop 2 activates `10` tail nodes (switching back to Push-CSR `MITra-RCH`), asserting bit-exact parity on both `0xC3` and `0xC4` and verifying `lanes[v] == active_mask_w` saturation skip.
4. **Polyglot & Sanitizer Gates**:
   * `ctest --output-on-failure` under ASan + UBSan (`build_on` `-O3` and `build_off` `-O0`).
   * `python3 ~/impulse/impulse-verification/tools/four_way_validation.py` & `fuzz_vm_contracts.py` — 100% 4-way bitwise and ICTX v1 parity.
