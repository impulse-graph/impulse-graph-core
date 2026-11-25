/**
 * @file v11_ops_p1.cpp
 * @brief Normative Pillar 1 Evaluator (0x00..0x14, 21 opcodes) for ImpulseVM v0.11.0.
 *
 * Strictly 0-ULP IEEE-754 bit-identical to V11_eval_p1 (impulse-ocaml) and
 * V11Pillar1Ops.java (impulse-graph-java). Zero heap allocations.
 */
#if defined(__clang__)
  #pragma STDC FP_CONTRACT OFF
#endif

#include "v11_internal.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace impulse::vm::v11 {
namespace {

inline bool is_frontier_empty(const impulse_vm_workspace_v11_t& ws, uint8_t reg) noexcept {
    return !ws.is_f_init(reg) || ws.f_regs[reg].is_empty();
}

inline void set_scalar_flags(impulse_vm_workspace_v11_t& ws, bool valid, uint64_t val) noexcept {
    if (!valid) {
        ws.set_zf(true);
        ws.set_nf(true);
    } else {
        ws.set_zf(val == 0ULL);
        ws.set_nf(false);
    }
}

inline void set_f64_flags(impulse_vm_workspace_v11_t& ws, bool valid, double val) noexcept {
    if (!valid) {
        ws.set_zf(true);
        ws.set_nf(true);
    } else {
        ws.set_zf(val == 0.0);
        ws.set_nf(false);
    }
}

inline double bits_to_f64(uint64_t bits) noexcept {
    double f = 0.0;
    std::memcpy(&f, &bits, 8);
    return f;
}

inline uint64_t f64_to_bits(double f) noexcept {
    uint64_t bits = 0;
    std::memcpy(&bits, &f, 8);
    return bits;
}

inline float bits_to_f32(uint32_t bits) noexcept {
    float f = 0.0f;
    std::memcpy(&f, &bits, 4);
    return f;
}

inline double ieee_min_f64(double a, double b) noexcept {
    if (a == 0.0 && b == 0.0) {
        return bits_to_f64(f64_to_bits(a) | f64_to_bits(b));
    }
    return (a < b) ? a : b;
}

inline double ieee_max_f64(double a, double b) noexcept {
    if (a == 0.0 && b == 0.0) {
        return bits_to_f64(f64_to_bits(a) & f64_to_bits(b));
    }
    return (a > b) ? a : b;
}

inline double round_half_away(double x) noexcept {
    if (std::isnan(x) || std::isinf(x) || std::fabs(x) >= 4503599627370496.0) {
        return x;
    }
    double a = std::fabs(x);
    double f = std::floor(a);
    double r = (a - f >= 0.5) ? (f + 1.0) : f;
    return std::copysign(r, x);
}

inline void write_f64_result(impulse_vm_workspace_v11_t& ws, uint8_t dst, bool valid, double f) noexcept {
    if (!valid || std::isnan(f)) {
        ws.write_r_null(dst);
        ws.set_zf(true);
        ws.set_nf(true);
    } else {
        ws.write_r(dst, static_cast<uint8_t>(ScalarType::SCALAR_F64), f64_to_bits(f));
        ws.set_zf(f == 0.0);
        ws.set_nf(false);
    }
}

void exec_load_const(impulse_vm_workspace_v11_t& ws,
                     const uint64_t* program,
                     size_t program_len,
                     uint8_t subop,
                     uint8_t dst,
                     uint32_t imm32_u) noexcept {
    int32_t imm32_s = static_cast<int32_t>(imm32_u);
    switch (subop) {
        case 0: { // .i32
            int64_t v = static_cast<int64_t>(imm32_s);
            uint64_t u = static_cast<uint64_t>(v);
            ws.write_r(dst, static_cast<uint8_t>(ScalarType::SCALAR_I64), u);
            set_scalar_flags(ws, true, u);
            ws.state.pc += 1u;
            break;
        }
        case 1: { // .u32
            uint64_t u = static_cast<uint64_t>(imm32_u);
            ws.write_r(dst, static_cast<uint8_t>(ScalarType::SCALAR_U64), u);
            set_scalar_flags(ws, true, u);
            ws.state.pc += 1u;
            break;
        }
        case 2: { // .f32 -> f64
            double f = static_cast<double>(bits_to_f32(imm32_u));
            if (std::isnan(f)) {
                ws.write_r_null(dst);
                set_f64_flags(ws, false, 0.0);
            } else {
                ws.write_r(dst, static_cast<uint8_t>(ScalarType::SCALAR_F64), f64_to_bits(f));
                set_f64_flags(ws, true, f);
            }
            ws.state.pc += 1u;
            break;
        }
        case 3: { // .f32_raw
            float f = bits_to_f32(imm32_u);
            if (std::isnan(f)) {
                ws.write_r_null(dst);
                set_scalar_flags(ws, false, 0ULL);
            } else {
                ws.write_r(dst, static_cast<uint8_t>(ScalarType::SCALAR_F32_RAW), static_cast<uint64_t>(imm32_u));
                ws.set_zf(f == 0.0f);
                ws.set_nf(false);
            }
            ws.state.pc += 1u;
            break;
        }
        case 4: { // .i64.ext
            if (static_cast<size_t>(ws.state.pc) + 1u >= program_len) {
                ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS;
                return;
            }
            uint64_t w2 = program[ws.state.pc + 1u];
            uint64_t v = (static_cast<uint64_t>(IMP_V11_IMM32(w2)) << 32) | static_cast<uint64_t>(imm32_u);
            ws.write_r(dst, static_cast<uint8_t>(ScalarType::SCALAR_I64), v);
            set_scalar_flags(ws, true, v);
            ws.state.pc += 2u;
            break;
        }
        case 5: { // .u64.ext
            if (static_cast<size_t>(ws.state.pc) + 1u >= program_len) {
                ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS;
                return;
            }
            uint64_t w2 = program[ws.state.pc + 1u];
            uint64_t v = (static_cast<uint64_t>(IMP_V11_IMM32(w2)) << 32) | static_cast<uint64_t>(imm32_u);
            ws.write_r(dst, static_cast<uint8_t>(ScalarType::SCALAR_U64), v);
            set_scalar_flags(ws, true, v);
            ws.state.pc += 2u;
            break;
        }
        case 6: { // .f64.ext
            if (static_cast<size_t>(ws.state.pc) + 1u >= program_len) {
                ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS;
                return;
            }
            uint64_t w2 = program[ws.state.pc + 1u];
            uint64_t bits = (static_cast<uint64_t>(IMP_V11_IMM32(w2)) << 32) | static_cast<uint64_t>(imm32_u);
            double f = bits_to_f64(bits);
            if (std::isnan(f)) {
                ws.write_r_null(dst);
                set_f64_flags(ws, false, 0.0);
            } else {
                ws.write_r(dst, static_cast<uint8_t>(ScalarType::SCALAR_F64), bits);
                set_f64_flags(ws, true, f);
            }
            ws.state.pc += 2u;
            break;
        }
        case 7: { // .null
            ws.write_r_null(dst);
            set_scalar_flags(ws, false, 0ULL);
            ws.state.pc += 1u;
            break;
        }
        default:
            ws.status = IMPULSE_VM_V11_ERR_INVALID_OPCODE;
            break;
    }
}

impulse_vm_status_v11_t lookup_domain_card(const V11SnapshotView* snap, uint16_t dom_id, uint64_t* out_card) noexcept {
    if (!snap) return IMPULSE_VM_V11_ERR_NULL_SNAPSHOT;
    if (!snap->get_domain_card(dom_id, out_card)) return IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS;
    return IMPULSE_VM_V11_OK;
}

void load_frontier_ids(impulse_vm_workspace_v11_t& ws,
                       const V11SnapshotView* snap,
                       uint8_t dst,
                       uint16_t dom_id,
                       const uint64_t* ids,
                       uint32_t id_count,
                       bool accum) noexcept {
    uint64_t dom_card = 0;
    impulse_vm_status_v11_t dst_st = lookup_domain_card(snap, dom_id, &dom_card);
    if (dst_st != IMPULSE_VM_V11_OK) {
        ws.status = dst_st;
        return;
    }

    // Validate & sort/dedupe incoming IDs inside arena scratch
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
            ws.set_zf(dom_card == 0);
            ws.state.pc += 1u;
            return;
        }
        uint32_t max_merged = existing.node_count + in_uniq;
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

void exec_load_const_pool(impulse_vm_workspace_v11_t& ws,
                          const V11SnapshotView* snap,
                          uint8_t subop,
                          uint8_t flags,
                          uint8_t dst,
                          uint32_t imm32,
                          uint16_t imm_lo,
                          uint16_t imm_hi) noexcept {
    switch (subop) {
        case 0: { // .vec
            const V11ConstPoolEntry* e = ws.find_const_pool(imm32);
            if (!e) {
                ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS;
            } else if (e->kind == ConstPoolKind::Vec) {
                ws.v_regs[dst] = e->vec;
                ws.state.pc += 1u;
            } else {
                ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH;
            }
            break;
        }
        case 1: { // .frontier
            const V11ConstPoolEntry* e = ws.find_const_pool(imm_lo);
            if (!e) {
                ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS;
            } else if (e->kind == ConstPoolKind::Frontier) {
                load_frontier_ids(ws, snap, dst, imm_hi, e->f_ids, e->f_len, (flags & FLAG_ACCUM) != 0u);
            } else {
                ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH;
            }
            break;
        }
        case 2: { // .str
            const V11ConstPoolEntry* e = ws.find_const_pool(imm32);
            if (!e) {
                ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS;
            } else if (e->kind == ConstPoolKind::Str) {
                uint64_t h = ws.intern_scalar_str(e->str.ptr, e->str.len);
                if (h == 0ULL) {
                    ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW;
                    return;
                }
                ws.write_r(dst, static_cast<uint8_t>(ScalarType::SCALAR_STR_HANDLE), h);
                ws.set_zf(e->str.len == 0u);
                ws.set_nf(false);
                ws.state.pc += 1u;
            } else {
                ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH;
            }
            break;
        }
        default:
            ws.status = IMPULSE_VM_V11_ERR_INVALID_OPCODE;
            break;
    }
}

void exec_load_param(impulse_vm_workspace_v11_t& ws,
                     const V11SnapshotView* snap,
                     uint8_t subop,
                     uint8_t flags,
                     uint8_t dst,
                     uint16_t pid,
                     uint16_t hi) noexcept {
    const V11ParamEntry* e = ws.find_param(pid);
    switch (subop) {
        case 0: { // .scalar
            if (!e) {
                ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS;
            } else if (e->kind == ParamKind::Scalar) {
                bool valid_expected_tag = (hi <= 6u);
                bool exp_ok = valid_expected_tag &&
                    (hi == e->scalar_type ||
                     (!e->scalar_valid && e->scalar_type == static_cast<uint8_t>(ScalarType::SCALAR_NULL_UNSET)));
                if (!exp_ok) {
                    ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH;
                } else {
                    if (e->scalar_type == static_cast<uint8_t>(ScalarType::SCALAR_F64) && e->scalar_valid) {
                        double f = bits_to_f64(e->scalar_bits);
                        if (std::isnan(f)) {
                            ws.write_r_null(dst);
                            set_f64_flags(ws, false, 0.0);
                        } else {
                            ws.write_r(dst, static_cast<uint8_t>(ScalarType::SCALAR_F64), e->scalar_bits);
                            set_f64_flags(ws, true, f);
                        }
                    } else if (!e->scalar_valid) {
                        ws.write_r_null(dst);
                        set_scalar_flags(ws, false, 0ULL);
                    } else {
                        ws.write_r(dst, e->scalar_type, e->scalar_bits);
                        set_scalar_flags(ws, true, e->scalar_bits);
                    }
                    ws.state.pc += 1u;
                }
            } else if (e->kind == ParamKind::Str) {
                if (hi != static_cast<uint16_t>(ScalarType::SCALAR_STR_HANDLE)) {
                    ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH;
                } else {
                    if (!e->str.valid) {
                        ws.write_r_null(dst);
                        set_scalar_flags(ws, false, 0ULL);
                    } else {
                        uint64_t h = ws.intern_scalar_str(e->str.ptr, e->str.len);
                        if (h == 0ULL) {
                            ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW;
                            return;
                        }
                        ws.write_r(dst, static_cast<uint8_t>(ScalarType::SCALAR_STR_HANDLE), h);
                        ws.set_zf(e->str.len == 0u);
                        ws.set_nf(false);
                    }
                    ws.state.pc += 1u;
                }
            } else {
                ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH;
            }
            break;
        }
        case 1: { // .frontier
            if (!e) {
                ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS;
            } else if (e->kind == ParamKind::Frontier) {
                if (e->dom_id != hi) {
                    ws.status = IMPULSE_VM_V11_ERR_DOMAIN_MISMATCH;
                } else {
                    load_frontier_ids(ws, snap, dst, hi, e->f_ids, e->f_len, (flags & FLAG_ACCUM) != 0u);
                }
            } else if (e->kind == ParamKind::MultiLane) {
                if (e->dom_id != hi) {
                    ws.status = IMPULSE_VM_V11_ERR_DOMAIN_MISMATCH;
                } else {
                    uint64_t dom_card = 0;
                    impulse_vm_status_v11_t st = lookup_domain_card(snap, hi, &dom_card);
                    if (st != IMPULSE_VM_V11_OK) {
                        ws.status = st;
                        return;
                    }
                    struct Pair { uint64_t u; uint64_t m; };
                    Pair* pairs = (e->ml_len > 0) ? ws.arena.alloc_array<Pair>(e->ml_len, 8) : nullptr;
                    if (e->ml_len > 0 && !pairs) {
                        ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW;
                        return;
                    }
                    uint32_t kept = 0;
                    for (uint32_t i = 0; i < e->ml_len; ++i) {
                        uint64_t u = e->ml_nodes[i];
                        uint64_t m = e->ml_masks[i];
                        if (u >= dom_card) {
                            ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS;
                            return;
                        }
                        if (m != 0ULL) {
                            pairs[kept++] = {u, m};
                        }
                    }
                    // Stable sort by node ID, last write wins for duplicate keys (matching TreeMap.put)
                    std::stable_sort(pairs, pairs + kept, [](const Pair& a, const Pair& b) {
                        return a.u < b.u;
                    });
                    uint32_t uniq = 0;
                    for (uint32_t i = 0; i < kept; ++i) {
                        if (uniq > 0 && pairs[uniq - 1].u == pairs[i].u) {
                            pairs[uniq - 1].m = pairs[i].m;
                        } else {
                            pairs[uniq++] = pairs[i];
                        }
                    }
                    uint64_t* sn = (uniq > 0) ? ws.arena.alloc_array<uint64_t>(uniq, 8) : nullptr;
                    uint64_t* sm = (uniq > 0) ? ws.arena.alloc_array<uint64_t>(uniq, 8) : nullptr;
                    if (uniq > 0 && (!sn || !sm)) {
                        ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW;
                        return;
                    }
                    for (uint32_t i = 0; i < uniq; ++i) {
                        sn[i] = pairs[i].u;
                        sm[i] = pairs[i].m;
                    }
                    if (!ws.f_regs[dst].init_multi_lane(
                            ws.arena, hi, dom_card, e->ml_width, e->ml_active, sn, sm, uniq, ws.geometry)) {
                        ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW;
                        return;
                    }
                    ws.set_f_init(dst, true);
                    ws.set_zf(uniq == 0u);
                    ws.state.pc += 1u;
                }
            } else {
                ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH;
            }
            break;
        }
        case 2: { // .vec
            if (!e) {
                ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS;
            } else if (e->kind == ParamKind::Vec && e->vec != nullptr) {
                if (!e->vec->matches_expected_param_tag(hi)) {
                    ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH;
                } else {
                    ws.v_regs[dst] = e->vec;
                    ws.state.pc += 1u;
                }
            } else {
                ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH;
            }
            break;
        }
        default:
            ws.status = IMPULSE_VM_V11_ERR_INVALID_OPCODE;
            break;
    }
}

void exec_mov(impulse_vm_workspace_v11_t& ws,
              const V11SnapshotView* snap,
              uint8_t subop,
              uint8_t dst,
              uint8_t s1,
              uint16_t imm_lo) noexcept {
    switch (subop) {
        case 0: { // .r
            if (!ws.is_r_valid(s1)) {
                ws.write_r_null(dst);
                set_scalar_flags(ws, false, 0ULL);
            } else {
                uint8_t ty = ws.read_r_type(s1);
                uint64_t v = ws.r_bank()[s1];
                ws.write_r(dst, ty, v);
                if (ty == static_cast<uint8_t>(ScalarType::SCALAR_F64)) {
                    set_f64_flags(ws, true, bits_to_f64(v));
                } else {
                    set_scalar_flags(ws, true, v);
                }
            }
            ws.state.pc += 1u;
            break;
        }
        case 1: { // .f
            if (!ws.is_f_init(s1)) {
                ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH;
            } else {
                if (dst != s1) {
                    if (!ws.f_regs[dst].copy_from(ws.arena, ws.f_regs[s1])) {
                        ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW;
                        return;
                    }
                    ws.set_f_init(dst, true);
                }
                ws.set_zf(ws.f_regs[dst].is_empty());
                ws.state.pc += 1u;
            }
            break;
        }
        case 2: { // .v
            if (ws.v_regs[s1] == nullptr) {
                ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH;
            } else {
                if (dst != s1) {
                    ws.v_regs[dst] = ws.v_regs[s1];
                }
                ws.state.pc += 1u;
            }
            break;
        }
        case 3: { // .clear_r
            ws.write_r(dst, static_cast<uint8_t>(ScalarType::SCALAR_I64), 0ULL);
            set_scalar_flags(ws, true, 0ULL);
            ws.state.pc += 1u;
            break;
        }
        case 4: { // .clear_f
            uint64_t dom_card = 0;
            impulse_vm_status_v11_t st = lookup_domain_card(snap, imm_lo, &dom_card);
            if (st != IMPULSE_VM_V11_OK) {
                ws.status = st;
            } else {
                ws.f_regs[dst].reset_empty(imm_lo, dom_card);
                ws.set_f_init(dst, true);
                ws.set_zf(true);
                ws.state.pc += 1u;
            }
            break;
        }
        case 5: { // .clear_v
            ws.v_regs[dst] = nullptr;
            ws.state.pc += 1u;
            break;
        }
        default:
            ws.status = IMPULSE_VM_V11_ERR_INVALID_OPCODE;
            break;
    }
}

void exec_swap(impulse_vm_workspace_v11_t& ws, uint8_t subop, uint8_t dst, uint8_t s1) noexcept {
    switch (subop) {
        case 0: { // .r
            bool vd = ws.is_r_valid(dst);
            uint64_t val_d = ws.r_bank()[dst];
            uint8_t ty_d = ws.read_r_type(dst);
            bool vs = ws.is_r_valid(s1);
            uint64_t val_s = ws.r_bank()[s1];
            uint8_t ty_s = ws.read_r_type(s1);
            if (vs) ws.write_r(dst, ty_s, val_s); else ws.write_r_null(dst);
            if (vd) ws.write_r(s1, ty_d, val_d); else ws.write_r_null(s1);
            ws.state.pc += 1u;
            break;
        }
        case 1: { // .f
            bool init_d = ws.is_f_init(dst);
            bool init_s = ws.is_f_init(s1);
            V11Frontier tmp = ws.f_regs[dst];
            ws.f_regs[dst] = ws.f_regs[s1];
            ws.f_regs[s1] = tmp;
            ws.set_f_init(dst, init_s);
            ws.set_f_init(s1, init_d);
            ws.state.pc += 1u;
            break;
        }
        case 2: { // .v
            V11Vector* tmp = ws.v_regs[dst];
            ws.v_regs[dst] = ws.v_regs[s1];
            ws.v_regs[s1] = tmp;
            ws.state.pc += 1u;
            break;
        }
        default:
            ws.status = IMPULSE_VM_V11_ERR_INVALID_OPCODE;
            break;
    }
}

void exec_spill(impulse_vm_workspace_v11_t& ws, uint8_t subop, uint8_t s1, uint16_t slot) noexcept {
    if (slot >= kSpillPerFrame) {
        ws.status = IMPULSE_VM_V11_ERR_STACK_OVERFLOW;
        return;
    }
    uint32_t idx = static_cast<uint32_t>(ws.state.call_depth) * kSpillPerFrame + slot;
    V11SpillSlot& sp = ws.spill_slots[idx];
    switch (subop) {
        case 0:
            sp.kind = kSpillR;
            sp.r_val = ws.r_bank()[s1];
            sp.r_valid = ws.is_r_valid(s1);
            sp.r_type = ws.read_r_type(s1);
            ws.state.pc += 1u;
            break;
        case 1:
            if (!ws.is_f_init(s1)) {
                ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH;
            } else {
                sp.kind = kSpillF;
                if (!sp.f_val.copy_from(ws.arena, ws.f_regs[s1])) {
                    ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW;
                    return;
                }
                ws.state.pc += 1u;
            }
            break;
        case 2:
            if (ws.v_regs[s1] == nullptr) {
                ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH;
            } else {
                sp.kind = kSpillV;
                sp.v_val = ws.v_regs[s1];
                ws.state.pc += 1u;
            }
            break;
        default:
            ws.status = IMPULSE_VM_V11_ERR_INVALID_OPCODE;
            break;
    }
}

void exec_reload(impulse_vm_workspace_v11_t& ws, uint8_t subop, uint8_t dst, uint16_t slot) noexcept {
    if (slot >= kSpillPerFrame) {
        ws.status = IMPULSE_VM_V11_ERR_STACK_OVERFLOW;
        return;
    }
    uint32_t idx = static_cast<uint32_t>(ws.state.call_depth) * kSpillPerFrame + slot;
    const V11SpillSlot& sp = ws.spill_slots[idx];
    if (subop == 0 && sp.kind == kSpillR) {
        if (!sp.r_valid) {
            ws.write_r_null(dst);
            set_scalar_flags(ws, false, 0ULL);
        } else {
            ws.write_r(dst, sp.r_type, sp.r_val);
            if (sp.r_type == static_cast<uint8_t>(ScalarType::SCALAR_F64)) {
                set_f64_flags(ws, true, bits_to_f64(sp.r_val));
            } else {
                set_scalar_flags(ws, true, sp.r_val);
            }
        }
        ws.state.pc += 1u;
    } else if (subop == 1 && sp.kind == kSpillF) {
        if (!ws.f_regs[dst].copy_from(ws.arena, sp.f_val)) {
            ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW;
            return;
        }
        ws.set_f_init(dst, true);
        ws.set_zf(ws.f_regs[dst].is_empty());
        ws.state.pc += 1u;
    } else if (subop == 2 && sp.kind == kSpillV) {
        ws.v_regs[dst] = sp.v_val;
        ws.state.pc += 1u;
    } else {
        ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH;
    }
}

void exec_scalar_int_alu(impulse_vm_workspace_v11_t& ws,
                         uint8_t subop, uint8_t dst,
                         uint8_t s1, uint8_t s2, uint8_t s3,
                         int32_t imm32_s) noexcept {
    bool a_valid = ws.is_r_valid(s1);
    uint64_t a_u = ws.r_bank()[s1];
    int64_t a_s = static_cast<int64_t>(a_u);

    if (subop == 10u) { // .not (unary)
        if (!a_valid) {
            ws.write_r_null(dst);
            set_scalar_flags(ws, false, 0ULL);
        } else {
            uint64_t r = ~a_u;
            ws.write_r(dst, static_cast<uint8_t>(ScalarType::SCALAR_I64), r);
            set_scalar_flags(ws, true, r);
        }
        ws.state.pc += 1u;
        return;
    }

    bool b_valid = (s3 == 1u) || ws.is_r_valid(s2);
    int64_t b_s = (s3 == 1u) ? static_cast<int64_t>(imm32_s) : static_cast<int64_t>(ws.r_bank()[s2]);
    uint64_t b_u = static_cast<uint64_t>(b_s);

    if (!a_valid || !b_valid) {
        ws.write_r_null(dst);
        set_scalar_flags(ws, false, 0ULL);
        ws.state.pc += 1u;
        return;
    }

    uint8_t out_ty = (subop == 4u || subop == 6u || subop == 12u)
        ? static_cast<uint8_t>(ScalarType::SCALAR_U64)
        : static_cast<uint8_t>(ScalarType::SCALAR_I64);
    bool res_valid = true;
    uint64_t r_u = 0ULL;

    switch (subop) {
        case 0: r_u = a_u + b_u; break;
        case 1: r_u = a_u - b_u; break;
        case 2: r_u = a_u * b_u; break;
        case 3:
            if (b_s == 0) {
                res_valid = false;
            } else if (a_s == (std::numeric_limits<int64_t>::min)() && b_s == -1) {
                r_u = static_cast<uint64_t>((std::numeric_limits<int64_t>::min)());
            } else {
                r_u = static_cast<uint64_t>(a_s / b_s);
            }
            break;
        case 4:
            if (b_u == 0ULL) res_valid = false;
            else r_u = a_u / b_u;
            break;
        case 5:
            if (b_s == 0) {
                res_valid = false;
            } else if (a_s == (std::numeric_limits<int64_t>::min)() && b_s == -1) {
                r_u = 0ULL;
            } else {
                r_u = static_cast<uint64_t>(a_s % b_s);
            }
            break;
        case 6:
            if (b_u == 0ULL) res_valid = false;
            else r_u = a_u % b_u;
            break;
        case 7:  r_u = a_u & b_u; break;
        case 8:  r_u = a_u | b_u; break;
        case 9:  r_u = a_u ^ b_u; break;
        case 11: r_u = a_u << (b_u & 63ULL); break;
        case 12: r_u = a_u >> (b_u & 63ULL); break;
        case 13: {
            unsigned sh = static_cast<unsigned>(b_u & 63ULL);
            r_u = static_cast<uint64_t>(a_s >> sh);
            break;
        }
        case 14: r_u = static_cast<uint64_t>((a_s < b_s) ? a_s : b_s); break;
        case 15: r_u = static_cast<uint64_t>((a_s > b_s) ? a_s : b_s); break;
        default: res_valid = false; break;
    }

    if (!res_valid) {
        ws.write_r_null(dst);
        set_scalar_flags(ws, false, 0ULL);
    } else {
        ws.write_r(dst, out_ty, r_u);
        set_scalar_flags(ws, true, r_u);
    }
    ws.state.pc += 1u;
}

void exec_scalar_fp_alu(impulse_vm_workspace_v11_t& ws,
                        uint8_t subop, uint8_t dst,
                        uint8_t s1, uint8_t s2, uint8_t s3) noexcept {
    bool a_raw_valid = ws.is_r_valid(s1);
    double a = a_raw_valid ? bits_to_f64(ws.r_bank()[s1]) : std::numeric_limits<double>::quiet_NaN();
    bool a_valid = a_raw_valid && !std::isnan(a);

    bool b_raw_valid = ws.is_r_valid(s2);
    double b = b_raw_valid ? bits_to_f64(ws.r_bank()[s2]) : std::numeric_limits<double>::quiet_NaN();
    bool b_valid = b_raw_valid && !std::isnan(b);

    bool c_raw_valid = ws.is_r_valid(s3);
    double c = c_raw_valid ? bits_to_f64(ws.r_bank()[s3]) : std::numeric_limits<double>::quiet_NaN();
    bool c_valid = c_raw_valid && !std::isnan(c);

    switch (subop) {
        case 0: write_f64_result(ws, dst, a_valid && b_valid, a + b); break;
        case 1: write_f64_result(ws, dst, a_valid && b_valid, a - b); break;
        case 2: write_f64_result(ws, dst, a_valid && b_valid, a * b); break;
        case 3:
            if (!a_valid || !b_valid || b == 0.0) write_f64_result(ws, dst, false, 0.0);
            else write_f64_result(ws, dst, true, a / b);
            break;
        case 4: write_f64_result(ws, dst, a_valid, -a); break;
        case 5: write_f64_result(ws, dst, a_valid, std::fabs(a)); break;
        case 6:
            if (!a_valid || a < 0.0) write_f64_result(ws, dst, false, 0.0);
            else write_f64_result(ws, dst, true, std::sqrt(a));
            break;
        case 7:  write_f64_result(ws, dst, a_valid && b_valid, ieee_min_f64(a, b)); break;
        case 8:  write_f64_result(ws, dst, a_valid && b_valid, ieee_max_f64(a, b)); break;
        case 9:  write_f64_result(ws, dst, a_valid && b_valid && c_valid, std::fma(a, b, c)); break;
        case 10: write_f64_result(ws, dst, a_valid, std::floor(a)); break;
        case 11: write_f64_result(ws, dst, a_valid, std::ceil(a)); break;
        case 12: write_f64_result(ws, dst, a_valid, round_half_away(a)); break;
        case 13: // i64_to_f64
            if (!a_raw_valid) write_f64_result(ws, dst, false, 0.0);
            else write_f64_result(ws, dst, true, static_cast<double>(static_cast<int64_t>(ws.r_bank()[s1])));
            break;
        case 14: // f64_to_i64 saturating trunc
            if (!a_valid) {
                ws.write_r_null(dst);
                ws.set_zf(true);
                ws.set_nf(true);
            } else {
                int64_t i = 0;
                if (a >= 9223372036854775808.0) {
                    i = (std::numeric_limits<int64_t>::max)();
                } else if (a <= -9223372036854775808.0) {
                    i = (std::numeric_limits<int64_t>::min)();
                } else {
                    i = static_cast<int64_t>(a < 0.0 ? std::ceil(a) : std::floor(a));
                }
                ws.write_r(dst, static_cast<uint8_t>(ScalarType::SCALAR_I64), static_cast<uint64_t>(i));
                ws.set_zf(i == 0);
                ws.set_nf(false);
            }
            break;
        case 15: // safe_div: (b == 0.0 || !b_valid) ? c : a / b
            if (!b_valid || b == 0.0) {
                write_f64_result(ws, dst, c_valid, c);
            } else if (!a_valid) {
                write_f64_result(ws, dst, false, 0.0);
            } else {
                write_f64_result(ws, dst, true, a / b);
            }
            break;
        default:
            write_f64_result(ws, dst, false, 0.0);
            break;
    }
    ws.state.pc += 1u;
}

void exec_scalar_3vl(impulse_vm_workspace_v11_t& ws,
                     uint8_t subop, uint8_t dst, uint8_t s1, uint8_t s2) noexcept {
    bool a_valid = ws.is_r_valid(s1);
    uint64_t a_u = ws.r_bank()[s1];
    int64_t a_s  = static_cast<int64_t>(a_u);
    bool b_valid = ws.is_r_valid(s2);
    uint64_t b_u = ws.r_bank()[s2];
    int64_t b_s  = static_cast<int64_t>(b_u);

    if (subop == 15u) { // .coalesce
        if (a_valid) {
            uint8_t ty = ws.read_r_type(s1);
            ws.write_r(dst, ty, a_u);
            if (ty == static_cast<uint8_t>(ScalarType::SCALAR_F64)) set_f64_flags(ws, true, bits_to_f64(a_u));
            else set_scalar_flags(ws, true, a_u);
        } else if (b_valid) {
            uint8_t ty = ws.read_r_type(s2);
            ws.write_r(dst, ty, b_u);
            if (ty == static_cast<uint8_t>(ScalarType::SCALAR_F64)) set_f64_flags(ws, true, bits_to_f64(b_u));
            else set_scalar_flags(ws, true, b_u);
        } else {
            ws.write_r_null(dst);
            set_scalar_flags(ws, false, 0ULL);
        }
        ws.state.pc += 1u;
        return;
    }

    // 0 = NULL, 1 = FALSE, 2 = TRUE
    int k_res = 0;
    switch (subop) {
        case 0: k_res = (a_valid && b_valid) ? (a_u == b_u ? 2 : 1) : 0; break;
        case 1: k_res = (a_valid && b_valid) ? (a_u != b_u ? 2 : 1) : 0; break;
        case 2: k_res = (a_valid && b_valid) ? (a_s <  b_s ? 2 : 1) : 0; break;
        case 3: k_res = (a_valid && b_valid) ? (a_s <= b_s ? 2 : 1) : 0; break;
        case 4: k_res = (a_valid && b_valid) ? (a_u <  b_u ? 2 : 1) : 0; break;
        case 5: k_res = (a_valid && b_valid) ? (a_u <= b_u ? 2 : 1) : 0; break;
        case 6:
        case 7:
        case 8:
        case 9:
            if (!a_valid || !b_valid) {
                k_res = 0;
            } else {
                double af = bits_to_f64(a_u);
                double bf = bits_to_f64(b_u);
                if (std::isnan(af) || std::isnan(bf)) {
                    k_res = 0;
                } else {
                    bool p = false;
                    if (subop == 6)      p = (af == bf);
                    else if (subop == 7) p = (af != bf);
                    else if (subop == 8) p = (af <  bf);
                    else                 p = (af <= bf);
                    k_res = p ? 2 : 1;
                }
            }
            break;
        case 10: { // Kleene AND
            bool a_false = a_valid && (a_u == 0ULL);
            bool b_false = b_valid && (b_u == 0ULL);
            if (a_false || b_false) k_res = 1;
            else if (a_valid && b_valid) k_res = 2;
            else k_res = 0;
            break;
        }
        case 11: { // Kleene OR
            bool a_true = a_valid && (a_u != 0ULL);
            bool b_true = b_valid && (b_u != 0ULL);
            if (a_true || b_true) k_res = 2;
            else if (a_valid && b_valid) k_res = 1;
            else k_res = 0;
            break;
        }
        case 12: // Kleene NOT
            if (!a_valid) k_res = 0;
            else k_res = (a_u == 0ULL) ? 2 : 1;
            break;
        case 13: k_res = !a_valid ? 2 : 1; break;
        case 14: k_res =  a_valid ? 2 : 1; break;
        default: k_res = 0; break;
    }

    if (k_res == 0) {
        ws.write_r_null(dst);
        set_scalar_flags(ws, false, 0ULL);
    } else {
        uint64_t out = (k_res == 2) ? 1ULL : 0ULL;
        ws.write_r(dst, static_cast<uint8_t>(ScalarType::SCALAR_BOOL_3VL), out);
        set_scalar_flags(ws, true, out);
    }
    ws.state.pc += 1u;
}

void exec_call(impulse_vm_workspace_v11_t& ws,
               uint8_t subop, uint8_t s1, uint8_t s2, uint8_t s3,
               int32_t imm32_s) noexcept {
    if (ws.state.call_depth >= kMaxCallDepth) {
        ws.status = IMPULSE_VM_V11_ERR_STACK_OVERFLOW;
        return;
    }
    uint8_t rs = (subop == 1u) ? 16u : s1;
    uint8_t fs = (subop == 1u) ? 16u : s2;
    uint8_t vs = (subop == 1u) ? 16u : s3;
    uint16_t d = ws.state.call_depth;
    V11CallFrame& frame = ws.call_frames[d];

    frame.return_pc = ws.state.pc + 1u;
    frame.r_stride  = rs;
    frame.f_stride  = fs;
    frame.v_stride  = vs;

    std::memcpy(frame.saved_r_val, ws.r_bank(), static_cast<size_t>(rs) * sizeof(uint64_t));
    frame.saved_r_valid = ws.state.r_valid_mask;
    frame.saved_r_type  = ws.state.r_type_word;
    frame.saved_f_valid = ws.f_valid_mask;
    for (uint8_t i = 0; i < fs; ++i) {
        if (ws.is_f_init(i)) {
            if (!frame.saved_f[i].copy_from(ws.arena, ws.f_regs[i])) {
                ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW;
                return;
            }
        } else {
            frame.saved_f[i].reset_empty(0u, 0u);
        }
    }
    for (uint8_t i = 0; i < vs; ++i) {
        frame.saved_v[i] = ws.v_regs[i];
    }

    // Shift caller [stride..15] -> callee [0..15-stride], reset [16-stride..15]
    for (uint8_t i = 0; i < 16u; ++i) {
        if (static_cast<uint32_t>(i) + rs < 16u) {
            uint8_t src = static_cast<uint8_t>(i + rs);
            if (ws.is_r_valid(src)) {
                ws.write_r(i, ws.read_r_type(src), ws.r_bank()[src]);
            } else {
                ws.write_r_null(i);
            }
        } else {
            ws.write_r_null(i);
        }

        if (static_cast<uint32_t>(i) + fs < 16u) {
            uint8_t src = static_cast<uint8_t>(i + fs);
            if (ws.is_f_init(src)) {
                if (!ws.f_regs[i].copy_from(ws.arena, ws.f_regs[src])) {
                    ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW;
                    return;
                }
                ws.set_f_init(i, true);
            } else {
                ws.set_f_init(i, false);
            }
        } else {
            ws.set_f_init(i, false);
        }

        ws.v_regs[i] = (static_cast<uint32_t>(i) + vs < 16u) ? ws.v_regs[i + vs] : nullptr;
    }

    ws.state.call_depth = static_cast<uint16_t>(d + 1u);
    uint32_t spill_base = static_cast<uint32_t>(ws.state.call_depth) * kSpillPerFrame;
    for (uint32_t k = 0; k < kSpillPerFrame; ++k) {
        ws.spill_slots[spill_base + k].kind = kSpillEmpty;
    }
    ws.state.pc = static_cast<uint32_t>(static_cast<int64_t>(ws.state.pc) + imm32_s);
}

void exec_ret(impulse_vm_workspace_v11_t& ws) noexcept {
    if (ws.state.call_depth == 0u) {
        ws.status = IMPULSE_VM_V11_ERR_STACK_UNDERFLOW;
        return;
    }
    uint32_t callee_spill_base = static_cast<uint32_t>(ws.state.call_depth) * kSpillPerFrame;
    for (uint32_t k = 0; k < kSpillPerFrame; ++k) {
        ws.spill_slots[callee_spill_base + k].kind = kSpillEmpty;
    }

    uint16_t d = --ws.state.call_depth;
    const V11CallFrame& frame = ws.call_frames[d];
    uint8_t rs = frame.r_stride;
    uint8_t fs = frame.f_stride;
    uint8_t vs = frame.v_stride;
    uint16_t saved_rvm = frame.saved_r_valid;
    uint64_t saved_rtw = frame.saved_r_type;
    uint16_t saved_fvm = frame.saved_f_valid;

    for (int ii = 15; ii >= 0; --ii) {
        uint8_t i = static_cast<uint8_t>(ii);
        if (i >= rs) {
            uint8_t src = static_cast<uint8_t>(i - rs);
            if (ws.is_r_valid(src)) {
                ws.write_r(i, ws.read_r_type(src), ws.r_bank()[src]);
            } else {
                ws.write_r_null(i);
            }
        } else {
            if (((saved_rvm >> i) & 1u) != 0u) {
                uint8_t ty = static_cast<uint8_t>((saved_rtw >> (i * 4u)) & 0xFULL);
                ws.write_r(i, ty, frame.saved_r_val[i]);
            } else {
                ws.write_r_null(i);
            }
        }

        if (i >= fs) {
            uint8_t src = static_cast<uint8_t>(i - fs);
            if (ws.is_f_init(src)) {
                if (!ws.f_regs[i].copy_from(ws.arena, ws.f_regs[src])) {
                    ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW;
                    return;
                }
                ws.set_f_init(i, true);
            } else {
                ws.set_f_init(i, false);
            }
        } else {
            if (((saved_fvm >> i) & 1u) != 0u) {
                if (!ws.f_regs[i].copy_from(ws.arena, frame.saved_f[i])) {
                    ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW;
                    return;
                }
                ws.set_f_init(i, true);
            } else {
                ws.set_f_init(i, false);
            }
        }

        ws.v_regs[i] = (i >= vs) ? ws.v_regs[i - vs] : frame.saved_v[i];
    }

    ws.state.pc = frame.return_pc;
}

void exec_set_max_dop(impulse_vm_workspace_v11_t& ws, uint8_t subop, uint8_t s1, uint16_t imm_lo) noexcept {
    if (subop == 0u) {
        uint16_t d = imm_lo;
        if (d == 0u) d = 1u;
        else if (d > ws.host_max_threads) d = ws.host_max_threads;
        ws.state.max_dop = d;
        ws.state.pc += 1u;
    } else if (subop == 1u) {
        if (!ws.is_r_valid(s1)) {
            ws.status = IMPULSE_VM_V11_ERR_INVALID_OPERAND;
        } else {
            int64_t d64 = static_cast<int64_t>(ws.r_bank()[s1]);
            uint16_t d = 1u;
            if (d64 <= 0) d = 1u;
            else if (d64 >= static_cast<int64_t>(ws.host_max_threads)) d = ws.host_max_threads;
            else d = static_cast<uint16_t>(d64);
            ws.state.max_dop = d;
            ws.state.pc += 1u;
        }
    } else {
        ws.status = IMPULSE_VM_V11_ERR_INVALID_OPCODE;
    }
}

} // namespace

