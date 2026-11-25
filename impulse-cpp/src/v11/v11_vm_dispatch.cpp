/**
 * @file v11_vm_dispatch.cpp
 * @brief C-ABI entrypoints and computed-goto / switch dispatch engine for ImpulseVM v0.11.0.
 *
 * Individual opcode implementations reside in `src/v11/opcodes/op_<hex>_<name>.inc` and are
 * included directly into the dispatch loop (matching the v0.10 architecture).
 */
#if defined(__clang__) || defined(__GNUC__)
  #pragma STDC FP_CONTRACT OFF
#endif

#include "v11_internal.hpp"
#include "opcodes/shared/v11_p1_helpers.hpp"
#include "opcodes/shared/v11_p2_helpers.hpp"
#include "opcodes/shared/v11_p3_helpers.hpp"
#include "opcodes/shared/v11_p4_helpers.hpp"
#include "opcodes/shared/v11_p5_helpers.hpp"
#include "impulse_graph_internal.hpp"

#include <atomic>
#include <cstdint>
#include <cstring>
#include <new>

#if !defined(IMPULSE_DISABLE_COMPUTED_GOTO) && (defined(__GNUC__) || defined(__clang__))
  #define V11_HAS_COMPUTED_GOTO 1
#else
  #define V11_HAS_COMPUTED_GOTO 0
#endif

#if defined(__clang__)
  #pragma clang diagnostic push
  #pragma clang diagnostic ignored "-Wgnu-label-as-value"
#elif defined(__GNUC__)
  #pragma GCC diagnostic push
  #pragma GCC diagnostic ignored "-Wpedantic"
#endif

namespace impulse::vm::v11 {

static impulse_vm_status_v11_t run_words_unlocked(
    const uint64_t* words,
    size_t word_count,
    const V11SnapshotView* snap,
    impulse_vm_workspace_v11_t& ws,
    uint64_t max_steps,
    uint32_t entry_pc,
    bool already_validated
) noexcept {
    if (!already_validated) {
        impulse_vm_validation_v11_t vr = impulse_vm_validate_words_v11(words, word_count);
        if (vr.status != IMPULSE_VM_V11_OK) {
            ws.state.pc = vr.bad_pc;
            ws.status = vr.status;
            return ws.status;
        }
    }

    ws.state.pc = entry_pc;
    ws.status = IMPULSE_VM_V11_OK;
    ws.gas_remaining = max_steps;
    uint64_t steps = 0ULL;
    uint64_t w = 0ULL;

#if V11_HAS_COMPUTED_GOTO
    static const void* dispatch_table[256];
    static std::atomic<int> dispatch_init{0};
    if (dispatch_init.load(std::memory_order_acquire) != 2) {
        int expected = 0;
        if (dispatch_init.compare_exchange_strong(expected, 1, std::memory_order_acq_rel)) {
            for (int i = 0; i < 256; ++i) dispatch_table[i] = &&op_INVALID;
#define V11_REGISTER_OP(CODE, NAME) dispatch_table[CODE] = &&op_##NAME;
            V11_IMPLEMENTED_OPS(V11_REGISTER_OP)
#undef V11_REGISTER_OP
            dispatch_init.store(2, std::memory_order_release);
        } else {
            while (dispatch_init.load(std::memory_order_acquire) != 2) {}
        }
    }

  #define V11_OP_CASE(NAME) op_##NAME:
  #define V11_DISPATCH() do { \
      if (ws.status != IMPULSE_VM_V11_OK) return ws.status; \
      if (steps >= max_steps) { ws.status = IMPULSE_VM_V11_ERR_GAS_EXHAUSTED; return ws.status; } \
      if (static_cast<size_t>(ws.state.pc) >= word_count) { ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; return ws.status; } \
      ++steps; \
      if (ws.gas_remaining > 0ULL) --ws.gas_remaining; \
      w = words[ws.state.pc]; \
      goto *dispatch_table[IMP_V11_OPCODE(w)]; \
  } while (0)

    V11_DISPATCH();
#else
  #define V11_OP_CASE(NAME) case static_cast<uint8_t>(Opcode::OP_##NAME):
  #define V11_DISPATCH() goto v11_switch_next

