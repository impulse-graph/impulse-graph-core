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

#include "vm_internal.hpp"
#include "opcodes/shared/v11_p1_helpers.hpp"
#include "opcodes/shared/v11_decimal64.hpp"
#include "opcodes/shared/v11_p2_helpers.hpp"
#include "opcodes/shared/v11_p3_helpers.hpp"
#include "opcodes/shared/v11_p4_helpers.hpp"
#include "opcodes/shared/v11_p5_helpers.hpp"
#include "impulse_graph_internal.hpp"

#include <atomic>
#include <bit>
#include <cstdint>
#include <cstring>
#include <limits>
#include <mutex>
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

namespace impulse::vm {

#if defined(_MSC_VER)
  #pragma optimize("g", off)
#endif
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
#ifdef IMPULSE_V11_OPCODE_PROFILE
    uint8_t prof_op = 0xFFu;
    uint64_t prof_t0 = 0ULL;
  #define V11_PROF_FINISH() do { \
      if (prof_op != 0xFFu) { \
          v11_profile_record(ws, prof_op, v11_profile_now_ns() - prof_t0); \
          prof_op = 0xFFu; \
      } \
  } while (0)
  #define V11_PROF_BEGIN(OP) do { \
      uint8_t _pop = (OP); \
      if (_pop >= 0x20u && _pop < 128u) { \
          prof_op = _pop; \
          prof_t0 = v11_profile_now_ns(); \
      } else { \
          if (_pop < 128u) ws.op_stats[_pop].calls += 1ULL; \
          prof_op = 0xFFu; \
      } \
  } while (0)
#else
  #define V11_PROF_FINISH() ((void)0)
  #define V11_PROF_BEGIN(OP) ((void)0)
#endif

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
      V11_PROF_FINISH(); \
      if (ws.status != IMPULSE_VM_V11_OK) return ws.status; \
      if (steps >= max_steps) { ws.status = IMPULSE_VM_V11_ERR_GAS_EXHAUSTED; return ws.status; } \
      if (static_cast<size_t>(ws.state.pc) >= word_count) { ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; return ws.status; } \
      ++steps; \
      if (ws.gas_remaining > 0ULL) --ws.gas_remaining; \
      w = words[ws.state.pc]; \
      uint8_t _dop = IMP_V11_OPCODE(w); \
      V11_PROF_BEGIN(_dop); \
      goto *dispatch_table[_dop]; \
  } while (0)

    V11_DISPATCH();
