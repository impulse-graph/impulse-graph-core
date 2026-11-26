// Opcode 0x25: OP_SET_CONTAINS
V11_OP_CASE(SET_CONTAINS) {
    const uint8_t subop = IMP_V11_SUBOP(w);
    const uint8_t dst = IMP_V11_DST(w);
    const uint8_t s1 = IMP_V11_S1(w);
    const uint8_t s2 = IMP_V11_S2(w);

    if (!ws.is_f_init(s1)) { ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; V11_DISPATCH(); }
    const V11Frontier& fa = ws.f_regs[s1];
    if (subop == 0u) {
        if (fa.multi_lane) { ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; V11_DISPATCH(); }
        if (!ws.is_r_valid(s2)) {
            ws.write_r_null(dst);
            p1_set_scalar_flags(ws, false, 0ULL);
            ws.state.pc += 1u;
            V11_DISPATCH();
        }
        uint64_t u = ws.read_r_raw(s2);
        if (u >= fa.domain_card || u > UINT32_MAX) { ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; V11_DISPATCH(); }
        uint64_t v = fa.contains_1bit(static_cast<uint32_t>(u)) ? 1ULL : 0ULL;
        ws.write_r(dst, static_cast<uint8_t>(ScalarType::SCALAR_BOOL_3VL), v);
        p1_set_scalar_flags(ws, true, v);
        ws.state.pc += 1u;
    } else if (subop == 1u) {
        if (!fa.multi_lane) { ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; V11_DISPATCH(); }
        if (!ws.is_r_valid(s2)) {
            ws.write_r_null(dst);
            p1_set_scalar_flags(ws, false, 0ULL);
            ws.state.pc += 1u;
            V11_DISPATCH();
        }
        uint64_t u = ws.read_r_raw(s2);
        if (u >= fa.domain_card) { ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; V11_DISPATCH(); }
        uint64_t mask = 0ULL;
        if (fa.ml_nodes != nullptr && fa.ml_count > 0u) {
            const uint64_t* it = std::lower_bound(fa.ml_nodes, fa.ml_nodes + fa.ml_count, u);
            if (it != fa.ml_nodes + fa.ml_count && *it == u) mask = fa.ml_masks[it - fa.ml_nodes];
        }
        ws.write_r(dst, static_cast<uint8_t>(ScalarType::SCALAR_U64), mask);
        p1_set_scalar_flags(ws, true, mask);
        ws.state.pc += 1u;
    } else {
        ws.status = IMPULSE_VM_V11_ERR_INVALID_OPCODE;
    }
    V11_DISPATCH();
}
