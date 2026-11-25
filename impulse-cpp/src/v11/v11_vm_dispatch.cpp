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

using namespace impulse::vm::v11;

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
    if (base_type == 9u && dim > 1u) {
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
    if (base_type >= 1u && base_type <= 4u) {
        int64_t* out = (n > 0u) ? arena.alloc_array<int64_t>(n, 8) : nullptr;
        if (n > 0u && !out) return nullptr;
        for (uint32_t i = 0; i < n; ++i) {
            int64_t v = 0;
            if (base_type == 1u && i < payload_bytes) { int8_t x; std::memcpy(&x, ptr + i, 1); v = x; }
            else if (base_type == 2u && (i + 1u) * 2u <= payload_bytes) { int16_t x; std::memcpy(&x, ptr + i * 2u, 2); v = x; }
            else if (base_type == 3u && (i + 1u) * 4u <= payload_bytes) { int32_t x; std::memcpy(&x, ptr + i * 4u, 4); v = x; }
            else if (base_type == 4u && (i + 1u) * 8u <= payload_bytes) { std::memcpy(&v, ptr + i * 8u, 8); }
            out[i] = v;
        }
        vec->elem_tag = kVecTagI64;
        vec->i64_data = out;
        return vec;
    }
    if (base_type >= 5u && base_type <= 8u) {
        int64_t* out = (n > 0u) ? arena.alloc_array<int64_t>(n, 8) : nullptr;
        if (n > 0u && !out) return nullptr;
        for (uint32_t i = 0; i < n; ++i) {
            uint64_t v = 0;
            if (base_type == 5u && i < payload_bytes) { uint8_t x; std::memcpy(&x, ptr + i, 1); v = x; }
            else if (base_type == 6u && (i + 1u) * 2u <= payload_bytes) { uint16_t x; std::memcpy(&x, ptr + i * 2u, 2); v = x; }
            else if (base_type == 7u && (i + 1u) * 4u <= payload_bytes) { uint32_t x; std::memcpy(&x, ptr + i * 4u, 4); v = x; }
            else if (base_type == 8u && (i + 1u) * 8u <= payload_bytes) { std::memcpy(&v, ptr + i * 8u, 8); }
            out[i] = static_cast<int64_t>(v);
        }
        vec->elem_tag = kVecTagU64;
        vec->i64_data = out;
        return vec;
    }
    if (base_type == 9u || base_type == 10u) {
        double* out = (n > 0u) ? arena.alloc_array<double>(n, 8) : nullptr;
        if (n > 0u && !out) return nullptr;
        for (uint32_t i = 0; i < n; ++i) {
            double v = 0.0;
            if (base_type == 9u && (i + 1u) * 4u <= payload_bytes) { float x; std::memcpy(&x, ptr + i * 4u, 4); v = static_cast<double>(x); }
            else if (base_type == 10u && (i + 1u) * 8u <= payload_bytes) { std::memcpy(&v, ptr + i * 8u, 8); }
            out[i] = v;
        }
        vec->elem_tag = kVecTagF64;
        vec->f64_data = out;
        return vec;
    }
    return nullptr;
}

bool build_snapshot_view_from_imps(const impulse_snapshot_t* snapshot,
                                   V11Arena& arena,
                                   V11SnapshotView& view) noexcept {
    const uint8_t* raw = static_cast<const uint8_t*>(snapshot->mmap_ptr);
    const size_t fsz = snapshot->file_size;

    const size_t nd = snapshot->domains.size();
    uint64_t* dom_cards = (nd > 0) ? arena.alloc_array<uint64_t>(nd, 8) : nullptr;
    if (nd > 0 && !dom_cards) return false;
    for (size_t i = 0; i < nd; ++i) {
        dom_cards[i] = snapshot->domains[i].node_count;
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

            if (rels[r].has_csr &&
                rd.csr_row_off_offset <= fsz && rd.csr_row_off_bytes <= fsz - rd.csr_row_off_offset &&
                rd.csr_col_idx_offset <= fsz && rd.csr_col_idx_bytes <= fsz - rd.csr_col_idx_offset) {
                const uint32_t n_rows = static_cast<uint32_t>(rd.node_count) + 1u;
                const uint32_t n_edges = static_cast<uint32_t>(rd.edge_count);
                uint32_t* roff = arena.alloc_array<uint32_t>(n_rows, 8);
                uint64_t* ctgt = (n_edges > 0u) ? arena.alloc_array<uint64_t>(n_edges, 8) : nullptr;
                if (!roff || (n_edges > 0u && !ctgt)) return false;

                const uint8_t* ro_src = raw + rd.csr_row_off_offset;
                for (uint32_t i = 0; i < n_rows; ++i) {
                    if (rd.edge_index_width == 8u && (i + 1u) * 8u <= rd.csr_row_off_bytes) {
                        uint64_t v = 0; std::memcpy(&v, ro_src + static_cast<size_t>(i) * 8u, 8);
                        roff[i] = static_cast<uint32_t>(v);
                    } else if ((i + 1u) * 4u <= rd.csr_row_off_bytes) {
                        uint32_t v = 0; std::memcpy(&v, ro_src + static_cast<size_t>(i) * 4u, 4);
                        roff[i] = v;
                    } else {
                        roff[i] = 0u;
                    }
                }
                const uint8_t* ci_src = raw + rd.csr_col_idx_offset;
                for (uint32_t e = 0; e < n_edges; ++e) {
                    if (rd.node_id_width == 2u && (e + 1u) * 2u <= rd.csr_col_idx_bytes) {
                        uint16_t v = 0; std::memcpy(&v, ci_src + static_cast<size_t>(e) * 2u, 2);
                        ctgt[e] = v;
                    } else if (rd.node_id_width == 8u && (e + 1u) * 8u <= rd.csr_col_idx_bytes) {
                        uint64_t v = 0; std::memcpy(&v, ci_src + static_cast<size_t>(e) * 8u, 8);
                        ctgt[e] = v;
                    } else if ((e + 1u) * 4u <= rd.csr_col_idx_bytes) {
                        uint32_t v = 0; std::memcpy(&v, ci_src + static_cast<size_t>(e) * 4u, 4);
                        ctgt[e] = v;
                    } else {
                        ctgt[e] = 0ULL;
                    }
                }
                rels[r].row_offsets = roff;
                rels[r].row_off_len = n_rows;
                rels[r].col_targets = ctgt;
                rels[r].col_tgt_len = n_edges;
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
        for (size_t d = 0; d < snapshot->domain_attributes.size() && d < nd; ++d) {
            const auto& attrs = snapshot->domain_attributes[d];
            for (size_t a = 0; a < attrs.size(); ++a) {
                V11Vector* v = decode_imps_attr_to_vec(raw, fsz, attrs[a], snapshot->domains[d].node_count, arena);
                if (v) {
                    entries[cnt++] = {static_cast<uint16_t>(d), static_cast<uint16_t>(a), v};
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

} // namespace

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
        if (!build_snapshot_view_from_imps(snapshot, scratch_ws->arena, view)) {
            scratch_ws->status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW;
            return IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW;
        }
        view_ptr = &view;
    }

    return run_words_unlocked(words, hdr.code_word_count, view_ptr, *scratch_ws, gas, hdr.entry_pc, true);
}

extern "C" IMPULSE_API const impulse_vm_state_v11_t* impulse_vm_v11_get_state(
    const impulse_vm_workspace_v11_t* ws
) {
    return ws ? &ws->state : nullptr;
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
