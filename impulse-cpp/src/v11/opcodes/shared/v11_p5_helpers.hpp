/**
 * @file v11_p5_helpers.hpp
 * @brief Shared Pillar 5 (0x60..0x6F) zero-malloc helpers for multi-lane bit-sliced
 *        frontiers, vector similarity metrics, and MITra AoS LANE_STATE tensors.
 */
#pragma once

#include "v11_p4_helpers.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace impulse::vm::v11 {

inline uint8_t p5_choose_width(uint32_t k) noexcept {
    if (k <= 8u)  return 8u;
    if (k <= 16u) return 16u;
    if (k <= 32u) return 32u;
    return 64u;
}

inline uint64_t p5_active_lane_mask(uint8_t ac) noexcept {
    if (ac == 0u) return 0ULL;
    if (ac >= 64u) return ~0ULL;
    return (1ULL << ac) - 1ULL;
}

inline const V11Frontier* p5_get_fml(impulse_vm_workspace_v11_t& ws, uint8_t reg) noexcept {
    if (!ws.is_f_init(reg) || !ws.f_regs[reg].multi_lane) {
        ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH;
        return nullptr;
    }
    return &ws.f_regs[reg];
}

struct V11MlPair {
    uint64_t node;
    uint64_t mask;
};

/** Arena-backed canonical sorted (node, nonzero-mask) multi-lane map. */
struct V11MlMap {
    uint64_t* nodes = nullptr;
    uint64_t* masks = nullptr;
    uint32_t  len   = 0;

    inline uint64_t get(uint64_t node) const noexcept {
        if (len == 0u || !nodes) return 0ULL;
        const uint64_t* it = std::lower_bound(nodes, nodes + len, node);
        if (it != nodes + len && *it == node) {
            return masks[static_cast<size_t>(it - nodes)];
        }
        return 0ULL;
    }
};

inline bool p5_ml_from_pairs(V11Arena& arena, V11MlPair* pairs, uint32_t count, V11MlMap* out) noexcept {
    if (count == 0u) {
        out->nodes = nullptr; out->masks = nullptr; out->len = 0u;
        return true;
    }
    std::sort(pairs, pairs + count, [](const V11MlPair& a, const V11MlPair& b) { return a.node < b.node; });
    uint32_t uniq = 0u;
    for (uint32_t i = 0; i < count; ++i) {
        if (pairs[i].mask == 0ULL) continue;
        if (uniq > 0u && pairs[uniq - 1u].node == pairs[i].node) {
            pairs[uniq - 1u].mask |= pairs[i].mask;
        } else {
            pairs[uniq++] = pairs[i];
        }
    }
    if (uniq == 0u) {
        out->nodes = nullptr; out->masks = nullptr; out->len = 0u;
        return true;
    }
    uint64_t* ns = arena.alloc_array<uint64_t>(uniq, 8);
    uint64_t* ms = arena.alloc_array<uint64_t>(uniq, 8);
    if (!ns || !ms) return false;
    for (uint32_t i = 0; i < uniq; ++i) {
        ns[i] = pairs[i].node;
        ms[i] = pairs[i].mask;
    }
    out->nodes = ns;
    out->masks = ms;
    out->len   = uniq;
    return true;
}

inline V11MlMap p5_ml_view(const V11Frontier& f) noexcept {
    V11MlMap m{};
    m.nodes = f.ml_nodes;
    m.masks = f.ml_masks;
    m.len   = f.ml_count;
    return m;
}