    while (ws.status == IMPULSE_VM_V11_OK) {
      v11_switch_next:
        if (ws.status != IMPULSE_VM_V11_OK) {
            return ws.status;
        }
        if (steps >= max_steps) {
            ws.status = IMPULSE_VM_V11_ERR_GAS_EXHAUSTED;
            return ws.status;
        }
        if (static_cast<size_t>(ws.state.pc) >= word_count) {
            ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS;
            return ws.status;
        }
        ++steps;
        if (ws.gas_remaining > 0ULL) --ws.gas_remaining;
        w = words[ws.state.pc];
        switch (IMP_V11_OPCODE(w)) {
#endif

    // --- Pillar 1: Control, Scalar ALU, 3VL, Windows & Spills (0x00..0x14) ---
    #include "opcodes/op_00_halt.inc"
    #include "opcodes/op_01_nop.inc"
    #include "opcodes/op_02_trap.inc"
    #include "opcodes/op_03_throw.inc"
    #include "opcodes/op_04_assert.inc"
    #include "opcodes/op_05_load_const.inc"
    #include "opcodes/op_06_load_const_pool.inc"
    #include "opcodes/op_07_load_param.inc"
    #include "opcodes/op_08_mov.inc"
    #include "opcodes/op_09_swap.inc"
    #include "opcodes/op_0a_spill.inc"
    #include "opcodes/op_0b_reload.inc"
    #include "opcodes/op_0c_scalar_int_alu.inc"
    #include "opcodes/op_0d_scalar_fp_alu.inc"
    #include "opcodes/op_0e_scalar_3vl.inc"
    #include "opcodes/op_0f_jmp.inc"
    #include "opcodes/op_10_jcc.inc"
    #include "opcodes/op_11_loop_decr.inc"
    #include "opcodes/op_12_call.inc"
    #include "opcodes/op_13_ret.inc"
    #include "opcodes/op_14_set_max_dop.inc"

    // --- Pillar 2: 3-Tier Adaptive Frontiers & Set Algebra (0x20..0x27) ---
    #include "opcodes/op_20_fseed.inc"
    #include "opcodes/op_21_init_domain_span.inc"
    #include "opcodes/op_22_set_op.inc"
    #include "opcodes/op_23_set_complement.inc"
    #include "opcodes/op_24_set_cardinality.inc"
    #include "opcodes/op_25_set_contains.inc"
    #include "opcodes/op_26_set_convert.inc"
    #include "opcodes/op_27_stable_check.inc"

    // --- Pillar 3: Topology, SpMV, Lexical Morsels & Structural Kernels (0x30..0x3B) ---
    #include "opcodes/op_30_walk.inc"
    #include "opcodes/op_31_walk_filtered.inc"
    #include "opcodes/op_33_degree.inc"
    #include "opcodes/op_34_spmv.inc"
    #include "opcodes/op_35_walk_morsel.inc"
    #include "opcodes/op_36_filter_morsel.inc"
    #include "opcodes/op_37_morsel_commit.inc"
    #include "opcodes/op_38_morsel_end.inc"
    #include "opcodes/op_39_sample_neighbors.inc"
    #include "opcodes/op_3a_has_topology.inc"
    #include "opcodes/op_3b_graph_kernel.inc"

    // --- Pillar 4: Vectorized Columnar, 3VL, Math, Strings, Keys & Collect (0x40..0x57) ---
    #include "opcodes/op_40_load_column.inc"
    #include "opcodes/op_41_gather_node_attr.inc"
    #include "opcodes/op_42_gather_edge_attr.inc"
    #include "opcodes/op_43_gather_vec.inc"
    #include "opcodes/op_44_node_filter.inc"
    #include "opcodes/op_45_vec_cmp.inc"
    #include "opcodes/op_46_vec_3vl_op.inc"
    #include "opcodes/op_47_vec_str_pred.inc"
    #include "opcodes/op_48_vec_math_arith.inc"
    #include "opcodes/op_49_vec_math_exp_log.inc"
    #include "opcodes/op_4a_vec_math_trig_hyp.inc"
    #include "opcodes/op_4b_vec_math_act.inc"
    #include "opcodes/op_4c_vec_int_bitwise.inc"
    #include "opcodes/op_4d_vec_ternary.inc"
    #include "opcodes/op_4e_vec_blend.inc"
    #include "opcodes/op_4f_coalesce.inc"
    #include "opcodes/op_50_vec_init.inc"
    #include "opcodes/op_51_vec_point.inc"
    #include "opcodes/op_52_vec_reduce.inc"
    #include "opcodes/op_53_top_k.inc"
    #include "opcodes/op_54_index_scan.inc"
    #include "opcodes/op_55_map_keys_to_dense.inc"
    #include "opcodes/op_56_map_dense_to_keys.inc"
    #include "opcodes/op_57_collect.inc"

    // --- Pillar 5: Multi-Lane, Vector ANN/Rerank/Pool & MITra LANE_STATE (0x60..0x6F) ---
    #include "opcodes/op_60_lane_init_seeds.inc"
    #include "opcodes/op_61_lane_init_groups.inc"
    #include "opcodes/op_63_lane_walk.inc"
    #include "opcodes/op_64_lane_bitwise.inc"
    #include "opcodes/op_65_lane_filter_quorum.inc"
    #include "opcodes/op_66_lane_reduce_and.inc"
    #include "opcodes/op_67_lane_reduce_attr.inc"
    #include "opcodes/op_69_vector_ann_search.inc"
    #include "opcodes/op_6a_vector_rerank.inc"
    #include "opcodes/op_6b_lane_state_init.inc"
    #include "opcodes/op_6c_lane_state_walk.inc"
    #include "opcodes/op_6d_lane_state_op.inc"
    #include "opcodes/op_6e_lane_state_reduce.inc"
    #include "opcodes/op_6f_vector_pool.inc"

#if V11_HAS_COMPUTED_GOTO
    op_INVALID:
        ws.status = IMPULSE_VM_V11_ERR_INVALID_OPCODE;
        return ws.status;
#else
        default:
            ws.status = IMPULSE_VM_V11_ERR_INVALID_OPCODE;
            return ws.status;
        }
    }
    return ws.status;
#endif

#undef V11_OP_CASE
#undef V11_DISPATCH
}

