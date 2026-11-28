// Opcode 0x24: OP_SET_CARDINALITY
VM_OP_CASE(SET_CARDINALITY) {
    const uint8_t subop = IMP_SUBOP(w);
    const uint8_t dst = IMP_DST(w);
    const uint8_t s1 = IMP_S1(w);
    const uint16_t imm_lo = IMP_IMM16_LO(w);

    if (!ws.is_f_init(s1)) { ws.status = IMPULSE_VM_ERR_TYPE_MISMATCH; VM_DISPATCH(); }
    VmFrontier& fa = ws.f_regs[s1];
    if (subop == 0u) {
        if (fa.multi_lane) { ws.status = IMPULSE_VM_ERR_TYPE_MISMATCH; VM_DISPATCH(); }
        uint64_t c = fa.cardinality();
        ws.write_r(dst, static_cast<uint8_t>(ScalarType::SCALAR_U64), c);
        p1_set_scalar_flags(ws, true, c);
        ws.state.pc += 1u;
    } else if (subop >= 1u && subop <= 4u) {
        if (!fa.multi_lane) { ws.status = IMPULSE_VM_ERR_TYPE_MISMATCH; VM_DISPATCH(); }
        uint64_t c = 0ULL;
        if (subop == 1u) {
            for (uint32_t i = 0; i < fa.ml_count; ++i) c += p2_popcount64(fa.ml_masks[i]);
        } else if (subop == 2u) {
            c = static_cast<uint64_t>(fa.ml_count);
        } else if (subop == 3u) {
            if (fa.active_lane_count > 0u) {
                uint64_t req = (fa.active_lane_count >= 64u) ? ~0ULL : ((1ULL << fa.active_lane_count) - 1ULL);
                for (uint32_t i = 0; i < fa.ml_count; ++i) {
                    if ((fa.ml_masks[i] & req) == req) ++c;
                }
            }
        } else { // subop == 4u
            if (imm_lo >= fa.alloc_width || imm_lo >= 64u) {
                ws.status = IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                VM_DISPATCH();
            }
            uint64_t bit = 1ULL << imm_lo;
            for (uint32_t i = 0; i < fa.ml_count; ++i) {
                if ((fa.ml_masks[i] & bit) != 0ULL) ++c;
            }
        }
        ws.write_r(dst, static_cast<uint8_t>(ScalarType::SCALAR_U64), c);
        p1_set_scalar_flags(ws, true, c);
        ws.state.pc += 1u;
    } else {
        ws.status = IMPULSE_VM_ERR_INVALID_OPCODE;
    }
    VM_DISPATCH();
}