inline bool p5_ml_union(V11Arena& arena, const V11MlMap& a, const V11MlMap& b, V11MlMap* out) noexcept {
    size_t cap = static_cast<size_t>(a.len) + static_cast<size_t>(b.len);
    if (cap == 0u) { out->nodes = nullptr; out->masks = nullptr; out->len = 0u; return true; }
    uint64_t* ns = arena.alloc_array<uint64_t>(cap, 8);
    uint64_t* ms = arena.alloc_array<uint64_t>(cap, 8);
    if (!ns || !ms) return false;
    uint32_t i = 0u, j = 0u, k = 0u;
    while (i < a.len && j < b.len) {
        if (a.nodes[i] < b.nodes[j]) {
            if (a.masks[i] != 0ULL) { ns[k] = a.nodes[i]; ms[k] = a.masks[i]; ++k; }
            ++i;
        } else if (b.nodes[j] < a.nodes[i]) {
            if (b.masks[j] != 0ULL) { ns[k] = b.nodes[j]; ms[k] = b.masks[j]; ++k; }
            ++j;
        } else {
            uint64_t r = a.masks[i] | b.masks[j];
            if (r != 0ULL) { ns[k] = a.nodes[i]; ms[k] = r; ++k; }
            ++i; ++j;
        }
    }
    while (i < a.len) { if (a.masks[i] != 0ULL) { ns[k] = a.nodes[i]; ms[k] = a.masks[i]; ++k; } ++i; }
    while (j < b.len) { if (b.masks[j] != 0ULL) { ns[k] = b.nodes[j]; ms[k] = b.masks[j]; ++k; } ++j; }
    out->nodes = ns; out->masks = ms; out->len = k;
    return true;
}

inline bool p5_ml_diff(V11Arena& arena, const V11MlMap& a, const V11MlMap& b, V11MlMap* out) noexcept {
    if (a.len == 0u) { out->nodes = nullptr; out->masks = nullptr; out->len = 0u; return true; }
    uint64_t* ns = arena.alloc_array<uint64_t>(a.len, 8);
    uint64_t* ms = arena.alloc_array<uint64_t>(a.len, 8);
    if (!ns || !ms) return false;
    uint32_t k = 0u;
    for (uint32_t i = 0; i < a.len; ++i) {
        uint64_t r = a.masks[i] & ~b.get(a.nodes[i]);
        if (r != 0ULL) { ns[k] = a.nodes[i]; ms[k] = r; ++k; }
    }
    out->nodes = ns; out->masks = ms; out->len = k;
    return true;
}

