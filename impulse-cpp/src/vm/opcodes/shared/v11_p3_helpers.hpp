/**
 * @file v11_p3_helpers.hpp
 * @brief Shared Pillar 3 (0x30..0x3B) zero-malloc helpers and lexical morsel engine.
 *
 * All temporary buffers are carved from ws.arena (zero heap allocation).
 * Coarse kernels are marked IMPULSE_V11_NOINLINE to keep the main dispatch loop L1i-resident.
 */
#pragma once
#include "vm_simd_kernels.hpp"

#include "v11_p2_helpers.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace impulse::vm {

inline const V11RelView* p3_get_rel(const V11SnapshotView* snap,
                                    impulse_vm_workspace_v11_t& ws,
                                    uint16_t rid) noexcept {
    if (!snap) { ws.status = IMPULSE_VM_V11_ERR_NULL_SNAPSHOT; return nullptr; }
    if (rid >= snap->relation_count) { ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; return nullptr; }
    return &snap->relations[rid];
}

inline const V11Frontier* p3_get_f1bit(impulse_vm_workspace_v11_t& ws, uint8_t reg) noexcept {
    if (!ws.is_f_init(reg) || ws.f_regs[reg].multi_lane) {
        ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH;
        return nullptr;
    }
    return &ws.f_regs[reg];
}

inline const V11Vector* p3_find_node_attr(const V11SnapshotView* snap, uint16_t dom, uint16_t attr) noexcept {
    if (!snap) return nullptr;
    for (uint32_t i = 0; i < snap->node_attr_count; ++i) {
        if (snap->node_attrs[i].dom_id == dom && snap->node_attrs[i].attr_id == attr) {
            return snap->node_attrs[i].vec;
        }
    }
    return nullptr;
}

inline const V11Vector* p3_find_edge_attr(const V11RelView& rel, uint32_t attr_id) noexcept {
    for (uint32_t i = 0; i < rel.edge_attr_count; ++i) {
        if (rel.edge_attr_ids[i] == attr_id) return rel.edge_attrs[i];
    }
    return nullptr;
}

inline bool p3_has_key_catalog(const V11SnapshotView* snap, uint16_t dom) noexcept {
    if (!snap) return false;
    for (uint32_t i = 0; i < snap->key_catalog_count; ++i) {
        if (snap->key_catalogs[i].dom_id == dom) return true;
    }
    return false;
}

inline bool p3_has_index(const V11SnapshotView* snap, uint16_t dom, uint16_t attr) noexcept {
    if (!snap) return false;
    for (uint32_t i = 0; i < snap->index_count; ++i) {
        if (snap->indexes[i].dom_id == dom && snap->indexes[i].attr_id == attr) return true;
    }
    return false;
}

/** Zero-malloc flat CSC reverse adjacency table built in caller arena. */
struct V11RevAdj {
    uint32_t  n_tgt   = 0;
    uint32_t* offsets = nullptr; // [n_tgt + 1]
    uint32_t* srcs    = nullptr; // [n_edges]
    uint32_t* edges   = nullptr; // [n_edges] (canonical CSR edge idx)
};

IMPULSE_V11_NOINLINE inline bool p3_build_rev_adj(V11Arena& arena,
                                                  const V11SnapshotView& snap,
                                                  const V11RelView& rel,
                                                  V11RevAdj* out) noexcept {
    uint64_t n_tgt64 = (rel.tgt_dom < snap.domain_count) ? snap.domain_cards[rel.tgt_dom] : 0ULL;
    if (n_tgt64 > UINT32_MAX) return false;
    uint32_t n_tgt = static_cast<uint32_t>(n_tgt64);
    uint32_t* offs = arena.alloc_array<uint32_t>(static_cast<size_t>(n_tgt) + 1u, 8);
    if (!offs) return false;
    std::memset(offs, 0, (static_cast<size_t>(n_tgt) + 1u) * sizeof(uint32_t));

    uint32_t n_src = (rel.row_off_len > 0u) ? (rel.row_off_len - 1u) : 0u;
    uint32_t total = 0u;
    for (uint32_t u = 0; u < n_src; ++u) {
        for (uint32_t e = rel.csr_row_off(u); e < rel.csr_row_off(u + 1u); ++e) {
            uint64_t v = rel.csr_col_tgt(e);
            if (v < n_tgt64) {
                offs[static_cast<uint32_t>(v) + 1u]++;
                total++;
            }
        }
    }
    for (uint32_t v = 0; v < n_tgt; ++v) offs[v + 1u] += offs[v];

    uint32_t* srcs  = (total > 0u) ? arena.alloc_array<uint32_t>(total, 8) : nullptr;
    uint32_t* edges = (total > 0u) ? arena.alloc_array<uint32_t>(total, 8) : nullptr;
    uint32_t* cur   = (n_tgt > 0u) ? arena.alloc_array<uint32_t>(n_tgt, 8) : nullptr;
    if ((total > 0u && (!srcs || !edges)) || (n_tgt > 0u && !cur)) return false;
    if (n_tgt > 0u) std::memcpy(cur, offs, static_cast<size_t>(n_tgt) * sizeof(uint32_t));

    for (uint32_t u = 0; u < n_src; ++u) {
        for (uint32_t e = rel.csr_row_off(u); e < rel.csr_row_off(u + 1u); ++e) {
            uint64_t v = rel.csr_col_tgt(e);
            if (v < n_tgt64) {
                uint32_t pos = cur[static_cast<uint32_t>(v)]++;
                srcs[pos]  = u;
                edges[pos] = e;
            }
        }
    }
    out->n_tgt   = n_tgt;
    out->offsets = offs;
    out->srcs    = srcs;
    out->edges   = edges;
    return true;
}

inline bool p3_compute_in_degs(V11Arena& arena,
                               const V11SnapshotView& snap,
                               const V11RelView& rel,
                               uint32_t** out_deg,
                               uint32_t* out_n_tgt) noexcept {
    uint64_t n_tgt64 = (rel.tgt_dom < snap.domain_count) ? snap.domain_cards[rel.tgt_dom] : 0ULL;
    if (n_tgt64 > UINT32_MAX) return false;
    uint32_t n_tgt = static_cast<uint32_t>(n_tgt64);
    *out_n_tgt = n_tgt;
    if (n_tgt == 0u) { *out_deg = nullptr; return true; }
    uint32_t* deg = arena.alloc_array<uint32_t>(n_tgt, 8);
    if (!deg) return false;
    std::memset(deg, 0, static_cast<size_t>(n_tgt) * sizeof(uint32_t));
    for (uint32_t e = 0; e < rel.col_tgt_len; ++e) {
        uint64_t v = rel.csr_col_tgt(e);
        if (v < n_tgt64) deg[static_cast<uint32_t>(v)]++;
    }
    *out_deg = deg;
    return true;
}

inline bool p3_sorted_contains(const uint32_t* arr, uint32_t len, uint32_t x) noexcept {
    return len > 0u && arr != nullptr && std::binary_search(arr, arr + len, x);
}

inline bool p3_fwd_step(V11Arena& arena,
                        const V11RelView& rel,
                        const uint32_t* src, uint32_t src_len,
                        const uint32_t* excl1, uint32_t excl1_len,
                        const uint32_t* excl2, uint32_t excl2_len,
                        uint32_t** out_ptr, uint32_t* out_len) noexcept {
    size_t cap = 0u;
    for (uint32_t i = 0; i < src_len; ++i) {
        uint32_t u = src[i];
        if (u + 1u < rel.row_off_len) cap += (rel.csr_row_off(u + 1u) - rel.csr_row_off(u));
    }
    if (cap == 0u) { *out_ptr = nullptr; *out_len = 0u; return true; }
    uint32_t* buf = arena.alloc_array<uint32_t>(cap, 8);
    if (!buf) return false;
    uint32_t cnt = 0u;
    for (uint32_t i = 0; i < src_len; ++i) {
        uint32_t u = src[i];
        if (u + 1u < rel.row_off_len) {
            for (uint32_t e = rel.csr_row_off(u); e < rel.csr_row_off(u + 1u); ++e) {
                uint32_t v = static_cast<uint32_t>(rel.csr_col_tgt(e));
                if (!p3_sorted_contains(excl1, excl1_len, v) && !p3_sorted_contains(excl2, excl2_len, v)) {
                    buf[cnt++] = v;
                }
            }
        }
    }
    *out_len = p2_dedupe_sorted_inplace(buf, cnt);
    *out_ptr = buf;
    return true;
}

inline bool p3_rev_step(V11Arena& arena,
                        const V11RevAdj& rev,
                        const uint32_t* tgt, uint32_t tgt_len,
                        const uint32_t* excl1, uint32_t excl1_len,
                        const uint32_t* excl2, uint32_t excl2_len,
                        uint32_t** out_ptr, uint32_t* out_len) noexcept {
    size_t cap = 0u;
    for (uint32_t i = 0; i < tgt_len; ++i) {
        uint32_t v = tgt[i];
        if (v < rev.n_tgt) cap += (rev.offsets[v + 1u] - rev.offsets[v]);
    }
    if (cap == 0u) { *out_ptr = nullptr; *out_len = 0u; return true; }
    uint32_t* buf = arena.alloc_array<uint32_t>(cap, 8);
    if (!buf) return false;
    uint32_t cnt = 0u;
    for (uint32_t i = 0; i < tgt_len; ++i) {
        uint32_t v = tgt[i];
        if (v < rev.n_tgt) {
            for (uint32_t e = rev.offsets[v]; e < rev.offsets[v + 1u]; ++e) {
                uint32_t u = rev.srcs[e];
                if (!p3_sorted_contains(excl1, excl1_len, u) && !p3_sorted_contains(excl2, excl2_len, u)) {
                    buf[cnt++] = u;
                }
            }
        }
    }
    *out_len = p2_dedupe_sorted_inplace(buf, cnt);
    *out_ptr = buf;
    return true;
}