#else
  #define V11_OP_CASE(NAME) case static_cast<uint8_t>(Opcode::OP_##NAME):
  #define V11_DISPATCH() goto v11_switch_next

    while (ws.status == IMPULSE_VM_V11_OK) {
      v11_switch_next:
        V11_PROF_FINISH();
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
        uint8_t _dop = IMP_V11_OPCODE(w);
        V11_PROF_BEGIN(_dop);
        switch (_dop) {
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
    #include "opcodes/op_15_scalar_decimal_alu.inc"

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
    #include "opcodes/op_58_vec_decimal_alu.inc"

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

bool v11_exec_morsel_p4_op(
    impulse_vm_workspace_v11_t& ws,
    const V11SnapshotView* snap,
    uint64_t w
) noexcept {
    (void)snap;
    const uint8_t _dop = IMP_V11_OPCODE(w);
#define V11_OP_CASE(NAME) case static_cast<uint8_t>(Opcode::OP_##NAME):
#define V11_DISPATCH() return true
    switch (_dop) {
        #include "opcodes/op_43_gather_vec.inc"
        #include "opcodes/op_45_vec_cmp.inc"
        #include "opcodes/op_46_vec_3vl_op.inc"
        #include "opcodes/op_47_vec_str_pred.inc"
        #include "opcodes/op_49_vec_math_exp_log.inc"
        #include "opcodes/op_4a_vec_math_trig_hyp.inc"
        #include "opcodes/op_4b_vec_math_act.inc"
        #include "opcodes/op_4c_vec_int_bitwise.inc"
        #include "opcodes/op_4d_vec_ternary.inc"
        #include "opcodes/op_4e_vec_blend.inc"
        #include "opcodes/op_58_vec_decimal_alu.inc"
        default:
            ws.status = IMPULSE_VM_V11_ERR_INVALID_OPCODE;
            return false;
    }
#undef V11_OP_CASE
#undef V11_DISPATCH
}

#if defined(_MSC_VER)
  #pragma optimize("", on)
#endif

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

} // namespace impulse::vm

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
    using namespace impulse::vm;
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

using namespace impulse::vm;

namespace {

V11Vector* decode_imps_attr_to_vec(const uint8_t* raw,
                                   size_t file_size,
                                   const impulse_attribute_descriptor_t& desc,
                                   uint64_t row_count,
                                   V11Arena& arena) noexcept {
    if (row_count > static_cast<uint64_t>(UINT32_MAX)) return nullptr;
    const uint32_t n = static_cast<uint32_t>(row_count);
    const uint8_t base_type = desc.type_code & 0x7Fu;
    const bool is_nullable = (desc.type_code & 0x80u) != 0u;
    if (desc.data_offset > file_size || desc.data_bytes > file_size - desc.data_offset) {
        return nullptr;
    }
    const uint8_t* ptr = raw + desc.data_offset;
    size_t payload_bytes = static_cast<size_t>(desc.data_bytes);
    const size_t vwords = (n > 0u) ? ((static_cast<size_t>(n) + 63u) >> 6) : 1u;
    uint64_t* valid_bits = arena.alloc_array<uint64_t>(vwords, 8);
    auto* vec = static_cast<V11Vector*>(arena.alloc(sizeof(V11Vector), alignof(V11Vector)));
    if (!valid_bits || !vec) return nullptr;
    if (is_nullable && n > 0u) {
        size_t bm_bytes = (static_cast<size_t>(n) + 7u) >> 3;
        size_t padded_bm = (bm_bytes + 127u) & ~static_cast<size_t>(127u);
        if (payload_bytes < padded_bm) return nullptr;
        std::memset(valid_bits, 0, vwords * sizeof(uint64_t));
        std::memcpy(valid_bits, ptr, bm_bytes);
        ptr += padded_bm;
        payload_bytes -= padded_bm;
    } else {
        std::memset(valid_bits, 0xFF, vwords * sizeof(uint64_t));
    }
    new (vec) V11Vector();
    vec->length = n;
    vec->valid_bits = valid_bits;

    const uint32_t dim = (desc.dimension == 0u) ? 1u : desc.dimension;
    if (base_type == 6u && dim > 1u) {
        // Float32 embedding column -> V11Vector TAG_EMB (double** emb_rows)
        size_t total = static_cast<size_t>(n) * static_cast<size_t>(dim);
        if (payload_bytes < total * sizeof(float)) return nullptr;
        double* flat = (total > 0u) ? arena.alloc_array<double>(total, 8) : nullptr;
        double** rows = (n > 0u) ? arena.alloc_array<double*>(n, 8) : nullptr;
        if ((total > 0u && !flat) || (n > 0u && !rows)) return nullptr;
        for (uint32_t i = 0; i < n; ++i) {
            if (((valid_bits[i >> 6] >> (i & 63u)) & 1ULL) == 0ULL) {
                rows[i] = nullptr;
            } else {
                rows[i] = flat + static_cast<size_t>(i) * dim;
                for (uint32_t d = 0; d < dim; ++d) {
                    float f = 0.0f;
                    std::memcpy(&f, ptr + (static_cast<size_t>(i) * dim + d) * sizeof(float), sizeof(float));
                    rows[i][d] = static_cast<double>(f);
                }
            }
        }
        vec->elem_tag = kVecTagEmb;
        vec->emb_dim = dim;
        vec->emb_rows = rows;
        return vec;
    }
    if ((base_type >= 1u && base_type <= 4u) || base_type == 0x15u) {
        const size_t elem_sz = (base_type == 1u) ? 1u : (base_type == 2u ? 2u : (base_type == 3u ? 4u : 8u));
        if (static_cast<size_t>(n) > payload_bytes / elem_sz) return nullptr;
        if (base_type == 0x15u && (n == 0u || (reinterpret_cast<uintptr_t>(ptr) & 7u) == 0u)) {
            // Zero-copy direct view into mmap'd 128B-aligned DECIMAL64 int64_t payload
            vec->elem_tag = kVecTagI64;
            vec->i64_data = (n > 0u) ? const_cast<int64_t*>(reinterpret_cast<const int64_t*>(ptr)) : nullptr;
            return vec;
        }
        int64_t* out = (n > 0u) ? arena.alloc_array<int64_t>(n, 8) : nullptr;
        if (n > 0u && !out) return nullptr;
        for (size_t i = 0; i < static_cast<size_t>(n); ++i) {
            int64_t v = 0;
            if (base_type == 1u) { int8_t x; std::memcpy(&x, ptr + i, 1); v = x; }
            else if (base_type == 2u) { int16_t x; std::memcpy(&x, ptr + i * 2u, 2); v = x; }
            else if (base_type == 3u) { int32_t x; std::memcpy(&x, ptr + i * 4u, 4); v = x; }
            else { std::memcpy(&v, ptr + i * 8u, 8); }
            out[i] = v;
        }
        vec->elem_tag = kVecTagI64;
        vec->i64_data = out;
        return vec;
    }
    if (base_type == 6u || base_type == 7u) {
        const size_t elem_sz = (base_type == 6u) ? 4u : 8u;
        if (static_cast<size_t>(n) > payload_bytes / elem_sz) return nullptr;
        double* out = (n > 0u) ? arena.alloc_array<double>(n, 8) : nullptr;
        if (n > 0u && !out) return nullptr;
        for (size_t i = 0; i < static_cast<size_t>(n); ++i) {
            double v = 0.0;
            if (base_type == 6u) { float x; std::memcpy(&x, ptr + i * 4u, 4); v = static_cast<double>(x); }
            else { std::memcpy(&v, ptr + i * 8u, 8); }
            if (std::isnan(v)) {
                valid_bits[i >> 6] &= ~(1ULL << (i & 63u));
                out[i] = 0.0;
            } else {
                out[i] = v;
            }
        }
        vec->elem_tag = kVecTagF64;
        vec->f64_data = out;
        return vec;
    }
    if (base_type == 8u || base_type == 9u) {
        if (static_cast<size_t>(n) > payload_bytes / 8u) return nullptr;
        int64_t* out = (n > 0u) ? arena.alloc_array<int64_t>(n, 8) : nullptr;
        if (n > 0u && !out) return nullptr;
        for (size_t i = 0; i < static_cast<size_t>(n); ++i) {
            uint64_t v = 0;
            std::memcpy(&v, ptr + i * 8u, 8);
            out[i] = static_cast<int64_t>(v);
        }
        vec->elem_tag = kVecTagU64;
        vec->i64_data = out;
        return vec;
    }
    return nullptr;
}

bool build_snapshot_view_from_imps(const impulse_snapshot_t* snapshot,
                                   V11Arena& arena,
                                   V11SnapshotView& view) noexcept {
    const uint8_t* raw = static_cast<const uint8_t*>(snapshot->mmap_ptr);
    const size_t fsz = snapshot->file_size;

    size_t nd = snapshot->domains.size();
    for (size_t i = 0; i < snapshot->domains.size(); ++i) {
        if (snapshot->domains[i].domain_id + 1u > nd) nd = snapshot->domains[i].domain_id + 1u;
    }
    for (size_t r = 0; r < snapshot->relations.size(); ++r) {
        const auto& rd = snapshot->relations[r];
        if (rd.src_domain_id + 1u > nd) nd = rd.src_domain_id + 1u;
        if (rd.tgt_domain_id + 1u > nd) nd = rd.tgt_domain_id + 1u;
    }
    uint64_t* dom_cards = (nd > 0) ? arena.alloc_array<uint64_t>(nd, 8) : nullptr;
    uint8_t* dom_widths = (nd > 0) ? arena.alloc_array<uint8_t>(nd, 8) : nullptr;
    if (nd > 0 && (!dom_cards || !dom_widths)) return false;
    if (nd > 0) {
        std::memset(dom_cards, 0, nd * sizeof(uint64_t));
        std::memset(dom_widths, 0, nd * sizeof(uint8_t));
    }
    for (size_t i = 0; i < snapshot->domains.size(); ++i) {
        uint16_t did = snapshot->domains[i].domain_id;
        dom_cards[did] = snapshot->domains[i].node_count;
        dom_widths[did] = snapshot->domains[i].dense_id_width;
    }
    const bool legacy_v9_or_no_doms = snapshot->domains.empty() || snapshot->header.version < 10u;
    if (legacy_v9_or_no_doms) {
        for (size_t r = 0; r < snapshot->relations.size(); ++r) {
            const auto& rd = snapshot->relations[r];
            if (dom_cards[rd.src_domain_id] < rd.node_count) {
                dom_cards[rd.src_domain_id] = rd.node_count;
            }
            if (dom_cards[rd.tgt_domain_id] == 0u) {
                dom_cards[rd.tgt_domain_id] = rd.node_count;
            }
            if (dom_cards[0] == 0u && rd.node_count > 0u) {
                dom_cards[0] = rd.node_count;
            }
        }
    }
    view.domain_cards = dom_cards;
    view.domain_count = static_cast<uint32_t>(nd);
    view.raw_snapshot = snapshot;

    // Relations (CSR topology + edge attributes)
    const size_t nr = snapshot->relations.size();
    if (nr > 0 && raw != nullptr) {
        V11RelView* rels = arena.alloc_array<V11RelView>(nr, alignof(V11RelView));
        if (!rels) return false;
        for (size_t r = 0; r < nr; ++r) {
            new (&rels[r]) V11RelView();
            const auto& rd = snapshot->relations[r];
            rels[r].rel_id = rd.relation_id;
            rels[r].src_dom = rd.src_domain_id;
            rels[r].tgt_dom = rd.tgt_domain_id;
            rels[r].has_csr = (rd.csr_row_off_bytes > 0u);
            rels[r].has_csc = (rd.csc_row_off_bytes > 0u);
            rels[r].has_coo = (rd.coo_src_bytes > 0u && rd.coo_tgt_bytes > 0u) ||
                              (((rd.section_features & IMPULSE_SECTION_FEAT_HAS_COO) != 0ULL) && rd.edge_count == 0u);
            rels[r].node_id_width = (rd.node_id_width == 2u || rd.node_id_width == 8u) ? rd.node_id_width : 4u;
            rels[r].edge_idx_width = (rd.edge_index_width == 8u) ? 8u : 4u;
            uint8_t src_dw = (rd.src_domain_id < nd) ? dom_widths[rd.src_domain_id] : 0u;
            uint8_t tgt_dw = (rd.tgt_domain_id < nd) ? dom_widths[rd.tgt_domain_id] : 0u;
            uint8_t src_w = (src_dw == 2u || src_dw == 4u || src_dw == 8u) ? src_dw : rels[r].node_id_width;
            uint8_t tgt_w = (tgt_dw == 2u || tgt_dw == 4u || tgt_dw == 8u) ? tgt_dw : rels[r].node_id_width;
            rels[r].src_node_width = src_w;
            rels[r].tgt_node_width = tgt_w;
            rels[r].coo_src_width = src_w;
            rels[r].coo_tgt_width = tgt_w;
            rels[r].coo_order = rd.coo_order;

            if (rd.node_count >= UINT32_MAX || rd.edge_count > UINT32_MAX) {
                return false;
            }
            if (rels[r].has_csr) {
                const uint32_t n_rows = static_cast<uint32_t>(rd.node_count) + 1u;
                const uint32_t n_edges = static_cast<uint32_t>(rd.edge_count);
                const size_t need_ro = static_cast<size_t>(n_rows) * rels[r].edge_idx_width;
                const size_t need_ct = static_cast<size_t>(n_edges) * tgt_w;
                if (rd.csr_row_off_offset > fsz || rd.csr_row_off_bytes > fsz - rd.csr_row_off_offset ||
                    rd.csr_row_off_bytes < need_ro ||
                    rd.csr_col_idx_offset > fsz || rd.csr_col_idx_bytes > fsz - rd.csr_col_idx_offset ||
                    rd.csr_col_idx_bytes < need_ct) {
                    return false;
                }
                rels[r].csr_row_off_raw = raw + rd.csr_row_off_offset;
                rels[r].csr_col_idx_raw = (n_edges > 0u) ? (raw + rd.csr_col_idx_offset) : nullptr;
                rels[r].row_off_len = n_rows;
                rels[r].col_tgt_len = n_edges;
            }
            if (rels[r].has_csc) {
                const uint32_t csc_rows = static_cast<uint32_t>(rd.csc_row_off_bytes / rels[r].edge_idx_width);
                const uint32_t n_edges = static_cast<uint32_t>(rd.edge_count);
                const size_t need_ct = static_cast<size_t>(n_edges) * src_w;
                if (rd.csc_row_off_offset > fsz || rd.csc_row_off_bytes > fsz - rd.csc_row_off_offset ||
                    rd.csc_col_idx_offset > fsz || rd.csc_col_idx_bytes > fsz - rd.csc_col_idx_offset ||
                    rd.csc_col_idx_bytes < need_ct) {
                    return false;
                }
                rels[r].csc_row_off_raw = raw + rd.csc_row_off_offset;
                rels[r].csc_col_idx_raw = (n_edges > 0u) ? (raw + rd.csc_col_idx_offset) : nullptr;
                rels[r].csc_row_off_len = csc_rows;
            }
            if (rels[r].has_coo) {
                const uint32_t n_edges = static_cast<uint32_t>(rd.edge_count);
                const size_t need_src = static_cast<size_t>(n_edges) * src_w;
                const size_t need_tgt = static_cast<size_t>(n_edges) * tgt_w;
                if (rd.coo_src_offset > fsz || rd.coo_src_bytes > fsz - rd.coo_src_offset || rd.coo_src_bytes < need_src ||
                    rd.coo_tgt_offset > fsz || rd.coo_tgt_bytes > fsz - rd.coo_tgt_offset || rd.coo_tgt_bytes < need_tgt) {
                    return false;
                }
                if (rd.coo_edge_id_offset > 0u || rd.coo_edge_id_bytes > 0u) {
                    const size_t need_eid = static_cast<size_t>(n_edges) * rels[r].edge_idx_width;
                    if (rd.coo_edge_id_offset > fsz || rd.coo_edge_id_bytes > fsz - rd.coo_edge_id_offset ||
                        rd.coo_edge_id_bytes < need_eid) {
                        return false;
                    }
                    rels[r].coo_edge_id_raw = (n_edges > 0u) ? (raw + rd.coo_edge_id_offset) : nullptr;
                }
                rels[r].coo_src_raw = (n_edges > 0u) ? (raw + rd.coo_src_offset) : nullptr;
                rels[r].coo_tgt_raw = (n_edges > 0u) ? (raw + rd.coo_tgt_offset) : nullptr;
                rels[r].coo_edge_count = n_edges;
            }

            if (r < snapshot->relation_attributes.size() && !snapshot->relation_attributes[r].empty()) {
                const auto& attrs = snapshot->relation_attributes[r];
                uint32_t* ids = arena.alloc_array<uint32_t>(attrs.size(), 8);
                V11Vector** vecs = arena.alloc_array<V11Vector*>(attrs.size(), 8);
                if (!ids || !vecs) return false;
                uint32_t cnt = 0u;
                for (size_t a = 0; a < attrs.size(); ++a) {
                    V11Vector* v = decode_imps_attr_to_vec(raw, fsz, attrs[a], rd.edge_count, arena);
                    if (v) {
                        ids[cnt] = static_cast<uint32_t>(a);
                        vecs[cnt] = v;
                        ++cnt;
                    }
                }
                rels[r].edge_attr_ids = ids;
                rels[r].edge_attrs = vecs;
                rels[r].edge_attr_count = cnt;
            }
        }
        view.relations = rels;
        view.relation_count = static_cast<uint32_t>(nr);
    }

    // Domain node attributes
    size_t total_na = 0;
    for (size_t d = 0; d < snapshot->domain_attributes.size(); ++d) {
        total_na += snapshot->domain_attributes[d].size();
    }
    if (total_na > 0 && raw != nullptr) {
        V11DomAttrEntry* entries = arena.alloc_array<V11DomAttrEntry>(total_na, alignof(V11DomAttrEntry));
        if (!entries) return false;
        uint32_t cnt = 0u;
        for (size_t d = 0; d < snapshot->domain_attributes.size() && d < snapshot->domains.size(); ++d) {
            const auto& attrs = snapshot->domain_attributes[d];
            const uint16_t did = snapshot->domains[d].domain_id;
            const uint64_t n_nodes = snapshot->domains[d].node_count;
            for (size_t a = 0; a < attrs.size(); ++a) {
                V11Vector* v = decode_imps_attr_to_vec(raw, fsz, attrs[a], n_nodes, arena);
                if (v) {
                    entries[cnt++] = {did, static_cast<uint16_t>(a), v};
                }
            }
        }
        view.node_attrs = entries;
        view.node_attr_count = cnt;
    }

    // Secondary indexes
    if (!snapshot->indexes.empty()) {
        V11IndexEntry* idx = arena.alloc_array<V11IndexEntry>(snapshot->indexes.size(), alignof(V11IndexEntry));
        if (!idx) return false;
        uint32_t cnt = 0u;
        for (const auto& ie : snapshot->indexes) {
            if (ie.relation_id == 0xFFFFu) {
                idx[cnt++] = {ie.domain_id, ie.attribute_index, V11StrSlice{}};
            }
        }
        view.indexes = idx;
        view.index_count = cnt;
    }
    return true;
}

bool decode_impb_const_pool(const uint8_t* ptr,
                            size_t len,
                            impulse_vm_workspace_v11_t& ws) noexcept {
    if (len == 0u) return true;
    if (!ptr || len < 8u) return false;
    uint32_t entry_count = 0u;
    uint32_t reserved = 0u;
    std::memcpy(&entry_count, ptr, 4u);
    std::memcpy(&reserved, ptr + 4u, 4u);
    if (reserved != 0u || entry_count > kMaxConstPool) return false;

    size_t off = 8u;
    for (uint32_t i = 0; i < entry_count; ++i) {
        if (off + 16u > len) return false;
        uint32_t id = 0u;
        uint8_t kind = 0u;
        uint8_t elem_tag = 0u;
        uint16_t flags = 0u;
        uint32_t count = 0u;
        uint32_t extra = 0u;
        std::memcpy(&id, ptr + off, 4u);
        kind = ptr[off + 4u];
        elem_tag = ptr[off + 5u];
        std::memcpy(&flags, ptr + off + 6u, 2u);
        std::memcpy(&count, ptr + off + 8u, 4u);
        std::memcpy(&extra, ptr + off + 12u, 4u);
        off += 16u;

        V11ConstPoolEntry& ent = ws.const_pool[ws.const_pool_count];
        ent = V11ConstPoolEntry{};
        ent.id = id;

        if (kind == static_cast<uint8_t>(ConstPoolKind::Str)) {
            if (off + count > len) return false;
            char* sbuf = (count > 0u) ? ws.arena.alloc_array<char>(count, 1) : nullptr;
            if (count > 0u && !sbuf) return false;
            if (count > 0u) std::memcpy(sbuf, ptr + off, count);
            ent.kind = ConstPoolKind::Str;
            ent.str = {sbuf, count, 1u};
            off = (off + count + 7u) & ~size_t(7u);
        } else if (kind == static_cast<uint8_t>(ConstPoolKind::Frontier)) {
            size_t nbytes = static_cast<size_t>(count) * sizeof(uint64_t);
            if (off + nbytes > len) return false;
            uint64_t* ids = (count > 0u) ? ws.arena.alloc_array<uint64_t>(count, 8) : nullptr;
            if (count > 0u && !ids) return false;
            if (nbytes > 0u) std::memcpy(ids, ptr + off, nbytes);
            ent.kind = ConstPoolKind::Frontier;
            ent.f_ids = ids;
            ent.f_len = count;
            off += nbytes;
        } else if (kind == static_cast<uint8_t>(ConstPoolKind::Vec)) {
            auto* vec = static_cast<V11Vector*>(ws.arena.alloc(sizeof(V11Vector), alignof(V11Vector)));
            if (!vec) return false;
            new (vec) V11Vector();
            vec->elem_tag = elem_tag;
            vec->length = count;
            const size_t nwords = (count > 0u) ? ((static_cast<size_t>(count) + 63u) >> 6) : 0u;
            if ((flags & 1u) != 0u && nwords > 0u) {
                size_t vbytes = nwords * sizeof(uint64_t);
                if (off + vbytes > len) return false;
                uint64_t* vb = ws.arena.alloc_array<uint64_t>(nwords, 8);
                if (!vb) return false;
                std::memcpy(vb, ptr + off, vbytes);
                vec->valid_bits = vb;
                off += vbytes;
            }
            if (elem_tag == kVecTagI64 || elem_tag == kVecTagU64) {
                size_t dbytes = static_cast<size_t>(count) * sizeof(int64_t);
                if (off + dbytes > len) return false;
                int64_t* d = (count > 0u) ? ws.arena.alloc_array<int64_t>(count, 8) : nullptr;
                if (count > 0u && !d) return false;
                if (dbytes > 0u) std::memcpy(d, ptr + off, dbytes);
                vec->i64_data = d;
                off += dbytes;
            } else if (elem_tag == kVecTagF64) {
                size_t dbytes = static_cast<size_t>(count) * sizeof(double);
                if (off + dbytes > len) return false;
                double* d = (count > 0u) ? ws.arena.alloc_array<double>(count, 8) : nullptr;
                if (count > 0u && !d) return false;
                if (dbytes > 0u) std::memcpy(d, ptr + off, dbytes);
                vec->f64_data = d;
                off += dbytes;
            } else if (elem_tag == kVecTagBool3vl) {
                size_t dbytes = nwords * sizeof(uint64_t);
                if (off + dbytes > len) return false;
                uint64_t* d = (nwords > 0u) ? ws.arena.alloc_array<uint64_t>(nwords, 8) : nullptr;
                if (nwords > 0u && !d) return false;
                if (dbytes > 0u) std::memcpy(d, ptr + off, dbytes);
                vec->bool_data_bits = d;
                off += dbytes;
            } else if (elem_tag == kVecTagStr) {
                V11StrSlice* d = (count > 0u) ? ws.arena.alloc_array<V11StrSlice>(count, alignof(V11StrSlice)) : nullptr;
                if (count > 0u && !d) return false;
                for (uint32_t k = 0; k < count; ++k) {
                    if (off + 4u > len) return false;
                    uint32_t slen = 0u;
                    std::memcpy(&slen, ptr + off, 4u);
                    off += 4u;
                    if (slen == 0xFFFFFFFFu) {
                        d[k] = {nullptr, 0u, 0u};
                    } else {
                        if (off + slen > len) return false;
                        char* sbuf = (slen > 0u) ? ws.arena.alloc_array<char>(slen, 1) : nullptr;
                        if (slen > 0u && !sbuf) return false;
                        if (slen > 0u) std::memcpy(sbuf, ptr + off, slen);
                        d[k] = {sbuf, slen, 1u};
                        off += slen;
                    }
                }
                vec->str_data = d;
                off = (off + 7u) & ~size_t(7u);
            } else {
                return false;
            }
            ent.kind = ConstPoolKind::Vec;
            ent.vec = vec;
        } else {
            return false;
        }
        ws.const_pool_count += 1u;
    }
    return off <= len;
}

bool bind_c_abi_params(const impulse_vm_params_v11_t* params,
                       impulse_vm_workspace_v11_t& ws) noexcept {
    if (!params || !params->bindings || params->binding_count == 0u) return true;
    if (params->binding_count > kMaxParams) return false;
    for (uint32_t i = 0; i < params->binding_count; ++i) {
        const auto& b = params->bindings[i];
        V11ParamEntry& e = ws.params[ws.param_count];
        e = V11ParamEntry{};
        e.id = b.param_id;
        switch (b.kind) {
            case IMPULSE_VM_V11_PARAM_SCALAR_NULL:
                e.kind = ParamKind::Scalar;
                e.scalar_type = 0u;
                e.scalar_valid = false;
                e.scalar_bits = 0ULL;
                break;
            case IMPULSE_VM_V11_PARAM_SCALAR_I64:
                e.kind = ParamKind::Scalar;
                e.scalar_type = static_cast<uint8_t>(ScalarType::SCALAR_I64);
                e.scalar_valid = true;
                e.scalar_bits = b.scalar_bits;
                break;
            case IMPULSE_VM_V11_PARAM_SCALAR_U64:
                e.kind = ParamKind::Scalar;
                e.scalar_type = static_cast<uint8_t>(ScalarType::SCALAR_U64);
                e.scalar_valid = true;
                e.scalar_bits = b.scalar_bits;
                break;
            case IMPULSE_VM_V11_PARAM_SCALAR_F64:
                e.kind = ParamKind::Scalar;
                e.scalar_type = static_cast<uint8_t>(ScalarType::SCALAR_F64);
                e.scalar_valid = true;
                e.scalar_bits = b.scalar_bits;
                break;
            case IMPULSE_VM_V11_PARAM_SCALAR_BOOL:
                e.kind = ParamKind::Scalar;
                e.scalar_type = static_cast<uint8_t>(ScalarType::SCALAR_BOOL_3VL);
                e.scalar_valid = true;
                e.scalar_bits = (b.scalar_bits != 0ULL) ? 1ULL : 0ULL;
                break;
            case IMPULSE_VM_V11_PARAM_SCALAR_STR: {
                e.kind = ParamKind::Str;
                char* sbuf = (b.count > 0u) ? ws.arena.alloc_array<char>(b.count, 1) : nullptr;
                if (b.count > 0u && (!sbuf || !b.data_ptr)) return false;
                if (b.count > 0u) std::memcpy(sbuf, b.data_ptr, b.count);
                e.str = {sbuf, b.count, 1u};
                break;
            }
            case IMPULSE_VM_V11_PARAM_FRONTIER_IDS: {
                e.kind = ParamKind::Frontier;
                e.dom_id = b.domain_id;
                uint64_t* ids = (b.count > 0u) ? ws.arena.alloc_array<uint64_t>(b.count, 8) : nullptr;
                if (b.count > 0u && (!ids || !b.data_ptr)) return false;
                if (b.count > 0u) std::memcpy(ids, b.data_ptr, static_cast<size_t>(b.count) * sizeof(uint64_t));
                e.f_ids = ids;
                e.f_len = b.count;
                break;
            }
            case IMPULSE_VM_V11_PARAM_VEC_I64:
            case IMPULSE_VM_V11_PARAM_VEC_U64:
            case IMPULSE_VM_V11_PARAM_VEC_F64: {
                auto* vec = static_cast<V11Vector*>(ws.arena.alloc(sizeof(V11Vector), alignof(V11Vector)));
                if (!vec) return false;
                new (vec) V11Vector();
                vec->length = b.count;
                if (b.valid_bits && b.count > 0u) {
                    size_t nw = (static_cast<size_t>(b.count) + 63u) >> 6;
                    uint64_t* vb = ws.arena.alloc_array<uint64_t>(nw, 8);
                    if (!vb) return false;
                    std::memcpy(vb, b.valid_bits, nw * sizeof(uint64_t));
                    vec->valid_bits = vb;
                }
                if (b.kind == IMPULSE_VM_V11_PARAM_VEC_F64) {
                    vec->elem_tag = kVecTagF64;
                    double* d = (b.count > 0u) ? ws.arena.alloc_array<double>(b.count, 8) : nullptr;
                    if (b.count > 0u && (!d || !b.data_ptr)) return false;
                    if (b.count > 0u) std::memcpy(d, b.data_ptr, static_cast<size_t>(b.count) * sizeof(double));
                    vec->f64_data = d;
                } else {
                    vec->elem_tag = (b.kind == IMPULSE_VM_V11_PARAM_VEC_U64) ? kVecTagU64 : kVecTagI64;
                    int64_t* d = (b.count > 0u) ? ws.arena.alloc_array<int64_t>(b.count, 8) : nullptr;
                    if (b.count > 0u && (!d || !b.data_ptr)) return false;
                    if (b.count > 0u) std::memcpy(d, b.data_ptr, static_cast<size_t>(b.count) * sizeof(int64_t));
                    vec->i64_data = d;
                }
                e.kind = ParamKind::Vec;
                e.vec = vec;
                break;
            }
            default:
                return false;
        }
        ws.param_count += 1u;
    }
    return true;
}

impulse_vm_status_v11_t serialize_outputs_to_result_buf(
    const impulse_vm_workspace_v11_t& ws,
    void* result_buf,
    size_t result_cap,
    size_t* out_result_bytes
) noexcept {
    if (!result_buf) {
        if (out_result_bytes) *out_result_bytes = 0u;
        return IMPULSE_VM_V11_OK;
    }
    // Header: [output_count:u32, reserved:u32] + per-slot [slot:u16, elem_tag:u8, flags:u8, length:u32] + payload
    size_t needed = 8u;
    for (uint32_t i = 0; i < ws.output_count; ++i) {
        const V11Vector* v = ws.outputs[i].vec;
        needed += 8u;
        if (!v) continue;
        size_t nw = (v->length > 0u) ? ((static_cast<size_t>(v->length) + 63u) >> 6) : 0u;
        if (v->valid_bits && nw > 0u) needed += nw * 8u;
        if (v->elem_tag == kVecTagI64 || v->elem_tag == kVecTagU64 || v->elem_tag == kVecTagF64) {
            needed += static_cast<size_t>(v->length) * 8u;
        } else if (v->elem_tag == kVecTagBool3vl) {
            needed += nw * 8u;
        } else if (v->elem_tag == kVecTagStr && v->str_data) {
            for (uint32_t k = 0; k < v->length; ++k) {
                needed += 4u + (v->str_data[k].valid ? v->str_data[k].len : 0u);
            }
            needed = (needed + 7u) & ~size_t(7u);
        }
    }
    if (out_result_bytes) *out_result_bytes = needed;
    if (needed > result_cap) {
        return IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW;
    }
    auto* dst = static_cast<uint8_t*>(result_buf);
    uint32_t oc = ws.output_count;
    uint32_t zero = 0u;
    std::memcpy(dst, &oc, 4u);
    std::memcpy(dst + 4u, &zero, 4u);
    size_t pos = 8u;
    for (uint32_t i = 0; i < ws.output_count; ++i) {
        const V11Vector* v = ws.outputs[i].vec;
        uint16_t slot = ws.outputs[i].slot;
        uint8_t tag = v ? v->elem_tag : 0u;
        uint8_t fl = (v && v->valid_bits && v->length > 0u) ? 1u : 0u;
        uint32_t n = v ? v->length : 0u;
        std::memcpy(dst + pos, &slot, 2u);
        dst[pos + 2u] = tag;
        dst[pos + 3u] = fl;
        std::memcpy(dst + pos + 4u, &n, 4u);
        pos += 8u;
        if (!v) continue;
        size_t nw = (n > 0u) ? ((static_cast<size_t>(n) + 63u) >> 6) : 0u;
        if (fl != 0u && nw > 0u) {
            std::memcpy(dst + pos, v->valid_bits, nw * 8u);
            pos += nw * 8u;
        }
        if ((tag == kVecTagI64 || tag == kVecTagU64) && n > 0u && v->i64_data) {
            std::memcpy(dst + pos, v->i64_data, static_cast<size_t>(n) * 8u);
            pos += static_cast<size_t>(n) * 8u;
        } else if (tag == kVecTagF64 && n > 0u && v->f64_data) {
            std::memcpy(dst + pos, v->f64_data, static_cast<size_t>(n) * 8u);
            pos += static_cast<size_t>(n) * 8u;
        } else if (tag == kVecTagBool3vl && nw > 0u && v->bool_data_bits) {
            std::memcpy(dst + pos, v->bool_data_bits, nw * 8u);
            pos += nw * 8u;
        } else if (tag == kVecTagStr && v->str_data) {
            for (uint32_t k = 0; k < n; ++k) {
                uint32_t sl = v->str_data[k].valid ? v->str_data[k].len : 0xFFFFFFFFu;
                std::memcpy(dst + pos, &sl, 4u);
                pos += 4u;
                if (v->str_data[k].valid && v->str_data[k].len > 0u) {
                    std::memcpy(dst + pos, v->str_data[k].ptr, v->str_data[k].len);
                    pos += v->str_data[k].len;
                }
            }
            while ((pos & 7u) != 0u) dst[pos++] = 0u;
        }
    }
    return IMPULSE_VM_V11_OK;
}

struct V11CachedSnapshotView {
    uint8_t* buf = nullptr;
    size_t cap = 0;
    V11Arena arena{};
    V11SnapshotView view{};
    ~V11CachedSnapshotView() {
#ifdef _WIN32
        _aligned_free(buf);
#else
        std::free(buf);
#endif
    }
};

void destroy_cached_snapshot_view(void* p) {
    delete static_cast<V11CachedSnapshotView*>(p);
}

} // namespace

extern "C" IMPULSE_API impulse_vm_status_v11_t impulse_vm_v11_prepare_snapshot(
    const impulse_snapshot_t* snapshot
) {
    using namespace impulse::vm;
    if (!snapshot) return IMPULSE_VM_V11_ERR_NULL_SNAPSHOT;
    if (snapshot->v11_cached_view.load(std::memory_order_acquire) != nullptr) return IMPULSE_VM_V11_OK;

    std::lock_guard<std::mutex> lock(snapshot->v11_view_mutex);
    if (snapshot->v11_cached_view.load(std::memory_order_acquire) != nullptr) return IMPULSE_VM_V11_OK;

    // Compute exact O(domains + relations + attrs) metadata + decoded-attr backing size
    auto safe_add = [](size_t& acc, size_t add) noexcept -> bool {
        if (add > (std::numeric_limits<size_t>::max)() - acc) return false;
        acc += add;
        return true;
    };
    auto add_attr_bytes = [&](size_t& acc, const impulse_attribute_descriptor_t& desc, uint64_t row_count) noexcept -> bool {
        if (row_count > static_cast<uint64_t>(UINT32_MAX)) return false;
        const size_t n = static_cast<size_t>(row_count);
        const uint8_t base_type = desc.type_code & 0x7Fu;
        const size_t vwords = (n > 0u) ? ((n + 63u) >> 6) : 1u;
        if (!safe_add(acc, vwords * sizeof(uint64_t) + 64u)) return false;
        if (!safe_add(acc, sizeof(V11Vector) + 64u)) return false;
        const uint32_t dim = (desc.dimension == 0u) ? 1u : desc.dimension;
        if (base_type == 6u && dim > 1u) {
            if (n > (std::numeric_limits<size_t>::max)() / dim) return false;
            const size_t total = n * static_cast<size_t>(dim);
            if (total > (std::numeric_limits<size_t>::max)() / sizeof(double)) return false;
            if (!safe_add(acc, total * sizeof(double) + 64u)) return false;
            if (n > (std::numeric_limits<size_t>::max)() / sizeof(double*)) return false;
            if (!safe_add(acc, n * sizeof(double*) + 64u)) return false;
        } else if ((base_type >= 1u && base_type <= 4u) || (base_type >= 6u && base_type <= 9u) || base_type == 0x15u) {
            const bool zero_copy_dec64 = (base_type == 0x15u) && ((desc.data_offset & 7u) == 0u);
            if (!zero_copy_dec64) {
                if (n > (std::numeric_limits<size_t>::max)() / 8u) return false;
                if (!safe_add(acc, n * 8u + 64u)) return false;
            }
        }
        return true;
    };

    size_t nd = snapshot->domains.size();
    for (size_t i = 0; i < snapshot->domains.size(); ++i) {
        if (snapshot->domains[i].domain_id + 1u > nd) nd = snapshot->domains[i].domain_id + 1u;
    }
    for (size_t r = 0; r < snapshot->relations.size(); ++r) {
        const auto& rd = snapshot->relations[r];
        if (rd.src_domain_id + 1u > nd) nd = rd.src_domain_id + 1u;
        if (rd.tgt_domain_id + 1u > nd) nd = rd.tgt_domain_id + 1u;
    }
    size_t cap = 4096u;
    if (!safe_add(cap, nd * (sizeof(uint64_t) + sizeof(uint8_t)) + 256u)) return IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW;
    const size_t nr = snapshot->relations.size();
    if (!safe_add(cap, nr * sizeof(V11RelView) + 256u)) return IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW;
    for (size_t r = 0; r < nr; ++r) {
        if (r < snapshot->relation_attributes.size() && !snapshot->relation_attributes[r].empty()) {
            const auto& attrs = snapshot->relation_attributes[r];
            if (!safe_add(cap, attrs.size() * (sizeof(uint32_t) + sizeof(V11Vector*)) + 256u)) {
                return IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW;
            }
            for (size_t a = 0; a < attrs.size(); ++a) {
                if (!add_attr_bytes(cap, attrs[a], snapshot->relations[r].edge_count)) {
                    return IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW;
                }
            }
        }
    }
    size_t total_na = 0u;
    for (size_t d = 0; d < snapshot->domain_attributes.size() && d < snapshot->domains.size(); ++d) {
        const auto& attrs = snapshot->domain_attributes[d];
        total_na += attrs.size();
        for (size_t a = 0; a < attrs.size(); ++a) {
            if (!add_attr_bytes(cap, attrs[a], snapshot->domains[d].node_count)) {
                return IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW;
            }
        }
    }
    if (!safe_add(cap, total_na * sizeof(V11DomAttrEntry) + 256u)) return IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW;
    if (!safe_add(cap, snapshot->indexes.size() * sizeof(V11IndexEntry) + 256u)) return IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW;
    cap = (cap + 127u) & ~static_cast<size_t>(127u);
    void* raw = nullptr;
#ifdef _WIN32
    raw = _aligned_malloc(cap, 128);
    if (!raw) return IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW;
#else
    if (posix_memalign(&raw, 128, cap) != 0 || !raw) {
        return IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW;
    }
#endif
    auto* cached = new V11CachedSnapshotView();
    cached->buf = static_cast<uint8_t*>(raw);
    cached->cap = cap;
    cached->arena.base = cached->buf;
    cached->arena.cap = cap;
    cached->arena.pos = 0u;
    cached->arena.mark = 0u;
    if (!build_snapshot_view_from_imps(snapshot, cached->arena, cached->view)) {
        delete cached;
        return IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW;
    }
    snapshot->v11_cached_view_dtor = &destroy_cached_snapshot_view;
    snapshot->v11_cached_view.store(cached, std::memory_order_release);
    return IMPULSE_VM_V11_OK;
}

namespace impulse::vm {

const V11SnapshotView* v11_get_cached_snapshot_view(const impulse_snapshot_t* snapshot) noexcept {
    if (!snapshot) return nullptr;
    const void* cached_ptr = snapshot->v11_cached_view.load(std::memory_order_acquire);
    return cached_ptr ? &static_cast<const V11CachedSnapshotView*>(cached_ptr)->view : nullptr;
}

} // namespace impulse::vm

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
    using namespace impulse::vm;
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

    impulse_vm_validation_v11_t vr = validate_impb_in_arena(impb_bytes, impb_len, &scratch_ws->arena);
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
    scratch_ws->const_pool_count = 0u;
    scratch_ws->param_count = 0u;
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

    if ((hdr.header_flags & 0x01u) != 0u && hdr.const_pool_bytes > 0u) {
        const uint8_t* cp_ptr = static_cast<const uint8_t*>(impb_bytes) +
                                sizeof(hdr) +
                                static_cast<size_t>(hdr.code_word_count) * sizeof(uint64_t);
        if (!decode_impb_const_pool(cp_ptr, hdr.const_pool_bytes, *scratch_ws)) {
            scratch_ws->status = IMPULSE_VM_V11_ERR_VALIDATION_FAILED;
            return IMPULSE_VM_V11_ERR_VALIDATION_FAILED;
        }
    }

    if (!bind_c_abi_params(params, *scratch_ws)) {
        scratch_ws->status = IMPULSE_VM_V11_ERR_INVALID_OPERAND;
        return IMPULSE_VM_V11_ERR_INVALID_OPERAND;
    }

    V11SnapshotView view{};
    const V11SnapshotView* view_ptr = nullptr;
    if (snapshot != nullptr) {
        const void* cached_ptr = snapshot->v11_cached_view.load(std::memory_order_acquire);
        if (cached_ptr != nullptr) {
            view_ptr = &static_cast<const V11CachedSnapshotView*>(cached_ptr)->view;
        } else {
            if (!build_snapshot_view_from_imps(snapshot, scratch_ws->arena, view)) {
                scratch_ws->status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW;
                return IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW;
            }
            view_ptr = &view;
        }
    }

    impulse_vm_status_v11_t st = run_words_unlocked(words, hdr.code_word_count, view_ptr, *scratch_ws, gas, hdr.entry_pc, true);
    if (st == IMPULSE_VM_V11_OK && result_buf != nullptr) {
        impulse_vm_status_v11_t ser_st = serialize_outputs_to_result_buf(*scratch_ws, result_buf, result_cap, out_result_bytes);
        if (ser_st != IMPULSE_VM_V11_OK) {
            scratch_ws->status = ser_st;
            return ser_st;
        }
    }
    return st;
}

