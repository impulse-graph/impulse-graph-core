/**
 * @file v11_p2_helpers.hpp
 * @brief Shared Pillar 2 (0x20..0x27) helpers for ImpulseVM v0.11.0 opcode .inc handlers.
 */
#pragma once

#include "v11_p1_helpers.hpp"
#include <bit>

namespace impulse::vm::v11 {

inline bool p2_set_f1bit(impulse_vm_workspace_v11_t& ws,
                         uint8_t dst,
                         uint16_t dom_id,
                         uint64_t dom_card,
                         const uint32_t* sorted_ids,
                         uint32_t count,
                         bool is_all_span,
                         bool know_card) noexcept {
    if (is_all_span && static_cast<uint64_t>(count) == dom_card && dom_card > 0ULL) {
        ws.f_regs[dst].init_all_span(dom_id, dom_card);
        ws.set_f_init(dst, true);
        return true;
    }
    if (!ws.f_regs[dst].init_from_sorted_ids(ws.arena, dom_id, dom_card, sorted_ids, count, ws.geometry, know_card)) {
        ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW;
        return false;
    }
    ws.set_f_init(dst, true);
    return true;
}

inline bool p2_add_sorted_unique(V11Arena& arena,
                                 const uint32_t* base,
                                 uint32_t base_len,
                                 uint32_t u,
                                 const uint32_t** out_ptr,
                                 uint32_t* out_len) noexcept {
    const uint32_t* it = std::lower_bound(base, base + base_len, u);
    if (it != base + base_len && *it == u) {
        *out_ptr = base;
        *out_len = base_len;
        return true;
    }
    if (base_len == UINT32_MAX) return false;
    uint32_t ins = static_cast<uint32_t>(it - base);
    uint32_t* out = arena.alloc_array<uint32_t>(static_cast<size_t>(base_len) + 1u, 8);
    if (!out) return false;
    if (ins > 0u) std::memcpy(out, base, static_cast<size_t>(ins) * sizeof(uint32_t));
    out[ins] = u;
    if (ins < base_len) {
        std::memcpy(out + ins + 1u, base + ins, static_cast<size_t>(base_len - ins) * sizeof(uint32_t));
    }
    *out_ptr = out;
    *out_len = base_len + 1u;
    return true;
}

inline bool p2_remove_sorted(V11Arena& arena,
                             const uint32_t* base,
                             uint32_t base_len,
                             uint32_t u,
                             const uint32_t** out_ptr,
                             uint32_t* out_len) noexcept {
    const uint32_t* it = std::lower_bound(base, base + base_len, u);
    if (it == base + base_len || *it != u) {
        *out_ptr = base;
        *out_len = base_len;
        return true;
    }
    uint32_t pos = static_cast<uint32_t>(it - base);
    if (base_len <= 1u) {
        *out_ptr = nullptr;
        *out_len = 0u;
        return true;
    }
    uint32_t* out = arena.alloc_array<uint32_t>(base_len - 1u, 8);
    if (!out) return false;
    if (pos > 0u) std::memcpy(out, base, static_cast<size_t>(pos) * sizeof(uint32_t));
    if (pos + 1u < base_len) {
        std::memcpy(out + pos, base + pos + 1u, static_cast<size_t>(base_len - pos - 1u) * sizeof(uint32_t));
    }
    *out_ptr = out;
    *out_len = base_len - 1u;
    return true;
}

inline uint32_t p2_dedupe_sorted_inplace(uint32_t* buf, uint32_t len) noexcept {
    if (len == 0u) return 0u;
    std::sort(buf, buf + len);
    uint32_t uniq = 1u;
    for (uint32_t i = 1; i < len; ++i) {
        if (buf[i] != buf[uniq - 1u]) {
            buf[uniq++] = buf[i];
        }
    }
    return uniq;
}

inline uint64_t p2_popcount64(uint64_t x) noexcept {
    return static_cast<uint64_t>(std::popcount(x));
}

inline bool p2_is_numeric_vec(const V11Vector* v) noexcept {
    return v != nullptr &&
           (v->elem_tag == kVecTagF64 || v->elem_tag == kVecTagI64 || v->elem_tag == kVecTagU64);
}

inline double p2_read_numeric_as_f64(const V11Vector& v, uint32_t idx) noexcept {
    if (v.elem_tag == kVecTagF64) return v.f64_data[idx];
    return static_cast<double>(v.i64_data[idx]);
}

} // namespace impulse::vm::v11
