// Opcode 0x24: OP_SET_CARDINALITY
V11_OP_CASE(SET_CARDINALITY) {
    const uint8_t subop = IMP_V11_SUBOP(w);
    const uint8_t dst = IMP_V11_DST(w);
    const uint8_t s1 = IMP_V11_S1(w);
    const uint16_t imm_lo = IMP_V11_IMM16_LO(w);

    if (!ws.is_f_init(s1)) { ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; V11_DISPATCH(); }
    V11Frontier& fa = ws.f_regs[s1];
    if (subop == 0u) {
        if (fa.multi_lane) { ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; V11_DISPATCH(); }
        uint64_t c = fa.cardinality();
        ws.write_r(dst, static_cast<uint8_t>(ScalarType::SCALAR_U64), c);
        p1_set_scalar_flags(ws, true, c);
        ws.state.pc += 1u;
    } else if (subop >= 1u && subop <= 4u) {
        if (!fa.multi_lane) { ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; V11_DISPATCH(); }
        uint64_t c = 0ULL;
        switch (subop) {
            case 1:
                for (uint32_t i = 0; i < fa.ml_count; ++i) c += p2_popcount64(fa.ml_masks[i]);
                break;
            case 2:
                c = static_cast<uint64_t>(fa.ml_count);
                break;
            case 3:
                if (fa.active_lane_count > 0u) {
                    uint64_t req = (fa.active_lane_count >= 64u) ? ~0ULL : ((1ULL << fa.active_lane_count) - 1ULL);
                    for (uint32_t i = 0; i < fa.ml_count; ++i) {
                        if ((fa.ml_masks[i] & req) == req) ++c;
                    }
                }
                break;
            case 4:
                if (imm_lo >= fa.alloc_width || imm_lo >= 64u) {
                    ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS;
                    V11_DISPATCH();
                } else {
                    uint64_t bit = 1ULL << imm_lo;
                    for (uint32_t i = 0; i < fa.ml_count; ++i) {
                        if ((fa.ml_masks[i] & bit) != 0ULL) ++c;
                    }
                }
                break;
            default:
                ws.status = IMPULSE_VM_V11_ERR_INVALID_OPCODE;
                V11_DISPATCH();
        }
        ws.write_r(dst, static_cast<uint8_t>(ScalarType::SCALAR_U64), c);
        p1_set_scalar_flags(ws, true, c);
        ws.state.pc += 1u;
    } else {
        ws.status = IMPULSE_VM_V11_ERR_INVALID_OPCODE;
    }
    V11_DISPATCH();
}