IMPULSE_V11_NOINLINE inline bool p5_lane_walk_step_once(
    V11Arena& arena,
    uint8_t subop,
    const V11RelView& rel,
    const V11RevAdj* rev_adj,
    const V11Vector* edge_mask,
    const V11MlMap& cur,
    const V11MlMap& excl,
    V11MlMap* out
) noexcept {
    const bool do_fwd = (subop == 0u || subop == 3u || subop == 4u || subop == 7u || subop == 2u || subop == 8u || subop == 9u);
    const bool do_rev = (subop == 1u || subop == 5u || subop == 6u || subop == 2u || subop == 8u || subop == 9u);

    size_t cap = 0u;
    if (do_fwd) {
        for (uint32_t i = 0; i < cur.len; ++i) {
            uint64_t u64 = cur.nodes[i];
            if (rel.row_off_len > 0u && u64 < rel.row_off_len - 1u) {
                uint32_t u = static_cast<uint32_t>(u64);
                cap += (rel.row_offsets[u + 1u] - rel.row_offsets[u]);
            }
        }
    }
    if (do_rev && rev_adj) {
        for (uint32_t i = 0; i < cur.len; ++i) {
            uint64_t v64 = cur.nodes[i];
            if (v64 < rev_adj->n_tgt) {
                uint32_t v = static_cast<uint32_t>(v64);
                cap += (rev_adj->offsets[v + 1u] - rev_adj->offsets[v]);
            }
        }
    }
    if (cap == 0u) { out->nodes = nullptr; out->masks = nullptr; out->len = 0u; return true; }
    if (cap > UINT32_MAX) return false;
    V11MlPair* pairs = arena.alloc_array<V11MlPair>(cap, 8);
    if (!pairs) return false;
    uint32_t cnt = 0u;

    if (do_fwd) {
        for (uint32_t i = 0; i < cur.len; ++i) {
            uint64_t u64 = cur.nodes[i];
            uint64_t mask = cur.masks[i];
            if (rel.row_off_len > 0u && u64 < rel.row_off_len - 1u) {
                uint32_t u = static_cast<uint32_t>(u64);
                for (uint32_t e = rel.row_offsets[u]; e < rel.row_offsets[u + 1u]; ++e) {
                    uint64_t v = rel.col_targets[e];
                    uint64_t ev = !edge_mask ? ~0ULL
                        : ((e < edge_mask->length && edge_mask->is_valid_at(e)) ? static_cast<uint64_t>(edge_mask->i64_data[e]) : 0ULL);
                    uint64_t pass = mask & ev & ~excl.get(v);
                    if (pass != 0ULL) pairs[cnt++] = {v, pass};
                }
            }
        }
    }
    if (do_rev && rev_adj) {
        for (uint32_t i = 0; i < cur.len; ++i) {
            uint64_t v64 = cur.nodes[i];
            uint64_t mask = cur.masks[i];
            if (v64 < rev_adj->n_tgt) {
                uint32_t v = static_cast<uint32_t>(v64);
                for (uint32_t k = rev_adj->offsets[v]; k < rev_adj->offsets[v + 1u]; ++k) {
                    uint64_t u = static_cast<uint64_t>(rev_adj->srcs[k]);
                    uint32_t e = rev_adj->edges[k];
                    uint64_t ev = !edge_mask ? ~0ULL
                        : ((e < edge_mask->length && edge_mask->is_valid_at(e)) ? static_cast<uint64_t>(edge_mask->i64_data[e]) : 0ULL);
                    uint64_t pass = mask & ev & ~excl.get(u);
                    if (pass != 0ULL) pairs[cnt++] = {u, pass};
                }
            }
        }
    }
    return p5_ml_from_pairs(arena, pairs, cnt, out);
}

inline bool p5_vec_to_f64_dense(V11Arena& arena, const V11Vector* v, double** out_ptr, uint32_t* out_len) noexcept {
    if (!p4_is_numeric_vec(v)) return false;
    uint32_t n = v->length;
    *out_len = n;
    if (n == 0u) { *out_ptr = nullptr; return true; }
    double* out = arena.alloc_array<double>(n, 8);
    if (!out) return false;
    for (uint32_t i = 0; i < n; ++i) {
        out[i] = v->is_valid_at(i) ? p4_vec_to_f64(*v, i) : 0.0;
    }
    *out_ptr = out;
    return true;
}

inline double p5_sim_metric(uint8_t metric, const double* a, uint32_t a_len, const double* b, uint32_t b_len) noexcept {
    uint32_t d = (a_len < b_len) ? a_len : b_len;
    double dot = 0.0, na = 0.0, nb = 0.0, l2 = 0.0;
    for (uint32_t i = 0; i < d; ++i) {
        double x = a[i], y = b[i];
        dot += x * y;
        na  += x * x;
        nb  += y * y;
        double diff = x - y;
        l2  += diff * diff;
    }
    switch (metric) {
        case 0: return dot;
        case 1: {
            double denom = std::sqrt(na) * std::sqrt(nb);
            return (denom == 0.0) ? std::numeric_limits<double>::quiet_NaN() : (dot / denom);
        }
        case 2: return -l2;
        case 3: return l2;
        default: return std::numeric_limits<double>::quiet_NaN();
    }
}