inline bool p3_union_sorted(V11Arena& arena,
                            const uint32_t* a, uint32_t na,
                            const uint32_t* b, uint32_t nb,
                            uint32_t** out_ptr, uint32_t* out_len) noexcept {
    size_t cap = static_cast<size_t>(na) + static_cast<size_t>(nb);
    if (cap == 0u) { *out_ptr = nullptr; *out_len = 0u; return true; }
    uint32_t* out = arena.alloc_array<uint32_t>(cap, 8);
    if (!out) return false;
    uint32_t i = 0u, j = 0u, k = 0u;
    while (i < na && j < nb) {
        if (a[i] < b[j]) out[k++] = a[i++];
        else if (b[j] < a[i]) out[k++] = b[j++];
        else { out[k++] = a[i++]; ++j; }
    }
    while (i < na) out[k++] = a[i++];
    while (j < nb) out[k++] = b[j++];
    *out_ptr = out;
    *out_len = k;
    return true;
}

inline bool p3_eval_edge_pred(const V11Vector& col, uint32_t e, uint8_t cmp_op,
                              bool rhs_valid, uint64_t rhs_bits, uint8_t rhs_ty) noexcept {
    if (e >= col.length) return (cmp_op == 7u);
    if (col.elem_tag == kVecTagLaneState) {
        if (cmp_op == 6u) return false;
        if (cmp_op == 7u) return true;
        return false;
    }
    if (cmp_op == 6u) return col.is_valid_at(e);
    if (cmp_op == 7u) return !col.is_valid_at(e);
    if (!rhs_valid || !col.is_valid_at(e)) return false;
    if (col.elem_tag == kVecTagF64) {
        double lv = col.f64_data[e];
        double rv = (rhs_ty == static_cast<uint8_t>(ScalarType::SCALAR_I64) ||
                     rhs_ty == static_cast<uint8_t>(ScalarType::SCALAR_U64))
            ? static_cast<double>(static_cast<int64_t>(rhs_bits))
            : p1_bits_to_f64(rhs_bits);
        if (std::isnan(lv) || std::isnan(rv)) return false;
        switch (cmp_op) {
            case 0: return lv == rv;
            case 1: return lv != rv;
            case 2: return lv <  rv;
            case 3: return lv <= rv;
            case 4: return lv >  rv;
            case 5: return lv >= rv;
            default: return false;
        }
    } else if (col.elem_tag == kVecTagI64) {
        int64_t lv = col.i64_data[e];
        int64_t rv = static_cast<int64_t>(rhs_bits);
        switch (cmp_op) {
            case 0: return lv == rv;
            case 1: return lv != rv;
            case 2: return lv <  rv;
            case 3: return lv <= rv;
            case 4: return lv >  rv;
            case 5: return lv >= rv;
            default: return false;
        }
    } else if (col.elem_tag == kVecTagU64) {
        uint64_t lv = static_cast<uint64_t>(col.i64_data[e]);
        uint64_t rv = rhs_bits;
        switch (cmp_op) {
            case 0: return lv == rv;
            case 1: return lv != rv;
            case 2: return lv <  rv;
            case 3: return lv <= rv;
            case 4: return lv >  rv;
            case 5: return lv >= rv;
            default: return false;
        }
    }
    return false;
}