impulse_vm_status_v11_t execute_words_internal(
    const uint64_t* words,
    size_t word_count,
    const V11SnapshotView* snap,
    impulse_vm_workspace_v11_t& ws,
    uint64_t max_steps,
    uint32_t entry_pc,
    bool already_validated
) noexcept {
    uint32_t expected = 0u;
    if (!ws.in_use.compare_exchange_strong(expected, 1u, std::memory_order_acquire)) {
        return IMPULSE_VM_V11_ERR_INVALID_OPERAND;
    }
    struct InUseGuard {
        std::atomic<uint32_t>& flag;
        ~InUseGuard() noexcept { flag.store(0u, std::memory_order_release); }
    } guard{ws.in_use};

    return run_words_unlocked(words, word_count, snap, ws, max_steps, entry_pc, already_validated);
}

} // namespace impulse::vm::v11

#if defined(__clang__)
  #pragma clang diagnostic pop
#elif defined(__GNUC__)
  #pragma GCC diagnostic pop
#endif

extern "C" IMPULSE_API impulse_vm_status_v11_t impulse_workspace_init(
    void* scratch_buf,
    size_t scratch_bytes,
    impulse_vm_workspace_v11_t** out_ws
) {
    using namespace impulse::vm::v11;
    if (!scratch_buf || !out_ws) {
        return IMPULSE_VM_V11_ERR_INVALID_OPERAND;
    }
    if ((reinterpret_cast<uintptr_t>(scratch_buf) & 127u) != 0u) {
        return IMPULSE_VM_V11_ERR_INVALID_OPERAND;
    }
    size_t header_aligned = (sizeof(impulse_vm_workspace_v11_t) + 127u) & ~size_t(127u);
    if (scratch_bytes < header_aligned + 1024u) {
        return IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW;
    }

    auto* ws = new (scratch_buf) impulse_vm_workspace_v11_t();
    ws->magic = kWorkspaceMagic;
    ws->total_capacity = scratch_bytes;
    ws->in_use.store(0u, std::memory_order_relaxed);
    ws->frontier_epoch = 1ULL;
    ws->host_max_threads = 16u;
    ws->max_call_depth = static_cast<uint8_t>(kMaxCallDepth);
    ws->geometry = kGeomP0;
    ws->const_pool_count = 0u;
    ws->param_count = 0u;
    ws->str_count = 0u;
    ws->output_count = 0u;

    ws->arena.base = static_cast<uint8_t*>(scratch_buf) + header_aligned;
    ws->arena.cap  = scratch_bytes - header_aligned;
    ws->arena.pos  = 0u;
    ws->arena.mark = 0u;

    ws->reset_execution_state();
    *out_ws = ws;
    return IMPULSE_VM_V11_OK;
}