inline V11Vector* p5_alloc_lane_state(V11Arena& arena,
                                      uint16_t dom,
                                      uint32_t n,
                                      uint16_t k,
                                      bool with_witness) noexcept {
    uint64_t total64 = static_cast<uint64_t>(n) * static_cast<uint64_t>(k);
    if (total64 > (std::numeric_limits<size_t>::max)() / sizeof(double)) return nullptr;
    size_t total = static_cast<size_t>(total64);

    auto* v = static_cast<V11Vector*>(arena.alloc(sizeof(V11Vector), alignof(V11Vector)));
    if (!v) return nullptr;
    double*  vals  = (total > 0u) ? arena.alloc_array<double>(total, 8)  : nullptr;
    uint8_t* valid = (total > 0u) ? arena.alloc_array<uint8_t>(total, 8) : nullptr;
    if (total > 0u && (!vals || !valid)) return nullptr;
    if (total > 0u) {
        std::memset(vals, 0, total * sizeof(double));
        std::memset(valid, 0, total);
    }

    int64_t* wit = nullptr;
    uint8_t* wit_valid = nullptr;
    if (with_witness) {
        wit       = (total > 0u) ? arena.alloc_array<int64_t>(total, 8) : nullptr;
        wit_valid = (total > 0u) ? arena.alloc_array<uint8_t>(total, 8) : nullptr;
        if (total > 0u && (!wit || !wit_valid)) return nullptr;
        if (total > 0u) {
            std::memset(wit, 0, total * sizeof(int64_t));
            std::memset(wit_valid, 0, total);
        }
    }

    new (v) V11Vector();
    v->elem_tag = kVecTagLaneState;
    v->length   = n;
    v->ls_dom   = dom;
    v->ls_k     = k;
    v->ls_vals  = vals;
    v->ls_valid = valid;
    v->ls_witness = wit;
    v->ls_witness_valid = wit_valid;
    return v;
}

inline void p5_relax_lane_edge(
    uint32_t u, uint32_t v, uint32_t e, uint16_t k, uint8_t semi,
    const V11Frontier* mask,
    bool scalar_w, bool scalar_w_valid, double scalar_w_val, const V11Vector* weight_col,
    const double* snap_in, const uint8_t* snap_in_valid,
    double* out_vals, uint8_t* out_valid,
    int64_t* out_wit, uint8_t* out_wit_valid,
    uint32_t n_out, uint64_t& hits
) noexcept {
    if (v >= n_out) return;
    if (mask && !mask->contains_1bit(v)) return;
    double w = 1.0;
    if (scalar_w) {
        if (!scalar_w_valid) return;
        w = scalar_w_val;
    } else if (weight_col != nullptr) {
        if (e >= weight_col->length || !weight_col->is_valid_at(e)) return;
        w = p4_vec_to_f64(*weight_col, e);
    }
    size_t u_base = static_cast<size_t>(u) * k;
    size_t v_base = static_cast<size_t>(v) * k;
    for (uint16_t l = 0; l < k; ++l) {
        if (!snap_in_valid[u_base + l]) continue;
        double du = snap_in[u_base + l];
        double cand = 0.0;
        if (semi == 0u || semi == 1u) cand = du + w;
        else if (semi == 2u || semi == 3u) cand = du * w;
        else cand = p1_ieee_max_f64(du, w);
        if (std::isnan(cand)) continue;
        bool updated = false;
        bool has = (out_valid[v_base + l] != 0u);
        if (semi == 3u) {
            out_vals[v_base + l] = has ? (out_vals[v_base + l] + cand) : cand;
            out_valid[v_base + l] = 1u;
            updated = true;
        } else if (semi == 0u || semi == 4u) {
            if (!has || cand < out_vals[v_base + l]) {
                out_vals[v_base + l] = cand;
                out_valid[v_base + l] = 1u;
                updated = true;
            }
        } else {
            if (!has || cand > out_vals[v_base + l]) {
                out_vals[v_base + l] = cand;
                out_valid[v_base + l] = 1u;
                updated = true;
            }
        }
        if (updated) {
            ++hits;
            if (out_wit != nullptr) {
                out_wit[v_base + l] = static_cast<int64_t>(u);
                out_wit_valid[v_base + l] = 1u;
            }
        }
    }
}

} // namespace impulse::vm::v11
