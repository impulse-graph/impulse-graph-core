// Opcode 0x27: OP_STABLE_CHECK
V11_OP_CASE(STABLE_CHECK) {
    const uint8_t subop = IMP_V11_SUBOP(w);
    const uint8_t dst = IMP_V11_DST(w);
    const uint8_t s1 = IMP_V11_S1(w);
    const uint8_t s2 = IMP_V11_S2(w);
    const uint8_t s3 = IMP_V11_S3(w);

    if (subop == 0u) {
        if (!ws.is_f_init(s1) || !ws.is_f_init(s2)) { ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; V11_DISPATCH(); }
        const V11Frontier& fa = ws.f_regs[s1];
        const V11Frontier& fb = ws.f_regs[s2];
        if (fa.domain_id != fb.domain_id) { ws.status = IMPULSE_VM_V11_ERR_DOMAIN_MISMATCH; V11_DISPATCH(); }
        if (fa.multi_lane != fb.multi_lane) { ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; V11_DISPATCH(); }
        bool eq = false;
        if (!fa.multi_lane) {
            if (fa.tier == kTierAllSpan && fb.tier == kTierAllSpan) eq = (fa.domain_card == fb.domain_card);
            else if (fa.tier == kTierAllSpan) eq = (static_cast<uint64_t>(fb.node_count) == fa.domain_card);
            else if (fb.tier == kTierAllSpan) eq = (static_cast<uint64_t>(fa.node_count) == fb.domain_card);
            else eq = (fa.node_count == fb.node_count) &&
                      (fa.node_count == 0u ||
                       std::memcmp(fa.nodes, fb.nodes, static_cast<size_t>(fa.node_count) * sizeof(uint32_t)) == 0);
        } else {
            if (fa.alloc_width != fb.alloc_width) { ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; V11_DISPATCH(); }
            eq = (fa.ml_count == fb.ml_count) &&
                 (fa.ml_count == 0u ||
                  (std::memcmp(fa.ml_nodes, fb.ml_nodes, static_cast<size_t>(fa.ml_count) * sizeof(uint64_t)) == 0 &&
                   std::memcmp(fa.ml_masks, fb.ml_masks, static_cast<size_t>(fa.ml_count) * sizeof(uint64_t)) == 0));
        }
        ws.set_st(eq);
        ws.set_zf(eq);
        ws.state.pc += 1u;
    } else if (subop == 1u) {
        if (!ws.is_r_valid(s3)) { ws.status = IMPULSE_VM_V11_ERR_INVALID_OPERAND; V11_DISPATCH(); }
        const V11Vector* v1 = ws.v_regs[s1];
        const V11Vector* v2 = ws.v_regs[s2];
        if (!v1 || !v2) { ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; V11_DISPATCH(); }
        double eps = p1_bits_to_f64(ws.read_r_raw(s3));
        if (std::isnan(eps)) { ws.status = IMPULSE_VM_V11_ERR_INVALID_OPERAND; V11_DISPATCH(); }
        if (!p2_is_numeric_vec(v1) || !p2_is_numeric_vec(v2)) { ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; V11_DISPATCH(); }
        if (v1->length != v2->length) { ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; V11_DISPATCH(); }
        double sum = 0.0;
        bool null_mismatch = false;
        for (uint32_t i = 0; i < v1->length; ++i) {
            bool b1 = v1->is_valid_at(i);
            bool b2 = v2->is_valid_at(i);
            if (!b1 && !b2) continue;
            if (b1 != b2) null_mismatch = true;
            else sum += std::fabs(p2_read_numeric_as_f64(*v1, i) - p2_read_numeric_as_f64(*v2, i));
        }
        if (std::isnan(sum)) {
            ws.write_r_null(dst);
            ws.set_zf(true);
            ws.set_st(false);
            ws.set_nf(true);
        } else {
            ws.write_r(dst, static_cast<uint8_t>(ScalarType::SCALAR_F64), p1_f64_to_bits(sum));
            ws.set_zf(sum == 0.0);
            ws.set_st(!null_mismatch && sum <= eps);
            ws.set_nf(false);
        }
        ws.state.pc += 1u;
    } else {
        ws.status = IMPULSE_VM_V11_ERR_INVALID_OPCODE;
    }
    V11_DISPATCH();
}
