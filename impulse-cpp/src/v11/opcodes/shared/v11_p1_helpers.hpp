/**
 * @file v11_p1_helpers.hpp
 * @brief Shared Pillar 1 (0x00..0x14) helpers for ImpulseVM v0.11.0 opcode .inc handlers.
 */
#pragma once

#include "../../v11_internal.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace impulse::vm::v11 {

inline bool p1_is_frontier_empty(const impulse_vm_workspace_v11_t& ws, uint8_t reg) noexcept {
    return !ws.is_f_init(reg) || ws.f_regs[reg].is_empty();
}

inline void p1_set_scalar_flags(impulse_vm_workspace_v11_t& ws, bool valid, uint64_t val) noexcept {
    if (!valid) {
        ws.set_zf(true);
        ws.set_nf(true);
    } else {
        ws.set_zf(val == 0ULL);
        ws.set_nf(false);
    }
}

inline void p1_set_f64_flags(impulse_vm_workspace_v11_t& ws, bool valid, double val) noexcept {
    if (!valid) {
        ws.set_zf(true);
        ws.set_nf(true);
    } else {
        ws.set_zf(val == 0.0);
        ws.set_nf(false);
    }
}

inline double p1_bits_to_f64(uint64_t bits) noexcept {
    double f = 0.0;
    std::memcpy(&f, &bits, 8);
    return f;
}

inline uint64_t p1_f64_to_bits(double f) noexcept {
    uint64_t bits = 0;
    std::memcpy(&bits, &f, 8);
    return bits;
}

inline float p1_bits_to_f32(uint32_t bits) noexcept {
    float f = 0.0f;
    std::memcpy(&f, &bits, 4);
    return f;
}

inline double p1_ieee_min_f64(double a, double b) noexcept {
    if (a == 0.0 && b == 0.0) {
        return p1_bits_to_f64(p1_f64_to_bits(a) | p1_f64_to_bits(b));
    }
    return (a < b) ? a : b;
}

inline double p1_ieee_max_f64(double a, double b) noexcept {
    if (a == 0.0 && b == 0.0) {
        return p1_bits_to_f64(p1_f64_to_bits(a) & p1_f64_to_bits(b));
    }
    return (a > b) ? a : b;
}

inline double p1_round_half_away(double x) noexcept {
    if (std::isnan(x) || std::isinf(x) || std::fabs(x) >= 4503599627370496.0) {
        return x;
    }
    double a = std::fabs(x);
    double f = std::floor(a);
    double r = (a - f >= 0.5) ? (f + 1.0) : f;
    return std::copysign(r, x);
}

inline void p1_write_f64_result(impulse_vm_workspace_v11_t& ws, uint8_t dst, bool valid, double f) noexcept {
    if (!valid || std::isnan(f)) {
        ws.write_r_null(dst);
        ws.set_zf(true);
        ws.set_nf(true);
    } else {
        ws.write_r(dst, static_cast<uint8_t>(ScalarType::SCALAR_F64), p1_f64_to_bits(f));
        ws.set_zf(f == 0.0);
        ws.set_nf(false);
    }
}

inline impulse_vm_status_v11_t v11_lookup_domain_card(const V11SnapshotView* snap,
                                                      uint16_t dom_id,
                                                      uint64_t* out_card) noexcept {
    if (!snap) return IMPULSE_VM_V11_ERR_NULL_SNAPSHOT;
    if (!snap->get_domain_card(dom_id, out_card)) return IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS;
    return IMPULSE_VM_V11_OK;
}

