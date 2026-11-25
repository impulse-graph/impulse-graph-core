/**
 * @file v11_ops_p2.cpp
 * @brief Pillar 2 Evaluator (0x20..0x27) — currently OP_STABLE_CHECK for Pillar 1
 *        compliance; remaining Pillar 2 opcodes implemented in M3.2.
 */
#if defined(__clang__)
  #pragma STDC FP_CONTRACT OFF
#endif

#include "v11_internal.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>

namespace impulse::vm::v11 {
namespace {

inline bool is_numeric_vec(const V11Vector* v) noexcept {
    return v != nullptr &&
           (v->elem_tag == kVecTagF64 || v->elem_tag == kVecTagI64 || v->elem_tag == kVecTagU64);
}

inline double read_numeric_as_f64(const V11Vector& v, uint32_t idx) noexcept {
    if (v.elem_tag == kVecTagF64) return v.f64_data[idx];
    if (v.elem_tag == kVecTagI64) return static_cast<double>(v.i64_data[idx]);
    return static_cast<double>(static_cast<uint64_t>(v.i64_data[idx]));
}

void exec_stable_check(impulse_vm_workspace_v11_t& ws,
                       uint8_t subop, uint8_t dst,
                       uint8_t s1, uint8_t s2, uint8_t s3) noexcept {
    if (subop == 0u) { // .frontier_eq
        if (!ws.is_f_init(s1) || !ws.is_f_init(s2)) {
            ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH;
            return;
        }
        const V11Frontier& fa = ws.f_regs[s1];
        const V11Frontier& fb = ws.f_regs[s2];
        if (fa.domain_id != fb.domain_id) {
            ws.status = IMPULSE_VM_V11_ERR_DOMAIN_MISMATCH;
            return;
        }
        if (fa.multi_lane != fb.multi_lane) {
            ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH;
            return;
        }
        bool eq = false;
        if (!fa.multi_lane) {
            if (fa.tier == kTierAllSpan && fb.tier == kTierAllSpan) {
                eq = (fa.domain_card == fb.domain_card);
            } else if (fa.tier == kTierAllSpan) {
                eq = (static_cast<uint64_t>(fb.node_count) == fa.domain_card);
            } else if (fb.tier == kTierAllSpan) {
                eq = (static_cast<uint64_t>(fa.node_count) == fb.domain_card);
            } else {
                eq = (fa.node_count == fb.node_count) &&
                     (fa.node_count == 0u ||
                      std::memcmp(fa.nodes, fb.nodes, static_cast<size_t>(fa.node_count) * sizeof(uint32_t)) == 0);
            }
        } else {
            if (fa.alloc_width != fb.alloc_width) {
                ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH;
                return;
            }
            eq = (fa.ml_count == fb.ml_count) &&
                 (fa.ml_count == 0u ||
                  (std::memcmp(fa.ml_nodes, fb.ml_nodes, static_cast<size_t>(fa.ml_count) * sizeof(uint64_t)) == 0 &&
                   std::memcmp(fa.ml_masks, fb.ml_masks, static_cast<size_t>(fa.ml_count) * sizeof(uint64_t)) == 0));
        }
        ws.set_st(eq);
        ws.set_zf(eq);
        ws.state.pc += 1u;
    } else if (subop == 1u) { // .vec_l1
        if (!ws.is_r_valid(s3)) {
            ws.status = IMPULSE_VM_V11_ERR_INVALID_OPERAND;
            return;
        }
        const V11Vector* v1 = ws.v_regs[s1];
        const V11Vector* v2 = ws.v_regs[s2];
        if (!v1 || !v2) {
            ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH;
            return;
        }
        double eps = 0.0;
        std::memcpy(&eps, &ws.r_bank()[s3], 8);
        if (std::isnan(eps)) {
            ws.status = IMPULSE_VM_V11_ERR_INVALID_OPERAND;
            return;
        }
        if (!is_numeric_vec(v1) || !is_numeric_vec(v2)) {
            ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH;
            return;
        }
        if (v1->length != v2->length) {
            ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS;
            return;
        }
        double sum = 0.0;
        bool null_mismatch = false;
        for (uint32_t i = 0; i < v1->length; ++i) {
            bool b1 = v1->is_valid_at(i);
            bool b2 = v2->is_valid_at(i);
            if (!b1 && !b2) continue;
            if (b1 != b2) {
                null_mismatch = true;
            } else {
                double x = read_numeric_as_f64(*v1, i);
                double y = read_numeric_as_f64(*v2, i);
                sum += std::fabs(x - y);
            }
        }
        if (std::isnan(sum)) {
            ws.write_r_null(dst);
            ws.set_zf(true);
            ws.set_st(false);
            ws.set_nf(true);
        } else {
            uint64_t bits = 0;
            std::memcpy(&bits, &sum, 8);
            ws.write_r(dst, static_cast<uint8_t>(ScalarType::SCALAR_F64), bits);
            ws.set_zf(sum == 0.0);
            ws.set_st(!null_mismatch && sum <= eps);
            ws.set_nf(false);
        }
        ws.state.pc += 1u;
    } else {
        ws.status = IMPULSE_VM_V11_ERR_INVALID_OPCODE;
    }
}

} // namespace

int step_pillar2(impulse_vm_workspace_v11_t& ws,
                 const V11SnapshotView* /*snap*/,
                 uint64_t w) noexcept {
    uint8_t op    = IMP_V11_OPCODE(w);
    uint8_t subop = IMP_V11_SUBOP(w);
    uint8_t s1    = IMP_V11_S1(w);
    uint8_t dst   = IMP_V11_DST(w);
    uint8_t s3    = IMP_V11_S3(w);
    uint8_t s2    = IMP_V11_S2(w);

    if (static_cast<Opcode>(op) == Opcode::OP_STABLE_CHECK) {
        exec_stable_check(ws, subop, dst, s1, s2, s3);
        return kStepStepped;
    }
    return kStepUnhandled;
}

} // namespace impulse::vm::v11