inline uint64_t p3_splitmix_sample(uint64_t seed, uint64_t u, uint64_t j) noexcept {
    uint64_t z = seed ^ (u * 0x9E3779B97F4A7C15ULL) ^ (j * 0xBF58476D1CE4E5B9ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

struct V11MorselItem {
    uint64_t m0;
    uint64_t m1;
    uint64_t m2;
    uint64_t m3;
};

struct V11MorselAccF {
    uint32_t* data = nullptr;
    uint32_t  len  = 0;
    uint32_t  cap  = 0;

    bool push(V11Arena& arena, uint32_t v) noexcept {
        if (len == cap) {
            uint32_t next_cap = (cap == 0u) ? 32u : (cap * 2u);
            uint32_t* nd = arena.alloc_array<uint32_t>(next_cap, 8);
            if (!nd) return false;
            if (len > 0u) std::memcpy(nd, data, static_cast<size_t>(len) * sizeof(uint32_t));
            data = nd;
            cap = next_cap;
        }
        data[len++] = v;
        return true;
    }
};

struct V11MorselAccMl {
    uint64_t* nodes = nullptr;
    uint64_t* masks = nullptr;
    uint32_t  len   = 0;
    uint32_t  cap   = 0;

    bool or_entry(V11Arena& arena, uint64_t u, uint64_t m) noexcept {
        for (uint32_t i = 0; i < len; ++i) {
            if (nodes[i] == u) { masks[i] |= m; return true; }
        }
        if (len == cap) {
            uint32_t next_cap = (cap == 0u) ? 32u : (cap * 2u);
            uint64_t* nn = arena.alloc_array<uint64_t>(next_cap, 8);
            uint64_t* nm = arena.alloc_array<uint64_t>(next_cap, 8);
            if (!nn || !nm) return false;
            if (len > 0u) {
                std::memcpy(nn, nodes, static_cast<size_t>(len) * sizeof(uint64_t));
                std::memcpy(nm, masks, static_cast<size_t>(len) * sizeof(uint64_t));
            }
            nodes = nn;
            masks = nm;
            cap = next_cap;
        }
        nodes[len] = u;
        masks[len] = m;
        ++len;
        return true;
    }
};

struct V11MorselFwdCsr {
    const V11RelView* rel = nullptr;
    const uint32_t*   src_ids = nullptr;
    uint32_t          src_len = 0u;
    const uint32_t*   s_excl = nullptr;
    uint32_t          s_excl_len = 0u;
};

IMPULSE_V11_NOINLINE inline void p3_run_morsel_block(
    impulse_vm_workspace_v11_t& ws,
    const V11SnapshotView& view,
    const uint64_t* program,
    size_t word_count,
    uint32_t hdr_pc,
    uint32_t end_pc,
    uint16_t default_dom,
    bool is_filter,
    const V11MorselItem* items,
    uint32_t total,
    uint64_t& steps,
    uint64_t max_steps,
    const V11MorselFwdCsr* fwd_csr = nullptr
) noexcept {
    if (end_pc <= hdr_pc || static_cast<size_t>(end_pc) >= word_count) {
        ws.status = IMPULSE_VM_V11_ERR_VALIDATION_FAILED;
        return;
    }
    V11MorselAccF  acc_f[8]{};
    uint64_t*      acc_f_bits[8]{};
    uint32_t       acc_f_bit_cap[8]{};
    V11MorselAccMl acc_ml[8]{};
    double*   acc_vf64[8]{};
    uint8_t*  acc_vf64_valid[8]{};
    uint32_t  acc_vf64_len[8]{};
    bool      has_vf64[8]{};
    int64_t*  acc_vu64[8]{};
    uint8_t*  acc_vu64_valid[8]{};
    uint32_t  acc_vu64_len[8]{};
    bool      has_vu64[8]{};
    uint64_t  acc_rcnt[8]{};

    int32_t committed_f_dom[8];
    int32_t committed_ml_dom[8];
    bool    committed_vf64[8]{};
    bool    committed_vu64[8]{};
    bool    committed_rcnt[8]{};
    bool    need_f_accum[8]{};
    bool    need_ml_accum[8]{};
    bool    need_vf64_accum[8]{};
    bool    need_vf64_zero[8]{};
    bool    need_vu64_accum[8]{};
    bool    need_rcnt_accum[8]{};
    for (int d = 0; d < 8; ++d) {
        committed_f_dom[d]  = -1;
        committed_ml_dom[d] = -1;
        need_f_accum[d]     = true;
        need_ml_accum[d]    = true;
        need_vf64_accum[d]  = true;
        need_vu64_accum[d]  = true;
        need_rcnt_accum[d]  = true;
    }

    // Pre-scan OP_MORSEL_COMMIT instructions first (validates c_dst < 8 and determines which sinks need snapshotting)
    for (uint32_t p = hdr_pc + 1u; p < end_pc; ++p) {
        uint64_t cw = program[p];
        if (IMP_V11_OPCODE(cw) == static_cast<uint8_t>(Opcode::OP_MORSEL_COMMIT)) {
            uint8_t c_sub   = IMP_V11_SUBOP(cw);
            uint8_t c_flags = IMP_V11_FLAGS(cw);
            uint8_t c_dst   = IMP_V11_DST(cw);
            if (c_dst >= 8u) {
                ws.status = IMPULSE_VM_V11_ERR_VALIDATION_FAILED;
                return;
            }
            uint16_t c_lo   = IMP_V11_IMM16_LO(cw);
            bool c_accum    = (c_flags & FLAG_ACCUM) != 0u;
            uint16_t dom    = (c_lo == 0xFFFFu) ? default_dom : c_lo;
            switch (c_sub) {
                case 0:
                    committed_f_dom[c_dst] = static_cast<int32_t>(dom);
                    if (!c_accum) need_f_accum[c_dst] = false;
                    break;
                case 1:
                    committed_ml_dom[c_dst] = static_cast<int32_t>(dom);
                    if (!c_accum) need_ml_accum[c_dst] = false;
                    break;
                case 2:
                    committed_vf64[c_dst] = true;
                    if (!c_accum) { need_vf64_accum[c_dst] = false; need_vf64_zero[c_dst] = true; }
                    break;
                case 3:
                case 4:
                    committed_vf64[c_dst] = true;
                    if (!c_accum) { need_vf64_accum[c_dst] = false; need_vf64_zero[c_dst] = false; }
                    break;
                case 5:
                case 6:
                case 7:
                    committed_vu64[c_dst] = true;
                    if (!c_accum) need_vu64_accum[c_dst] = false;
                    break;
                case 8:
                    committed_rcnt[c_dst] = true;
                    if (!c_accum) need_rcnt_accum[c_dst] = false;
                    break;
                default: break;
            }
        }
    }

    // Snapshot only the pre-existing outer registers 0..7 that are targeted by commit sinks
    bool can_rewind_chunks = true;
    for (uint8_t d = 0; d < 8; ++d) {
        if (committed_f_dom[d] >= 0) {
            uint16_t dom = static_cast<uint16_t>(committed_f_dom[d]);
            uint64_t dcard = (dom < view.domain_count) ? view.domain_cards[dom] : 0ULL;
            if (dcard > 0ULL && dcard <= (1ULL << 22)) {
                size_t bw = (static_cast<size_t>(dcard) + 63u) >> 6;
                uint64_t* bits = ws.arena.alloc_array<uint64_t>(bw, 64);
                if (bits) {
                    std::memset(bits, 0, bw * sizeof(uint64_t));
                    acc_f_bits[d] = bits;
                    acc_f_bit_cap[d] = static_cast<uint32_t>(dcard);
                } else {
                    can_rewind_chunks = false;
                }
            } else {
                can_rewind_chunks = false;
            }
            if (need_f_accum[d] && ws.is_f_init(d) && !ws.f_regs[d].multi_lane) {
                const uint32_t* ids = nullptr;
                uint32_t n = 0u;
                if (!ws.f_regs[d].materialize_1bit(ws.arena, &ids, &n)) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; return; }
                for (uint32_t i = 0; i < n; ++i) {
                    uint32_t id = ids[i];
                    if (acc_f_bits[d] && id < acc_f_bit_cap[d]) {
                        acc_f_bits[d][id >> 6] |= (1ULL << (id & 63u));
                    } else {
                        can_rewind_chunks = false;
                        if (!acc_f[d].push(ws.arena, id)) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; return; }
                    }
                }
            }
        }
        if (committed_ml_dom[d] >= 0) {
            can_rewind_chunks = false;
            if (need_ml_accum[d] && ws.is_f_init(d) && ws.f_regs[d].multi_lane) {
                for (uint32_t k = 0; k < ws.f_regs[d].ml_count; ++k) {
                    if (!acc_ml[d].or_entry(ws.arena, ws.f_regs[d].ml_nodes[k], ws.f_regs[d].ml_masks[k])) {
                        ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; return;
                    }
                }
            }
        }
        const V11Vector* vd = ws.v_regs[d];
        if (committed_vf64[d] && vd && vd->elem_tag == kVecTagF64) {
            has_vf64[d] = true;
            acc_vf64_len[d] = vd->length;
            if (vd->length > 0u) {
                acc_vf64[d] = ws.arena.alloc_array<double>(vd->length, 8);
                if (!acc_vf64[d]) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; return; }
                if (need_vf64_accum[d]) {
                    acc_vf64_valid[d] = ws.arena.alloc_array<uint8_t>(vd->length, 8);
                    if (!acc_vf64_valid[d]) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; return; }
                    std::memcpy(acc_vf64[d], vd->f64_data, static_cast<size_t>(vd->length) * sizeof(double));
                    for (uint32_t i = 0; i < vd->length; ++i) acc_vf64_valid[d][i] = vd->is_valid_at(i) ? 1u : 0u;
                } else if (need_vf64_zero[d]) {
                    std::memset(acc_vf64[d], 0, static_cast<size_t>(vd->length) * sizeof(double));
                    acc_vf64_valid[d] = nullptr; // Lazy: all valid until a NaN occurs
                } else {
                    acc_vf64_valid[d] = ws.arena.alloc_array<uint8_t>(vd->length, 8);
                    if (!acc_vf64_valid[d]) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; return; }
                    std::memset(acc_vf64[d], 0, static_cast<size_t>(vd->length) * sizeof(double));
                    std::memset(acc_vf64_valid[d], 0, vd->length);
                }
            }
        }
        if (committed_vu64[d] && vd && (vd->elem_tag == kVecTagU64 || vd->elem_tag == kVecTagI64)) {
            has_vu64[d] = true;
            acc_vu64_len[d] = vd->length;
            if (vd->length > 0u) {
                acc_vu64[d] = ws.arena.alloc_array<int64_t>(vd->length, 8);
                acc_vu64_valid[d] = ws.arena.alloc_array<uint8_t>(vd->length, 8);
                if (!acc_vu64[d] || !acc_vu64_valid[d]) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; return; }
                if (need_vu64_accum[d]) {
                    std::memcpy(acc_vu64[d], vd->i64_data, static_cast<size_t>(vd->length) * sizeof(int64_t));
                    for (uint32_t i = 0; i < vd->length; ++i) acc_vu64_valid[d][i] = vd->is_valid_at(i) ? 1u : 0u;
                } else {
                    std::memset(acc_vu64[d], 0, static_cast<size_t>(vd->length) * sizeof(int64_t));
                    std::memset(acc_vu64_valid[d], 0, vd->length);
                }
            }
        }
        acc_rcnt[d] = (committed_rcnt[d] && need_rcnt_accum[d] && ws.is_r_valid(d)) ? ws.read_r_raw(d) : 0ULL;
    }

    uint32_t m_max = (ws.geometry.morsel_max > 0u) ? ws.geometry.morsel_max : 1u;
    uint32_t* mf0_ids = nullptr;
    int64_t* m0 = nullptr;
    int64_t* m1 = nullptr;
    int64_t* m2 = nullptr;
    int64_t* m3 = nullptr;
    V11Vector* v8 = nullptr;
    V11Vector* v9 = nullptr;
    V11Vector* v10 = nullptr;
    V11Vector* v11 = nullptr;
    bool hoisted_bufs = false;

    auto ensure_morsel_bufs = [&](uint32_t cap) noexcept -> bool {
        if (hoisted_bufs) return true;
        mf0_ids = ws.arena.alloc_array<uint32_t>(cap, 64);
        m0 = ws.arena.alloc_array<int64_t>(cap, 64);
        m1 = is_filter ? nullptr : ws.arena.alloc_array<int64_t>(cap, 64);
        m2 = is_filter ? nullptr : ws.arena.alloc_array<int64_t>(cap, 64);
        m3 = ws.arena.alloc_array<int64_t>(cap, 64);
        v8  = static_cast<V11Vector*>(ws.arena.alloc(sizeof(V11Vector), alignof(V11Vector)));
        v9  = is_filter ? nullptr : static_cast<V11Vector*>(ws.arena.alloc(sizeof(V11Vector), alignof(V11Vector)));
        v10 = is_filter ? nullptr : static_cast<V11Vector*>(ws.arena.alloc(sizeof(V11Vector), alignof(V11Vector)));
        v11 = static_cast<V11Vector*>(ws.arena.alloc(sizeof(V11Vector), alignof(V11Vector)));
        if (!mf0_ids || !m0 || !m3 || !v8 || !v11 || (!is_filter && (!m1 || !m2 || !v9 || !v10))) {
            return false;
        }
        for (uint32_t i = 0; i < cap; ++i) mf0_ids[i] = i;
        hoisted_bufs = true;
        return true;
    };

    uint32_t cursor = 0u;
    uint32_t csr_src_idx = 0u;
    uint32_t csr_edge_idx = 0u;
    uint32_t csr_edge_end = 0u;
    uint32_t csr_cur_u = 0u;

    while (ws.status == IMPULSE_VM_V11_OK) {
        uint32_t len = 0u;
        if (fwd_csr != nullptr) {
            // Stream next chunk of up to m_max edges directly from CSR
            const V11RelView& rel = *fwd_csr->rel;
            const uint32_t* s_ids = fwd_csr->src_ids;
            const uint32_t s_len = fwd_csr->src_len;
            const uint32_t* s_excl = fwd_csr->s_excl;
            const uint32_t s_excl_len = fwd_csr->s_excl_len;
            while (len < m_max) {
                while (csr_edge_idx >= csr_edge_end) {
                    if (csr_src_idx >= s_len) goto csr_chunk_ready;
                    csr_cur_u = s_ids[csr_src_idx++];
                    if (csr_cur_u + 1u < rel.row_off_len) {
                        csr_edge_idx = rel.csr_row_off(csr_cur_u);
                        csr_edge_end = rel.csr_row_off(csr_cur_u + 1u);
                    } else {
                        csr_edge_idx = 0u;
                        csr_edge_end = 0u;
                    }
                }
                if (!hoisted_bufs) {
                    if (!ensure_morsel_bufs(m_max)) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; break; }
                }
                while (csr_edge_idx < csr_edge_end && len < m_max) {
                    uint32_t e = csr_edge_idx++;
                    uint32_t v = static_cast<uint32_t>(rel.csr_col_tgt(e));
                    if (s_excl_len == 0u || !p3_sorted_contains(s_excl, s_excl_len, v)) {
                        m0[len] = static_cast<int64_t>(csr_cur_u);
                        m1[len] = static_cast<int64_t>(v);
                        m2[len] = static_cast<int64_t>(e);
                        m3[len] = 1LL;
                        ++len;
                    }
                }
            }
        csr_chunk_ready:
            if (ws.status != IMPULSE_VM_V11_OK || len == 0u) break;
        } else {
            if (cursor >= total) break;
            len = (total - cursor < m_max) ? (total - cursor) : m_max;
            uint32_t alloc_cap = (total < m_max) ? total : m_max;
            if (!ensure_morsel_bufs(alloc_cap)) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; break; }
            for (uint32_t i = 0; i < len; ++i) {
                m0[i] = static_cast<int64_t>(items[cursor + i].m0);
                if (!is_filter) {
                    m1[i] = static_cast<int64_t>(items[cursor + i].m1);
                    m2[i] = static_cast<int64_t>(items[cursor + i].m2);
                }
                m3[i] = static_cast<int64_t>(items[cursor + i].m3);
            }
            cursor += len;
        }

        if (steps >= max_steps) {
            ws.status = IMPULSE_VM_V11_ERR_GAS_EXHAUSTED;
            break;
        }
        ++steps;
        if (ws.gas_remaining > 0ULL) --ws.gas_remaining;

        const size_t chunk_mark = ws.arena.pos;

        ws.f_regs[8].reset_empty(0xFFFFu, len);
        ws.f_regs[8].nodes = mf0_ids;
        ws.f_regs[8].node_count = len;
        ws.f_regs[8].tier = compute_tier(kGeomP0, len, len, false);
        ws.f_regs[8].cached_card = static_cast<int64_t>(len);
        ws.set_f_init(8, true);
        new (v8)  V11Vector(); v8->elem_tag = kVecTagU64;  v8->length = len;  v8->i64_data = m0;
        new (v11) V11Vector(); v11->elem_tag = kVecTagU64; v11->length = len; v11->i64_data = m3;
        ws.v_regs[8]  = v8;
        ws.v_regs[11] = v11;
        if (!is_filter) {
            new (v9)  V11Vector(); v9->elem_tag = kVecTagU64;  v9->length = len;  v9->i64_data = m1;
            new (v10) V11Vector(); v10->elem_tag = kVecTagU64; v10->length = len; v10->i64_data = m2;
            ws.v_regs[9]  = v9;
            ws.v_regs[10] = v10;
        } else {
            ws.v_regs[9]  = nullptr;
            ws.v_regs[10] = nullptr;
        }

        V11Vector* morsel_spills[16]{};
        ws.state.pc = hdr_pc + 1u;

        while (ws.state.pc < end_pc && ws.status == IMPULSE_VM_V11_OK) {
            uint64_t bw = program[ws.state.pc];
            uint8_t op    = IMP_V11_OPCODE(bw);
            uint8_t subop = IMP_V11_SUBOP(bw);
            uint8_t flags = IMP_V11_FLAGS(bw);
            uint8_t s1    = IMP_V11_S1(bw);
            uint8_t dst   = IMP_V11_DST(bw);
            uint8_t s3    = IMP_V11_S3(bw);
            uint8_t s2    = IMP_V11_S2(bw);
            int32_t imm32_s = IMP_V11_IMM32_S(bw);
            uint16_t imm_lo = IMP_V11_IMM16_LO(bw);
            uint16_t imm_hi = IMP_V11_IMM16_HI(bw);
            bool masked = (flags & FLAG_MASKED_S3) != 0u;
            bool scalar = (flags & FLAG_SCALAR_RHS) != 0u;

            const V11Frontier* active_mask = nullptr;
            bool use_sparse_mask = false;
            const uint32_t* mask_ids = nullptr;
            uint32_t mask_cnt = 0u;
            if (masked) {
                active_mask = p3_get_f1bit(ws, s3);
                if (!active_mask) break;
                if (active_mask->tier == kTierEmpty) {
                    use_sparse_mask = true;
                    mask_ids = nullptr;
                    mask_cnt = 0u;
                } else if (active_mask->tier != kTierAllSpan) {
                    use_sparse_mask = true;
                    mask_ids = active_mask->nodes;
                    mask_cnt = active_mask->node_count;
                }
            }
            auto for_each_lane = [&](auto&& fn) noexcept {
                if (use_sparse_mask) {
                    for (uint32_t k = 0; k < mask_cnt; ++k) {
                        uint32_t i = mask_ids[k];
                        if (i < len) fn(i);
                    }
                } else {
                    for (uint32_t i = 0; i < len; ++i) fn(i);
                }
            };

#ifdef IMPULSE_V11_OPCODE_PROFILE
            uint64_t m_prof_t0 = (op >= 0x20u && op < 128u) ? v11_profile_now_ns() : 0ULL;
            if (op < 0x20u) ws.op_stats[op].calls += 1ULL;
#endif
            switch (static_cast<Opcode>(op)) {
                case Opcode::OP_NOP:
                    ws.state.pc += 1u;
                    break;
                case Opcode::OP_SPILL:
                    if (subop != 2u || imm_lo >= 16u) { ws.status = IMPULSE_VM_V11_ERR_STACK_OVERFLOW; break; }
                    morsel_spills[imm_lo] = ws.v_regs[s1];
                    ws.state.pc += 1u;
                    break;
                case Opcode::OP_RELOAD:
                    if (subop != 2u || imm_lo >= 16u) { ws.status = IMPULSE_VM_V11_ERR_STACK_OVERFLOW; break; }
                    if (!morsel_spills[imm_lo]) { ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; break; }
                    ws.v_regs[dst] = morsel_spills[imm_lo];
                    ws.state.pc += 1u;
                    break;
                case Opcode::OP_JMP: {
                    int64_t tgt = static_cast<int64_t>(ws.state.pc) + imm32_s;
                    if (tgt <= static_cast<int64_t>(hdr_pc) || tgt > static_cast<int64_t>(end_pc)) {
                        ws.status = IMPULSE_VM_V11_ERR_VALIDATION_FAILED; break;
                    }
                    ws.state.pc = static_cast<uint32_t>(tgt);
                    break;
                }
                case Opcode::OP_JCC: {
                    bool take = false;
                    if (subop == 6u) take = ws.is_r_valid(s1) && ws.read_r_raw(s1) != 0ULL;
                    else if (subop == 7u) take = !ws.is_r_valid(s1) || ws.read_r_raw(s1) == 0ULL;
                    else if (subop == 8u) take = !ws.is_f_init(s1) || ws.f_regs[s1].is_empty();
                    else if (subop == 9u) take = ws.is_f_init(s1) && !ws.f_regs[s1].is_empty();
                    if (take) {
                        int64_t tgt = static_cast<int64_t>(ws.state.pc) + imm32_s;
                        if (tgt <= static_cast<int64_t>(hdr_pc) || tgt > static_cast<int64_t>(end_pc)) {
                            ws.status = IMPULSE_VM_V11_ERR_VALIDATION_FAILED; break;
                        }
                        ws.state.pc = static_cast<uint32_t>(tgt);
                    } else {
                        ws.state.pc += 1u;
                    }
                    break;
                }
                case Opcode::OP_MOV:
                    if (subop == 1u) {
                        if (!ws.is_f_init(s1)) { ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; break; }
                        if (!ws.f_regs[dst].copy_from(ws.arena, ws.f_regs[s1])) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; break; }
                        ws.set_f_init(dst, true);
                        ws.state.pc += 1u;
                    } else if (subop == 2u) {
                        if (!ws.v_regs[s1]) { ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; break; }
                        ws.v_regs[dst] = ws.v_regs[s1];
                        ws.state.pc += 1u;
                    } else {
                        ws.status = IMPULSE_VM_V11_ERR_INVALID_OPCODE;
                    }
                    break;
                case Opcode::OP_SWAP:
                    if (subop == 1u) {
                        bool id = ws.is_f_init(dst), is = ws.is_f_init(s1);
                        V11Frontier t = ws.f_regs[dst]; ws.f_regs[dst] = ws.f_regs[s1]; ws.f_regs[s1] = t;
                        ws.set_f_init(dst, is); ws.set_f_init(s1, id);
                        ws.state.pc += 1u;
                    } else if (subop == 2u) {
                        V11Vector* t = ws.v_regs[dst]; ws.v_regs[dst] = ws.v_regs[s1]; ws.v_regs[s1] = t;
                        ws.state.pc += 1u;
                    } else {
                        ws.status = IMPULSE_VM_V11_ERR_INVALID_OPCODE;
                    }
                    break;
                case Opcode::OP_SET_OP: {
                    const V11Frontier* fa = p3_get_f1bit(ws, s1);
                    const V11Frontier* fb = p3_get_f1bit(ws, s2);
                    if (!fa || !fb) break;
                    uint32_t* out = ws.arena.alloc_array<uint32_t>(len, 8);
                    if (!out) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; break; }
                    uint32_t cnt = 0u;
                    for (uint32_t i = 0; i < len; ++i) {
                        bool a = fa->contains_1bit(i), b = fb->contains_1bit(i);
                        bool r = (subop == 0u) ? (a || b) : (subop == 1u) ? (a && b) : (subop == 2u) ? (a && !b) : (subop == 3u) ? (a != b) : false;
                        if (r) out[cnt++] = i;
                    }
                    if (!ws.f_regs[dst].init_from_sorted_ids(ws.arena, 0xFFFFu, len, out, cnt, kGeomP0, true)) {
                        ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; break;
                    }
                    ws.set_f_init(dst, true);
                    ws.state.pc += 1u;
                    break;
                }
                case Opcode::OP_SET_COMPLEMENT: {
                    const V11Frontier* fa = p3_get_f1bit(ws, s1);
                    if (!fa) break;
                    uint32_t* out = ws.arena.alloc_array<uint32_t>(len, 8);
                    if (!out) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; break; }
                    uint32_t cnt = 0u;
                    for (uint32_t i = 0; i < len; ++i) {
                        if (!fa->contains_1bit(i)) out[cnt++] = i;
                    }
                    if (!ws.f_regs[dst].init_from_sorted_ids(ws.arena, 0xFFFFu, len, out, cnt, kGeomP0, true)) {
                        ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; break;
                    }
                    ws.set_f_init(dst, true);
                    ws.state.pc += 1u;
                    break;
                }
                case Opcode::OP_GATHER_NODE_ATTR:
                case Opcode::OP_GATHER_EDGE_ATTR: {
                    const V11Vector* idx_vec = ws.v_regs[s1];
                    if (!idx_vec || idx_vec->elem_tag != kVecTagU64) { ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; break; }
                    if (idx_vec->length < len) { ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; break; }
                    const V11Vector* col = nullptr;
                    if (op == static_cast<uint8_t>(Opcode::OP_GATHER_NODE_ATTR)) {
                        col = p3_find_node_attr(&view, imm_lo, imm_hi);
                    } else {
                        if (imm_lo >= view.relation_count) { ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; break; }
                        col = p3_find_edge_attr(view.relations[imm_lo], imm_hi);
                    }
                    if (!col) { ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; break; }
                    // Fast path: unmasked, all-valid u64 index vector (M0/M1/M2) into f64 column
                    if (!use_sparse_mask && col->elem_tag == kVecTagF64 && idx_vec->valid_bits == nullptr) {
                        size_t vwords = (static_cast<size_t>(len) + 63u) >> 6;
                        if (vwords == 0u) vwords = 1u;
                        uint64_t* vbits = ws.arena.alloc_array<uint64_t>(vwords, 8);
                        double* out = ws.arena.alloc_array<double>(len, 8);
                        auto* ov = static_cast<V11Vector*>(ws.arena.alloc(sizeof(V11Vector), alignof(V11Vector)));
                        if (!vbits || !out || !ov) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; break; }
                        const int64_t* __restrict idxs = idx_vec->i64_data;
                        const double* __restrict src_col = col->f64_data;
                        const uint64_t col_len = col->length;
                        bool oob = false;
                        if (col->valid_bits == nullptr) {
                            uint32_t i = 0u;
                            for (; i + 4u <= len; i += 4u) {
                                uint64_t id0 = static_cast<uint64_t>(idxs[i + 0u]);
                                uint64_t id1 = static_cast<uint64_t>(idxs[i + 1u]);
                                uint64_t id2 = static_cast<uint64_t>(idxs[i + 2u]);
                                uint64_t id3 = static_cast<uint64_t>(idxs[i + 3u]);
                                if (id0 >= col_len || id1 >= col_len || id2 >= col_len || id3 >= col_len) { oob = true; break; }
                                out[i + 0u] = src_col[id0];
                                out[i + 1u] = src_col[id1];
                                out[i + 2u] = src_col[id2];
                                out[i + 3u] = src_col[id3];
                            }
                            if (!oob) {
                                for (; i < len; ++i) {
                                    uint64_t id = static_cast<uint64_t>(idxs[i]);
                                    if (id >= col_len) { oob = true; break; }
                                    out[i] = src_col[id];
                                }
                            }
                            if (oob) { ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; break; }
                            std::memset(vbits, 0xFF, vwords * sizeof(uint64_t));
                            if (len & 63u) vbits[vwords - 1u] = (1ULL << (len & 63u)) - 1ULL;
                        } else {
                            std::memset(vbits, 0, vwords * sizeof(uint64_t));
                            for (uint32_t i = 0; i < len; ++i) {
                                uint64_t id = static_cast<uint64_t>(idxs[i]);
                                if (id >= col_len) { oob = true; break; }
                                if (col->is_valid_at(static_cast<uint32_t>(id))) {
                                    out[i] = src_col[id];
                                    vbits[i >> 6] |= (1ULL << (i & 63u));
                                }
                            }
                            if (oob) { ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; break; }
                        }
                        new (ov) V11Vector();
                        ov->elem_tag = kVecTagF64; ov->length = len; ov->f64_data = out; ov->valid_bits = vbits;
                        ws.v_regs[dst] = ov;
                        ws.state.pc += 1u;
                        break;
                    }
                    bool oob = false;
                    for_each_lane([&](uint32_t i) {
                        if (!oob && idx_vec->is_valid_at(i)) {
                            uint64_t id = static_cast<uint64_t>(idx_vec->i64_data[i]);
                            if (id >= col->length) oob = true;
                        }
                    });
                    if (oob) { ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; break; }
                    size_t vwords = (static_cast<size_t>(len) + 63u) >> 6;
                    if (vwords == 0u) vwords = 1u;
                    uint64_t* vbits = ws.arena.alloc_array<uint64_t>(vwords, 8);
                    auto* ov = static_cast<V11Vector*>(ws.arena.alloc(sizeof(V11Vector), alignof(V11Vector)));
                    if (!vbits || !ov) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; break; }
                    std::memset(vbits, 0, vwords * sizeof(uint64_t));
                    new (ov) V11Vector();
                    ov->elem_tag = col->elem_tag;
                    ov->length = len;
                    ov->valid_bits = vbits;
                    if (col->elem_tag == kVecTagF64) {
                        ov->f64_data = ws.arena.alloc_array<double>(len, 8);
                        if (!ov->f64_data) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; break; }
                        for_each_lane([&](uint32_t i) {
                            if (!idx_vec->is_valid_at(i)) return;
                            uint32_t id = static_cast<uint32_t>(idx_vec->i64_data[i]);
                            if (col->is_valid_at(id)) {
                                ov->f64_data[i] = col->f64_data[id];
                                vbits[i >> 6] |= (1ULL << (i & 63u));
                            }
                        });
                    } else if (col->elem_tag == kVecTagI64 || col->elem_tag == kVecTagU64) {
                        ov->i64_data = ws.arena.alloc_array<int64_t>(len, 8);
                        if (!ov->i64_data) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; break; }
                        for_each_lane([&](uint32_t i) {
                            if (!idx_vec->is_valid_at(i)) return;
                            uint32_t id = static_cast<uint32_t>(idx_vec->i64_data[i]);
                            if (col->is_valid_at(id)) {
                                ov->i64_data[i] = col->i64_data[id];
                                vbits[i >> 6] |= (1ULL << (i & 63u));
                            }
                        });
                    } else if (col->elem_tag == kVecTagStr) {
                        ov->str_data = ws.arena.alloc_array<V11StrSlice>(len, alignof(V11StrSlice));
                        if (!ov->str_data) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; break; }
                        std::memset(ov->str_data, 0, static_cast<size_t>(len) * sizeof(V11StrSlice));
                        for_each_lane([&](uint32_t i) {
                            if (!idx_vec->is_valid_at(i)) return;
                            uint32_t id = static_cast<uint32_t>(idx_vec->i64_data[i]);
                            if (col->is_valid_at(id)) {
                                ov->str_data[i] = col->str_data[id];
                                vbits[i >> 6] |= (1ULL << (i & 63u));
                            }
                        });
                    } else {
                        ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS;
                        break;
                    }
                    ws.v_regs[dst] = ov;
                    ws.state.pc += 1u;
                    break;
                }
                case Opcode::OP_COALESCE: {
                    const V11Vector* lhs = ws.v_regs[s1];
                    if (!lhs) { ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; break; }
                    size_t vwords = (static_cast<size_t>(len) + 63u) >> 6;
                    if (vwords == 0u) vwords = 1u;
                    uint64_t* vbits = ws.arena.alloc_array<uint64_t>(vwords, 8);
                    auto* ov = static_cast<V11Vector*>(ws.arena.alloc(sizeof(V11Vector), alignof(V11Vector)));
                    if (!vbits || !ov) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; break; }
                    std::memset(vbits, 0, vwords * sizeof(uint64_t));
                    new (ov) V11Vector();
                    ov->elem_tag = lhs->elem_tag;
                    ov->length = len;
                    ov->valid_bits = vbits;
                    const V11Vector* rv = scalar ? nullptr : ws.v_regs[s2];
                    if (lhs->elem_tag == kVecTagF64) {
                        ov->f64_data = ws.arena.alloc_array<double>(len, 8);
                        if (!ov->f64_data) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; break; }
                        double r_sc = scalar && ws.is_r_valid(s2) ? p1_bits_to_f64(ws.read_r_raw(s2)) : std::numeric_limits<double>::quiet_NaN();
                        bool r_sc_ok = scalar && ws.is_r_valid(s2) && !std::isnan(r_sc);
                        for_each_lane([&](uint32_t i) {
                            if (i < lhs->length && lhs->is_valid_at(i)) {
                                ov->f64_data[i] = lhs->f64_data[i];
                                vbits[i >> 6] |= (1ULL << (i & 63u));
                            } else if (scalar) {
                                if (r_sc_ok) { ov->f64_data[i] = r_sc; vbits[i >> 6] |= (1ULL << (i & 63u)); }
                            } else if (rv && rv->elem_tag == kVecTagF64 && i < rv->length && rv->is_valid_at(i)) {
                                ov->f64_data[i] = rv->f64_data[i];
                                vbits[i >> 6] |= (1ULL << (i & 63u));
                            }
                        });
                        ws.v_regs[dst] = ov;
                        ws.state.pc += 1u;
                    } else if (lhs->elem_tag == kVecTagI64 || lhs->elem_tag == kVecTagU64) {
                        ov->i64_data = ws.arena.alloc_array<int64_t>(len, 8);
                        if (!ov->i64_data) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; break; }
                        for_each_lane([&](uint32_t i) {
                            if (i < lhs->length && lhs->is_valid_at(i)) {
                                ov->i64_data[i] = lhs->i64_data[i];
                                vbits[i >> 6] |= (1ULL << (i & 63u));
                            } else if (scalar) {
                                if (ws.is_r_valid(s2)) { ov->i64_data[i] = static_cast<int64_t>(ws.read_r_raw(s2)); vbits[i >> 6] |= (1ULL << (i & 63u)); }
                            } else if (rv && rv->elem_tag == lhs->elem_tag && i < rv->length && rv->is_valid_at(i)) {
                                ov->i64_data[i] = rv->i64_data[i];
                                vbits[i >> 6] |= (1ULL << (i & 63u));
                            }
                        });
                        ws.v_regs[dst] = ov;
                        ws.state.pc += 1u;
                    } else {
                        ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH;
                    }
                    break;
                }
                case Opcode::OP_VEC_MATH_ARITH: {
                    const V11Vector* lhs = ws.v_regs[s1];
                    if (!lhs || lhs->elem_tag != kVecTagF64) { ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; break; }
                    size_t vwords = (static_cast<size_t>(len) + 63u) >> 6;
                    if (vwords == 0u) vwords = 1u;
                    uint64_t* vbits = ws.arena.alloc_array<uint64_t>(vwords, 8);
                    double* out = ws.arena.alloc_array<double>(len, 8);
                    auto* ov = static_cast<V11Vector*>(ws.arena.alloc(sizeof(V11Vector), alignof(V11Vector)));
                    if (!vbits || !out || !ov) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; break; }
                    const V11Vector* rv = scalar ? nullptr : ws.v_regs[s2];
                    double r_sc = scalar && ws.is_r_valid(s2) ? p1_bits_to_f64(ws.read_r_raw(s2)) : std::numeric_limits<double>::quiet_NaN();
                    bool r_sc_ok = scalar && ws.is_r_valid(s2) && !std::isnan(r_sc);
                    if (!use_sparse_mask && lhs->length >= len &&
                        (subop <= 3u || subop == 8u || subop == 9u || subop == 10u) &&
                        (subop >= 8u || scalar || (rv && rv->elem_tag == kVecTagF64 && rv->length >= len))) {
                        if (subop >= 8u) {
                            simd::f64_arith_unary_valid(subop, lhs->f64_data, lhs->valid_bits, out, vbits, len);
                        } else if (scalar) {
                            if (r_sc_ok) {
                                simd::f64_arith_scalar_valid(subop, lhs->f64_data, r_sc, lhs->valid_bits, out, vbits, len);
                            } else {
                                std::memset(vbits, 0, vwords * sizeof(uint64_t));
                            }
                        } else {
                            simd::f64_arith_vec_valid(subop, lhs->f64_data, rv->f64_data, lhs->valid_bits, rv->valid_bits, out, vbits, len);
                        }
                    } else {
                        std::memset(vbits, 0, vwords * sizeof(uint64_t));
                        for_each_lane([&](uint32_t i) {
                            if (i >= lhs->length || !lhs->is_valid_at(i)) return;
                            double x = lhs->f64_data[i];
                            double r = std::numeric_limits<double>::quiet_NaN();
                            if (subop == 8u) r = -x;
                            else if (subop == 9u) r = std::fabs(x);
                            else if (subop == 10u) { if (x < 0.0) return; r = std::sqrt(x); }
                            else {
                                bool has_y = scalar ? r_sc_ok : (rv && rv->elem_tag == kVecTagF64 && i < rv->length && rv->is_valid_at(i));
                                if (!has_y) return;
                                double y = scalar ? r_sc : rv->f64_data[i];
                                if (subop == 0u) r = x + y;
                                else if (subop == 1u) r = x - y;
                                else if (subop == 2u) r = x * y;
                                else if (subop == 3u) r = (y == 0.0) ? std::numeric_limits<double>::quiet_NaN() : (x / y);
                                else if (subop == 6u) r = p1_ieee_min_f64(x, y);
                                else if (subop == 7u) r = p1_ieee_max_f64(x, y);
                            }
                            if (!std::isnan(r)) {
                                out[i] = r;
                                vbits[i >> 6] |= (1ULL << (i & 63u));
                            }
                        });
                    }
                    new (ov) V11Vector();
                    ov->elem_tag = kVecTagF64; ov->length = len; ov->f64_data = out; ov->valid_bits = vbits;
                    ws.v_regs[dst] = ov;
                    ws.state.pc += 1u;
                    break;
                }
                case Opcode::OP_VEC_CMP: {
                    const V11Vector* lhs = ws.v_regs[s1];
                    if (lhs && (lhs->elem_tag == kVecTagI64 || lhs->elem_tag == kVecTagU64)) {
                        v11_exec_morsel_p4_op(ws, &view, bw);
                        break;
                    }
                    if (!lhs || lhs->elem_tag != kVecTagF64) { ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; break; }
                    size_t vwords = (static_cast<size_t>(len) + 63u) >> 6;
                    if (vwords == 0u) vwords = 1u;
                    uint64_t* valid = ws.arena.alloc_array<uint64_t>(vwords, 8);
                    uint64_t* data  = ws.arena.alloc_array<uint64_t>(vwords, 8);
                    auto* ov = static_cast<V11Vector*>(ws.arena.alloc(sizeof(V11Vector), alignof(V11Vector)));
                    if (!valid || !data || !ov) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; break; }
                    const V11Vector* rv = scalar ? nullptr : ws.v_regs[s2];
                    bool r_sc_ok = scalar && ws.is_r_valid(s2);
                    double r_sc = r_sc_ok ? p1_bits_to_f64(ws.read_r_raw(s2)) : std::numeric_limits<double>::quiet_NaN();
                    if (!use_sparse_mask && subop <= 5u && lhs->length >= len &&
                        (scalar || (rv && rv->elem_tag == kVecTagF64 && rv->length >= len))) {
                        simd::f64_cmp_words(subop, lhs->f64_data, rv ? rv->f64_data : nullptr,
                                            scalar, r_sc, r_sc_ok,
                                            lhs->valid_bits, rv ? rv->valid_bits : nullptr,
                                            valid, data, len);
                    } else {
                        std::memset(valid, 0, vwords * sizeof(uint64_t));
                        std::memset(data, 0, vwords * sizeof(uint64_t));
                        for_each_lane([&](uint32_t i) {
                            if (i >= lhs->length || !lhs->is_valid_at(i)) return;
                            bool has_y = scalar ? r_sc_ok : (rv && rv->elem_tag == kVecTagF64 && i < rv->length && rv->is_valid_at(i));
                            if (!has_y) return;
                            double x = lhs->f64_data[i];
                            double y = scalar ? r_sc : rv->f64_data[i];
                            if (std::isnan(x) || std::isnan(y)) return;
                            bool res = false;
                            if (subop == 0u) res = (x == y);
                            else if (subop == 1u) res = (x != y);
                            else if (subop == 2u) res = (x <  y);
                            else if (subop == 3u) res = (x <= y);
                            else if (subop == 4u) res = (x >  y);
                            else if (subop == 5u) res = (x >= y);
                            uint64_t bit = 1ULL << (i & 63u);
                            valid[i >> 6] |= bit;
                            if (res) data[i >> 6] |= bit;
                        });
                    }
                    new (ov) V11Vector();
                    ov->elem_tag = kVecTagBool3vl; ov->length = len; ov->valid_bits = valid; ov->bool_data_bits = data;
                    ws.v_regs[dst] = ov;
                    ws.state.pc += 1u;
                    break;
                }
                case Opcode::OP_SET_CONVERT: {
                    if (subop != 2u) { ws.status = IMPULSE_VM_V11_ERR_INVALID_OPCODE; break; }
                    const V11Vector* bv = ws.v_regs[s1];
                    if (!bv || bv->elem_tag != kVecTagBool3vl) { ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; break; }
                    uint32_t* tmp = (bv->length > 0u) ? ws.arena.alloc_array<uint32_t>(bv->length, 8) : nullptr;
                    if (bv->length > 0u && !tmp) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; break; }
                    uint32_t cnt = (bv->length > 0u)
                        ? simd::b2f_extract_words(bv->valid_bits, bv->bool_data_bits, bv->length, tmp)
                        : 0u;
                    if (!ws.f_regs[dst].init_from_sorted_ids(ws.arena, 0xFFFFu, len, tmp, cnt, kGeomP0, true)) {
                        ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; break;
                    }
                    ws.set_f_init(dst, true);
                    ws.state.pc += 1u;
                    break;
                }
                case Opcode::OP_MORSEL_COMMIT: {
                    const V11Frontier* cmask = masked ? active_mask
                        : ((ws.is_f_init(8) && !ws.f_regs[8].multi_lane) ? &ws.f_regs[8] : nullptr);
                    const uint32_t* cm_ids = nullptr;
                    uint32_t cm_len = 0u;
                    if (cmask && !cmask->materialize_1bit(ws.arena, &cm_ids, &cm_len)) {
                        ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; break;
                    }
                    switch (subop) {
                        case 0: { // .yield_frontier
                            const V11Vector* ids = ws.v_regs[s1];
                            if (!ids || ids->elem_tag != kVecTagU64) { ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; break; }
                            uint64_t* fbits = acc_f_bits[dst];
                            uint32_t fcap = acc_f_bit_cap[dst];
                            for (uint32_t k = 0; k < cm_len; ++k) {
                                uint32_t i = cm_ids[k];
                                if (i < ids->length && ids->is_valid_at(i)) {
                                    uint32_t id = static_cast<uint32_t>(ids->i64_data[i]);
                                    if (fbits && id < fcap) {
                                        fbits[id >> 6] |= (1ULL << (id & 63u));
                                    } else {
                                        can_rewind_chunks = false;
                                        if (!acc_f[dst].push(ws.arena, id)) {
                                            ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; break;
                                        }
                                    }
                                }
                            }
                            if (ws.status == IMPULSE_VM_V11_OK) ws.state.pc += 1u;
                            break;
                        }
                        case 1: { // .yield_lane_or
                            const V11Vector* ids = ws.v_regs[s1];
                            const V11Vector* lms = scalar ? nullptr : ws.v_regs[s2];
                            if (!ids || ids->elem_tag != kVecTagU64 ||
                                (!scalar && (!lms || (lms->elem_tag != kVecTagU64 && lms->elem_tag != kVecTagI64)))) {
                                ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; break;
                            }
                            bool r_ok = scalar && ws.is_r_valid(s2);
                            uint64_t r_val = r_ok ? ws.read_r_raw(s2) : 0ULL;
                            for (uint32_t k = 0; k < cm_len; ++k) {
                                uint32_t i = cm_ids[k];
                                if (i >= ids->length || !ids->is_valid_at(i)) continue;
                                bool has_lm = scalar ? r_ok : (i < lms->length && lms->is_valid_at(i));
                                if (!has_lm) continue;
                                uint64_t lm = scalar ? r_val : static_cast<uint64_t>(lms->i64_data[i]);
                                if (lm != 0ULL) {
                                    if (!acc_ml[dst].or_entry(ws.arena, static_cast<uint64_t>(ids->i64_data[i]), lm)) {
                                        ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; break;
                                    }
                                }
                            }
                            if (ws.status == IMPULSE_VM_V11_OK) ws.state.pc += 1u;
                            break;
                        }
                        case 2: case 3: case 4: { // .scatter_add / min / max
                            const V11Vector* ids = ws.v_regs[s1];
                            if (!ids || ids->elem_tag != kVecTagU64 || !has_vf64[dst]) {
                                ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; break;
                            }
                            const V11Vector* vals = scalar ? nullptr : ws.v_regs[s2];
                            bool r_ok = scalar && ws.is_r_valid(s2);
                            double r_val = r_ok ? p1_bits_to_f64(ws.read_r_raw(s2)) : std::numeric_limits<double>::quiet_NaN();
                            // Fast path: non-accum scatter_add with all-valid dense sink (1 L2 write per edge!)
                            if (subop == 2u && !scalar && acc_vf64_valid[dst] == nullptr &&
                                vals && vals->elem_tag == kVecTagF64 &&
                                ids->valid_bits == nullptr && ids->length >= len && vals->length >= len) {
                                double* __restrict dst_arr = acc_vf64[dst];
                                const uint32_t dst_len = acc_vf64_len[dst];
                                const int64_t* __restrict id_arr = ids->i64_data;
                                const double* __restrict val_arr = vals->f64_data;
                                const uint64_t* val_bits = vals->valid_bits;
                                for (uint32_t k = 0; k < cm_len; ++k) {
                                    uint32_t i = cm_ids[k];
                                    if (val_bits && (((val_bits[i >> 6] >> (i & 63u)) & 1ULL) == 0ULL)) continue;
                                    double v = val_arr[i];
                                    if (__builtin_expect(!(v == v), 0)) continue;
                                    uint64_t tgt64 = static_cast<uint64_t>(id_arr[i]);
                                    if (__builtin_expect(tgt64 >= dst_len, 0)) {
                                        ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; break;
                                    }
                                    double sum = dst_arr[tgt64] + v;
                                    if (__builtin_expect(sum == sum, 1)) {
                                        dst_arr[tgt64] = sum;
                                    } else {
                                        can_rewind_chunks = false;
                                        acc_vf64_valid[dst] = ws.arena.alloc_array<uint8_t>(dst_len, 8);
                                        if (!acc_vf64_valid[dst]) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; break; }
                                        std::memset(acc_vf64_valid[dst], 1, dst_len);
                                        acc_vf64_valid[dst][tgt64] = 0u;
                                        for (uint32_t k2 = k + 1u; k2 < cm_len; ++k2) {
                                            uint32_t i2 = cm_ids[k2];
                                            if (val_bits && (((val_bits[i2 >> 6] >> (i2 & 63u)) & 1ULL) == 0ULL)) continue;
                                            double v2 = val_arr[i2];
                                            if (std::isnan(v2)) continue;
                                            uint64_t t2 = static_cast<uint64_t>(id_arr[i2]);
                                            if (t2 >= dst_len) { ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; break; }
                                            double prev2 = acc_vf64_valid[dst][t2] ? dst_arr[t2] : 0.0;
                                            double s2v = prev2 + v2;
                                            if (std::isnan(s2v)) acc_vf64_valid[dst][t2] = 0u;
                                            else { dst_arr[t2] = s2v; acc_vf64_valid[dst][t2] = 1u; }
                                        }
                                        break;
                                    }
                                }
                                if (ws.status == IMPULSE_VM_V11_OK) ws.state.pc += 1u;
                                break;
                            }
                            if (acc_vf64_valid[dst] == nullptr && acc_vf64_len[dst] > 0u) {
                                can_rewind_chunks = false;
                                acc_vf64_valid[dst] = ws.arena.alloc_array<uint8_t>(acc_vf64_len[dst], 8);
                                if (!acc_vf64_valid[dst]) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; break; }
                                std::memset(acc_vf64_valid[dst], 1, acc_vf64_len[dst]);
                            }
                            for (uint32_t k = 0; k < cm_len; ++k) {
                                uint32_t i = cm_ids[k];
                                if (i >= ids->length || !ids->is_valid_at(i)) continue;
                                bool has_v = scalar ? (r_ok && !std::isnan(r_val))
                                                    : (vals && vals->elem_tag == kVecTagF64 && i < vals->length && vals->is_valid_at(i));
                                if (!has_v) continue;
                                double v = scalar ? r_val : vals->f64_data[i];
                                if (std::isnan(v)) continue;
                                uint64_t tgt64 = static_cast<uint64_t>(ids->i64_data[i]);
                                if (tgt64 >= acc_vf64_len[dst]) {
                                    ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; break;
                                }
                                uint32_t ti = static_cast<uint32_t>(tgt64);
                                if (subop == 2u) {
                                    double prev = acc_vf64_valid[dst][ti] ? acc_vf64[dst][ti] : 0.0;
                                    double sum = prev + v;
                                    if (std::isnan(sum)) acc_vf64_valid[dst][ti] = 0u;
                                    else { acc_vf64[dst][ti] = sum; acc_vf64_valid[dst][ti] = 1u; }
                                } else if (subop == 3u) {
                                    acc_vf64[dst][ti] = acc_vf64_valid[dst][ti] ? p1_ieee_min_f64(acc_vf64[dst][ti], v) : v;
                                    acc_vf64_valid[dst][ti] = 1u;
                                } else {
                                    acc_vf64[dst][ti] = acc_vf64_valid[dst][ti] ? p1_ieee_max_f64(acc_vf64[dst][ti], v) : v;
                                    acc_vf64_valid[dst][ti] = 1u;
                                }
                            }
                            if (ws.status == IMPULSE_VM_V11_OK) ws.state.pc += 1u;
                            break;
                        }
                        case 5: case 6: case 7: { // .scatter_min_src / argmin / argmax
                            const V11Vector* ids = ws.v_regs[s1];
                            if (!ids || ids->elem_tag != kVecTagU64 || !has_vu64[dst]) {
                                ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; break;
                            }
                            const V11Vector* vals = scalar ? nullptr : ws.v_regs[s2];
                            for (uint32_t k = 0; k < cm_len; ++k) {
                                uint32_t i = cm_ids[k];
                                if (i >= ids->length || !ids->is_valid_at(i)) continue;
                                bool has_v = scalar ? ws.is_r_valid(s2)
                                                    : (vals && (vals->elem_tag == kVecTagU64 || vals->elem_tag == kVecTagI64) && i < vals->length && vals->is_valid_at(i));
                                if (!has_v) continue;
                                uint64_t v = scalar ? ws.read_r_raw(s2) : static_cast<uint64_t>(vals->i64_data[i]);
                                uint64_t tgt64 = static_cast<uint64_t>(ids->i64_data[i]);
                                if (tgt64 >= acc_vu64_len[dst]) {
                                    ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; break;
                                }
                                uint32_t ti = static_cast<uint32_t>(tgt64);
                                if (!acc_vu64_valid[dst][ti]) {
                                    acc_vu64[dst][ti] = static_cast<int64_t>(v);
                                    acc_vu64_valid[dst][ti] = 1u;
                                } else if (subop == 5u || subop == 6u) {
                                    if (v < static_cast<uint64_t>(acc_vu64[dst][ti])) acc_vu64[dst][ti] = static_cast<int64_t>(v);
                                } else {
                                    if (v > static_cast<uint64_t>(acc_vu64[dst][ti])) acc_vu64[dst][ti] = static_cast<int64_t>(v);
                                }
                            }
                            if (ws.status == IMPULSE_VM_V11_OK) ws.state.pc += 1u;
                            break;
                        }
                        case 8: // .count_add_r
                            acc_rcnt[dst] += static_cast<uint64_t>(cm_len);
                            ws.state.pc += 1u;
                            break;
                        default:
                            ws.status = IMPULSE_VM_V11_ERR_INVALID_OPCODE;
                            break;
                    }
                    break;
                }
                case Opcode::OP_GATHER_VEC:
                case Opcode::OP_VEC_3VL_OP:
                case Opcode::OP_VEC_STR_PRED:
                case Opcode::OP_VEC_MATH_EXP_LOG:
                case Opcode::OP_VEC_MATH_TRIG_HYP:
                case Opcode::OP_VEC_MATH_ACT:
                case Opcode::OP_VEC_INT_BITWISE:
                case Opcode::OP_VEC_TERNARY:
                case Opcode::OP_VEC_BLEND:
                    v11_exec_morsel_p4_op(ws, &view, bw);
                    break;
                default:
                    ws.status = IMPULSE_VM_V11_ERR_INVALID_OPCODE;
                    break;
            }