int step_pillar1(impulse_vm_workspace_v11_t& ws,
                 const V11SnapshotView* snap,
                 const uint64_t* program,
                 size_t program_len,
                 uint64_t w) noexcept {
    uint8_t op      = IMP_V11_OPCODE(w);
    uint8_t subop   = IMP_V11_SUBOP(w);
    uint8_t flags   = IMP_V11_FLAGS(w);
    uint8_t s1      = IMP_V11_S1(w);
    uint8_t dst     = IMP_V11_DST(w);
    uint8_t s3      = IMP_V11_S3(w);
    uint8_t s2      = IMP_V11_S2(w);
    uint32_t imm32  = IMP_V11_IMM32(w);
    int32_t imm32_s = IMP_V11_IMM32_S(w);
    uint16_t imm_lo = IMP_V11_IMM16_LO(w);
    uint16_t imm_hi = IMP_V11_IMM16_HI(w);

    switch (static_cast<Opcode>(op)) {
        case Opcode::OP_HALT:
            return kStepHalted;
        case Opcode::OP_NOP:
            ws.state.pc += 1u;
            return kStepStepped;
        case Opcode::OP_TRAP:
            ws.user_throw_code = imm32;
            ws.status = IMPULSE_VM_V11_ERR_TRAP;
            return kStepStepped;
        case Opcode::OP_THROW:
            ws.user_throw_code = imm32;
            if (subop == 0u) {
                ws.has_user_throw_payload = false;
                ws.user_throw_payload = 0ULL;
            } else if (subop == 1u) {
                if (ws.is_r_valid(s1)) {
                    ws.has_user_throw_payload = true;
                    ws.user_throw_payload = ws.r_bank()[s1];
                } else {
                    ws.has_user_throw_payload = false;
                    ws.user_throw_payload = 0ULL;
                }
            }
            ws.status = IMPULSE_VM_V11_ERR_USER_THROW;
            return kStepStepped;
        case Opcode::OP_ASSERT: {
            bool ok = false;
            switch (subop) {
                case 0: ok = ws.is_r_valid(s1) && ws.r_bank()[s1] != 0ULL; break;
                case 1: ok = !is_frontier_empty(ws, s1); break;
                case 2: ok = is_frontier_empty(ws, s1); break;
                case 3: ok = ws.flag_zf(); break;
                case 4: ok = !ws.flag_zf(); break;
                case 5: ok = ws.flag_st(); break;
                case 6: ok = ws.is_r_valid(s1); break;
                case 7: ok = !ws.is_r_valid(s1); break;
                default: ok = false; break;
            }
            if (ok) ws.state.pc += 1u;
            else ws.status = IMPULSE_VM_V11_ERR_ASSERTION_FAILED;
            return kStepStepped;
        }
        case Opcode::OP_LOAD_CONST:
            exec_load_const(ws, program, program_len, subop, dst, imm32);
            return kStepStepped;
        case Opcode::OP_LOAD_CONST_POOL:
            exec_load_const_pool(ws, snap, subop, flags, dst, imm32, imm_lo, imm_hi);
            return kStepStepped;
        case Opcode::OP_LOAD_PARAM:
            exec_load_param(ws, snap, subop, flags, dst, imm_lo, imm_hi);
            return kStepStepped;
        case Opcode::OP_MOV:
            exec_mov(ws, snap, subop, dst, s1, imm_lo);
            return kStepStepped;
        case Opcode::OP_SWAP:
            exec_swap(ws, subop, dst, s1);
            return kStepStepped;
        case Opcode::OP_SPILL:
            exec_spill(ws, subop, s1, imm_lo);
            return kStepStepped;
        case Opcode::OP_RELOAD:
            exec_reload(ws, subop, dst, imm_lo);
            return kStepStepped;
        case Opcode::OP_SCALAR_INT_ALU:
            exec_scalar_int_alu(ws, subop, dst, s1, s2, s3, imm32_s);
            return kStepStepped;
        case Opcode::OP_SCALAR_FP_ALU:
            exec_scalar_fp_alu(ws, subop, dst, s1, s2, s3);
            return kStepStepped;
        case Opcode::OP_SCALAR_3VL:
            exec_scalar_3vl(ws, subop, dst, s1, s2);
            return kStepStepped;
        case Opcode::OP_JMP:
            ws.state.pc = static_cast<uint32_t>(static_cast<int64_t>(ws.state.pc) + imm32_s);
            return kStepStepped;
        case Opcode::OP_JCC: {
            bool take = false;
            switch (subop) {
                case 0: take = ws.flag_zf(); break;
                case 1: take = !ws.flag_zf(); break;
                case 2: take = ws.flag_st(); break;
                case 3: take = !ws.flag_st(); break;
                case 4: take = ws.flag_nf(); break;
                case 5: take = !ws.flag_nf(); break;
                case 6: take = ws.is_r_valid(s1) && ws.r_bank()[s1] != 0ULL; break;
                case 7: take = !ws.is_r_valid(s1) || ws.r_bank()[s1] == 0ULL; break;
                case 8: take = is_frontier_empty(ws, s1); break;
                case 9: take = !is_frontier_empty(ws, s1); break;
                default: take = false; break;
            }
            ws.state.pc = take
                ? static_cast<uint32_t>(static_cast<int64_t>(ws.state.pc) + imm32_s)
                : (ws.state.pc + 1u);
            return kStepStepped;
        }
        case Opcode::OP_LOOP_DECR:
            if (!ws.is_r_valid(dst)) {
                ws.status = IMPULSE_VM_V11_ERR_INVALID_OPERAND;
            } else {
                uint64_t next_u = ws.r_bank()[dst] - 1ULL;
                int64_t next_s = static_cast<int64_t>(next_u);
                uint8_t ty = ws.read_r_type(dst);
                ws.write_r(dst, ty, next_u);
                ws.set_zf(next_s == 0);
                ws.state.pc = (next_s > 0)
                    ? static_cast<uint32_t>(static_cast<int64_t>(ws.state.pc) + imm32_s)
                    : (ws.state.pc + 1u);
            }
            return kStepStepped;
        case Opcode::OP_CALL:
            exec_call(ws, subop, s1, s2, s3, imm32_s);
            return kStepStepped;
        case Opcode::OP_RET:
            exec_ret(ws);
            return kStepStepped;
        case Opcode::OP_SET_MAX_DOP:
            exec_set_max_dop(ws, subop, s1, imm_lo);
            return kStepStepped;
        default:
            return kStepUnhandled;
    }
}

} // namespace impulse::vm::v11