extern "C" IMPULSE_API impulse_vm_status_v11_t impulse_vm_execute_v11(
    const void* impb_bytes,
    size_t impb_len,
    const impulse_snapshot_t* snapshot,
    const impulse_vm_params_v11_t* params,
    impulse_vm_workspace_v11_t* scratch_ws,
    void* result_buf,
    size_t result_cap,
    size_t* out_result_bytes
) {
    using namespace impulse::vm::v11;
    if (!impb_bytes || !scratch_ws || scratch_ws->magic != kWorkspaceMagic) {
        return IMPULSE_VM_V11_ERR_INVALID_OPERAND;
    }
    if (result_buf != nullptr && (reinterpret_cast<uintptr_t>(result_buf) & 63u) != 0u) {
        return IMPULSE_VM_V11_ERR_INVALID_OPERAND;
    }
    if (out_result_bytes) {
        *out_result_bytes = 0u;
    }
    (void)result_cap;

    // Acquire non-reentrancy CAS guard BEFORE mutating any workspace state!
    uint32_t expected = 0u;
    if (!scratch_ws->in_use.compare_exchange_strong(expected, 1u, std::memory_order_acquire)) {
        return IMPULSE_VM_V11_ERR_INVALID_OPERAND;
    }
    struct InUseGuard {
        std::atomic<uint32_t>& flag;
        ~InUseGuard() noexcept { flag.store(0u, std::memory_order_release); }
    } guard{scratch_ws->in_use};

    impulse_vm_validation_v11_t vr = impulse_vm_validate_v11(impb_bytes, impb_len);
    if (vr.status != IMPULSE_VM_V11_OK) {
        scratch_ws->state.pc = vr.bad_pc;
        scratch_ws->status = vr.status;
        return vr.status;
    }

    impulse_impb_header_v11_t hdr{};
    std::memcpy(&hdr, impb_bytes, sizeof(hdr));

    if (hdr.required_domains > 0u) {
        if (!snapshot) {
            scratch_ws->status = IMPULSE_VM_V11_ERR_NULL_SNAPSHOT;
            return IMPULSE_VM_V11_ERR_NULL_SNAPSHOT;
        }
        if (snapshot->domains.size() < hdr.required_domains) {
            scratch_ws->status = IMPULSE_VM_V11_ERR_DOMAIN_MISMATCH;
            return IMPULSE_VM_V11_ERR_DOMAIN_MISMATCH;
        }
    }

    scratch_ws->reset_execution_state();
    scratch_ws->max_call_depth = hdr.max_call_depth;
    if (params && params->geometry_override) {
        scratch_ws->geometry = *params->geometry_override;
    } else {
        scratch_ws->geometry = kGeomP0;
    }
    uint64_t gas = (params && params->gas_limit > 0ULL) ? params->gas_limit : UINT64_MAX;

    uint64_t* words = scratch_ws->arena.alloc_array<uint64_t>(hdr.code_word_count, 8);
    if (!words) {
        scratch_ws->status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW;
        return IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW;
    }
    std::memcpy(words,
                static_cast<const uint8_t*>(impb_bytes) + sizeof(hdr),
                static_cast<size_t>(hdr.code_word_count) * sizeof(uint64_t));

    V11SnapshotView view{};
    const V11SnapshotView* view_ptr = nullptr;
    if (snapshot != nullptr) {
        size_t nd = snapshot->domains.size();
        uint64_t* dom_cards = (nd > 0) ? scratch_ws->arena.alloc_array<uint64_t>(nd, 8) : nullptr;
        if (nd > 0 && !dom_cards) {
            scratch_ws->status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW;
            return IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW;
        }
        for (size_t i = 0; i < nd; ++i) {
            dom_cards[i] = snapshot->domains[i].node_count;
        }
        view.domain_cards = dom_cards;
        view.domain_count = static_cast<uint32_t>(nd);
        view.raw_snapshot = snapshot;
        view_ptr = &view;
    }

    return run_words_unlocked(words, hdr.code_word_count, view_ptr, *scratch_ws, gas, hdr.entry_pc, true);
}

extern "C" IMPULSE_API const impulse_vm_state_v11_t* impulse_vm_v11_get_state(
    const impulse_vm_workspace_v11_t* ws
) {
    return ws ? &ws->state : nullptr;
}