#ifdef IMPULSE_V11_OPCODE_PROFILE
            if (op >= 0x20u && op < 128u) {
                v11_profile_record(ws, op, v11_profile_now_ns() - m_prof_t0);
            }
#endif
        }
        if (can_rewind_chunks) {
            ws.arena.pos = chunk_mark;
        }
    }

    // Clean up morsel-private registers 8..15
    for (uint8_t k = 8u; k < 16u; ++k) {
        ws.set_f_init(k, false);
        ws.v_regs[k] = nullptr;
    }

    if (ws.status == IMPULSE_VM_V11_OK) {
        for (uint8_t d = 0; d < 8; ++d) {
            if ((committed_f_dom[d] >= 0 && static_cast<uint32_t>(committed_f_dom[d]) >= view.domain_count) ||
                (committed_ml_dom[d] >= 0 && static_cast<uint32_t>(committed_ml_dom[d]) >= view.domain_count)) {
                ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS;
                return;
            }
        }
        // Stage all commit allocations before mutating outer registers 0..7 (atomic on arena OOM)
        V11Frontier staged_f[8]{};
        bool        staged_f_set[8]{};
        V11Vector*  staged_v[8]{};
        bool        staged_v_set[8]{};
        bool any_non_empty = false;
        for (uint8_t d = 0; d < 8; ++d) {
            if (committed_f_dom[d] >= 0) {
                uint16_t dom = static_cast<uint16_t>(committed_f_dom[d]);
                uint64_t dcard = view.domain_cards[dom];
                if (acc_f_bits[d] && acc_f[d].len == 0u) {
                    size_t bw = (static_cast<size_t>(acc_f_bit_cap[d]) + 63u) >> 6;
                    uint32_t uniq = 0u;
                    for (size_t wi = 0; wi < bw; ++wi) uniq += static_cast<uint32_t>(__builtin_popcountll(acc_f_bits[d][wi]));
                    uint32_t* out_ids = (uniq > 0u) ? ws.arena.alloc_array<uint32_t>(uniq, 8) : nullptr;
                    if (uniq > 0u && !out_ids) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; return; }
                    uint32_t k = 0u;
                    for (size_t wi = 0; wi < bw; ++wi) {
                        uint64_t word = acc_f_bits[d][wi];
                        uint32_t base = static_cast<uint32_t>(wi << 6);
                        while (word != 0ULL) {
                            uint32_t b = static_cast<uint32_t>(__builtin_ctzll(word));
                            out_ids[k++] = base + b;
                            word &= (word - 1ULL);
                        }
                    }
                    if (uniq > 0u) any_non_empty = true;
                    staged_f[d].reset_empty(dom, dcard);
                    if (uniq > 0u) {
                        staged_f[d].nodes = out_ids;
                        staged_f[d].node_count = uniq;
                        staged_f[d].tier = compute_tier(ws.geometry, dcard, uniq, false);
                        staged_f[d].cached_card = (staged_f[d].tier == kTier0Inline || staged_f[d].tier == kTierAllSpan)
                            ? static_cast<int64_t>(uniq) : -1;
                    }
                    staged_f_set[d] = true;
                } else {
                    if (acc_f_bits[d]) {
                        size_t bw = (static_cast<size_t>(acc_f_bit_cap[d]) + 63u) >> 6;
                        for (size_t wi = 0; wi < bw; ++wi) {
                            uint64_t word = acc_f_bits[d][wi];
                            uint32_t base = static_cast<uint32_t>(wi << 6);
                            while (word != 0ULL) {
                                uint32_t b = static_cast<uint32_t>(__builtin_ctzll(word));
                                if (!acc_f[d].push(ws.arena, base + b)) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; return; }
                                word &= (word - 1ULL);
                            }
                        }
                    }
                    uint32_t uniq = p2_dedupe_sorted_inplace(acc_f[d].data, acc_f[d].len);
                    if (uniq > 0u) any_non_empty = true;
                    if (!staged_f[d].init_from_sorted_ids(ws.arena, dom, dcard, acc_f[d].data, uniq, ws.geometry, false)) {
                        ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; return;
                    }
                    staged_f_set[d] = true;
                }
            }
            if (committed_ml_dom[d] >= 0) {
                uint16_t dom = static_cast<uint16_t>(committed_ml_dom[d]);
                uint64_t dcard = view.domain_cards[dom];
                uint32_t cnt = acc_ml[d].len;
                if (cnt > 0u) any_non_empty = true;
                uint32_t* perm = (cnt > 0u) ? ws.arena.alloc_array<uint32_t>(cnt, 8) : nullptr;
                uint64_t* ns   = (cnt > 0u) ? ws.arena.alloc_array<uint64_t>(cnt, 8) : nullptr;
                uint64_t* ms   = (cnt > 0u) ? ws.arena.alloc_array<uint64_t>(cnt, 8) : nullptr;
                if (cnt > 0u && (!perm || !ns || !ms)) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; return; }
                for (uint32_t i = 0; i < cnt; ++i) perm[i] = i;
                std::sort(perm, perm + cnt, [&](uint32_t a, uint32_t b) { return acc_ml[d].nodes[a] < acc_ml[d].nodes[b]; });
                for (uint32_t i = 0; i < cnt; ++i) { ns[i] = acc_ml[d].nodes[perm[i]]; ms[i] = acc_ml[d].masks[perm[i]]; }
                if (!staged_f[d].init_multi_lane(ws.arena, dom, dcard, 64u, 64u, ns, ms, cnt, ws.geometry)) {
                    ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; return;
                }
                staged_f_set[d] = true;
            }
            if (committed_vf64[d] && has_vf64[d]) {
                uint32_t n = acc_vf64_len[d];
                size_t vwords = (n > 0u) ? ((static_cast<size_t>(n) + 63u) >> 6) : 1u;
                uint64_t* vbits = ws.arena.alloc_array<uint64_t>(vwords, 8);
                auto* ov = static_cast<V11Vector*>(ws.arena.alloc(sizeof(V11Vector), alignof(V11Vector)));
                if (!vbits || !ov) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; return; }
                if (acc_vf64_valid[d] == nullptr) {
                    std::memset(vbits, 0xFF, vwords * sizeof(uint64_t));
                    if (n & 63u) vbits[vwords - 1u] = (1ULL << (n & 63u)) - 1ULL;
                    if (!any_non_empty) {
                        for (uint32_t i = 0; i < n; ++i) {
                            if (acc_vf64[d][i] != 0.0) { any_non_empty = true; break; }
                        }
                    }
                } else {
                    std::memset(vbits, 0, vwords * sizeof(uint64_t));
                    for (uint32_t i = 0; i < n; ++i) {
                        if (acc_vf64_valid[d][i]) {
                            vbits[i >> 6] |= (1ULL << (i & 63u));
                            if (acc_vf64[d][i] != 0.0) any_non_empty = true;
                        }
                    }
                }
                new (ov) V11Vector();
                ov->elem_tag = kVecTagF64; ov->length = n; ov->f64_data = acc_vf64[d]; ov->valid_bits = vbits;
                staged_v[d] = ov;
                staged_v_set[d] = true;
            }
            if (committed_vu64[d] && has_vu64[d]) {
                uint32_t n = acc_vu64_len[d];
                size_t vwords = (n > 0u) ? ((static_cast<size_t>(n) + 63u) >> 6) : 1u;
                uint64_t* vbits = ws.arena.alloc_array<uint64_t>(vwords, 8);
                auto* ov = static_cast<V11Vector*>(ws.arena.alloc(sizeof(V11Vector), alignof(V11Vector)));
                if (!vbits || !ov) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; return; }
                std::memset(vbits, 0, vwords * sizeof(uint64_t));
                for (uint32_t i = 0; i < n; ++i) {
                    if (acc_vu64_valid[d][i]) {
                        vbits[i >> 6] |= (1ULL << (i & 63u));
                        any_non_empty = true;
                    }
                }
                new (ov) V11Vector();
                ov->elem_tag = kVecTagU64; ov->length = n; ov->i64_data = acc_vu64[d]; ov->valid_bits = vbits;
                staged_v[d] = ov;
                staged_v_set[d] = true;
            }
            if (committed_rcnt[d] && acc_rcnt[d] != 0ULL) {
                any_non_empty = true;
            }
        }
        for (uint8_t d = 0; d < 8; ++d) {
            if (staged_f_set[d]) { ws.f_regs[d] = staged_f[d]; ws.set_f_init(d, true); }
            if (staged_v_set[d]) { ws.v_regs[d] = staged_v[d]; }
            if (committed_rcnt[d]) { ws.write_r(d, static_cast<uint8_t>(ScalarType::SCALAR_U64), acc_rcnt[d]); }
        }
        ws.set_zf(!any_non_empty);
        ws.state.pc = end_pc + 1u;
    }
}

} // namespace impulse::vm