extern "C" IMPULSE_API const impulse_vm_state_v11_t* impulse_vm_v11_get_state(
    const impulse_vm_workspace_v11_t* ws
) {
    return ws ? &ws->state : nullptr;
}

extern "C" IMPULSE_API uint32_t impulse_vm_v11_get_output_count(
    const impulse_vm_workspace_v11_t* ws
) {
    return ws ? ws->output_count : 0u;
}

extern "C" IMPULSE_API int impulse_vm_v11_get_output(
    const impulse_vm_workspace_v11_t* ws,
    uint32_t index,
    impulse_vm_output_view_v11_t* out
) {
    if (!ws || !out || index >= ws->output_count) return 0;
    const auto& slot = ws->outputs[index];
    const auto* v = slot.vec;
    out->slot = slot.slot;
    out->elem_tag = v ? v->elem_tag : 0u;
    out->reserved = 0u;
    out->length = v ? v->length : 0u;
    out->i64_data = v ? v->i64_data : nullptr;
    out->f64_data = v ? v->f64_data : nullptr;
    out->valid_bits = v ? v->valid_bits : nullptr;
    out->bool_data_bits = v ? v->bool_data_bits : nullptr;
    return 1;
}

extern "C" IMPULSE_API int impulse_vm_v11_get_output_str(
    const impulse_vm_workspace_v11_t* ws,
    uint32_t index,
    uint32_t elem_idx,
    const char** out_ptr,
    uint32_t* out_len,
    int* out_valid
) {
    if (!ws || index >= ws->output_count) return 0;
    const auto* v = ws->outputs[index].vec;
    if (!v || v->elem_tag != impulse::vm::kVecTagStr || !v->str_data || elem_idx >= v->length) return 0;
    const auto& s = v->str_data[elem_idx];
    if (out_ptr) *out_ptr = s.ptr;
    if (out_len) *out_len = s.len;
    if (out_valid) *out_valid = s.valid ? 1 : 0;
    return 1;
}

extern "C" IMPULSE_API void impulse_vm_v11_reset_opcode_stats(
    impulse_vm_workspace_v11_t* ws
) {
#ifdef IMPULSE_V11_OPCODE_PROFILE
    if (ws) ws->reset_opcode_stats();
#else
    (void)ws;
#endif
}

extern "C" IMPULSE_API int impulse_vm_v11_get_opcode_stats(
    const impulse_vm_workspace_v11_t* ws,
    impulse_vm_v11_opcode_stat_t* out_128
) {
#ifdef IMPULSE_V11_OPCODE_PROFILE
    if (!ws || !out_128) return 0;
    std::memcpy(out_128, ws->op_stats, sizeof(ws->op_stats));
    return 1;
#else
    (void)ws;
    (void)out_128;
    return 0;
#endif
}