IMPULSE_V11_NOINLINE inline void p1_load_frontier_ids(impulse_vm_workspace_v11_t& ws,
                                                      const V11SnapshotView* snap,
                                                      uint8_t dst,
                                                      uint16_t dom_id,
                                                      const uint64_t* ids,
                                                      uint32_t id_count,
                                                      bool accum) noexcept {
    uint64_t dom_card = 0;
    impulse_vm_status_v11_t dst_st = v11_lookup_domain_card(snap, dom_id, &dom_card);
    if (dst_st != IMPULSE_VM_V11_OK) {
        ws.status = dst_st;
        return;
    }

    uint32_t* incoming = nullptr;
    uint32_t in_uniq = 0;
    if (id_count > 0) {
        incoming = ws.arena.alloc_array<uint32_t>(id_count, 8);
        if (!incoming) {
            ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW;
            return;
        }
        for (uint32_t i = 0; i < id_count; ++i) {
            uint64_t u = ids[i];
            if (u >= dom_card || u > UINT32_MAX) {
                ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS;
                return;
            }
            incoming[i] = static_cast<uint32_t>(u);
        }
        std::sort(incoming, incoming + id_count);
        in_uniq = 1u;
        for (uint32_t i = 1; i < id_count; ++i) {
            if (incoming[i] != incoming[in_uniq - 1u]) {
                incoming[in_uniq++] = incoming[i];
            }
        }
    }

    if (accum) {
        if (!ws.is_f_init(dst)) {
            ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH;
            return;
        }
        const V11Frontier& existing = ws.f_regs[dst];
        if (existing.domain_id != dom_id) {
            ws.status = IMPULSE_VM_V11_ERR_DOMAIN_MISMATCH;
            return;
        }
        if (existing.multi_lane) {
            ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH;
            return;
        }
        if (existing.tier == kTierAllSpan) {
            if (dom_card > UINT32_MAX) {
                ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW;
                return;
            }
            uint32_t n_all = static_cast<uint32_t>(dom_card);
            uint32_t* all_ids = (n_all > 0u) ? ws.arena.alloc_array<uint32_t>(n_all, 8) : nullptr;
            if (n_all > 0u && !all_ids) {
                ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW;
                return;
            }
            for (uint32_t idx = 0; idx < n_all; ++idx) {
                all_ids[idx] = idx;
            }
            if (!ws.f_regs[dst].init_from_sorted_ids(ws.arena, dom_id, dom_card, all_ids, n_all, ws.geometry, true)) {
                ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW;
                return;
            }
            ws.set_f_init(dst, true);
            ws.set_zf(n_all == 0u);
            ws.state.pc += 1u;
            return;
        }
        size_t max_merged = static_cast<size_t>(existing.node_count) + static_cast<size_t>(in_uniq);
        if (max_merged > UINT32_MAX) {
            ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW;
            return;
        }
        uint32_t* merged = (max_merged > 0) ? ws.arena.alloc_array<uint32_t>(max_merged, 8) : nullptr;
        if (max_merged > 0 && !merged) {
            ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW;
            return;
        }
        uint32_t i = 0, j = 0, k = 0;
        while (i < existing.node_count && j < in_uniq) {
            if (existing.nodes[i] < incoming[j]) {
                merged[k++] = existing.nodes[i++];
            } else if (incoming[j] < existing.nodes[i]) {
                merged[k++] = incoming[j++];
            } else {
                merged[k++] = existing.nodes[i++];
                ++j;
            }
        }
        while (i < existing.node_count) merged[k++] = existing.nodes[i++];
        while (j < in_uniq) merged[k++] = incoming[j++];

        if (!ws.f_regs[dst].init_from_sorted_ids(ws.arena, dom_id, dom_card, merged, k, ws.geometry, true)) {
            ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW;
            return;
        }
        ws.set_f_init(dst, true);
        ws.set_zf(k == 0u);
        ws.state.pc += 1u;
    } else {
        if (!ws.f_regs[dst].init_from_sorted_ids(ws.arena, dom_id, dom_card, incoming, in_uniq, ws.geometry, true)) {
            ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW;
            return;
        }
        ws.set_f_init(dst, true);
        ws.set_zf(in_uniq == 0u);
        ws.state.pc += 1u;
    }
}

} // namespace impulse::vm::v11
