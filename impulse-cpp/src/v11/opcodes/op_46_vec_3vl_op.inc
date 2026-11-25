// Opcode 0x46: OP_VEC_3VL_OP
V11_OP_CASE(VEC_3VL_OP) {
    const uint8_t subop = IMP_V11_SUBOP(w);
    const uint8_t flags = IMP_V11_FLAGS(w);
    const uint8_t dst = IMP_V11_DST(w);
    const uint8_t s1 = IMP_V11_S1(w);
    const uint8_t s2 = IMP_V11_S2(w);
    const uint8_t s3 = IMP_V11_S3(w);

    if (subop >= 12u) { ws.status = IMPULSE_VM_V11_ERR_INVALID_OPCODE; V11_DISPATCH(); }

    const V11Frontier* mask = p4_get_mask_frontier(ws, flags, s3);
    if (ws.status != IMPULSE_VM_V11_OK) V11_DISPATCH();
    const V11Vector* va = ws.v_regs[s1];
    if (!va) { ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; V11_DISPATCH(); }
    const uint32_t n = va->length;
    const size_t vwords = (n > 0u) ? ((static_cast<size_t>(n) + 63u) >> 6) : 1u;

    if (subop >= 8u && subop <= 11u) { // is_nan, is_inf, is_finite, assert_finite
        if (va->elem_tag != kVecTagF64) { ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; V11_DISPATCH(); }
        if (subop == 11u) {
            for (uint32_t i = 0; i < n; ++i) {
                if (!p4_lane_active(mask, i)) continue;
                if (va->is_valid_at(i) && !std::isfinite(va->f64_data[i])) {
                    ws.status = IMPULSE_VM_V11_ERR_ASSERTION_FAILED; V11_DISPATCH();
                }
            }
            ws.state.pc += 1u;
            V11_DISPATCH();
        }
        uint64_t* out_valid = ws.arena.alloc_array<uint64_t>(vwords, 8);
        uint64_t* out_data  = ws.arena.alloc_array<uint64_t>(vwords, 8);
        auto* ov = static_cast<V11Vector*>(ws.arena.alloc(sizeof(V11Vector), alignof(V11Vector)));
        if (!out_valid || !out_data || !ov) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
        std::memset(out_valid, 0, vwords * sizeof(uint64_t));
        std::memset(out_data, 0, vwords * sizeof(uint64_t));
        for (uint32_t i = 0; i < n; ++i) {
            if (!p4_lane_active(mask, i)) continue;
            bool res = false;
            if (va->is_valid_at(i)) {
                double x = va->f64_data[i];
                if (subop == 8u) res = std::isnan(x);
                else if (subop == 9u) res = std::isinf(x);
                else if (subop == 10u) res = std::isfinite(x);
            }
            uint64_t bit = 1ULL << (i & 63u);
            out_valid[i >> 6] |= bit;
            if (res) out_data[i >> 6] |= bit;
        }
        new (ov) V11Vector();
        ov->elem_tag = kVecTagBool3vl; ov->length = n; ov->valid_bits = out_valid; ov->bool_data_bits = out_data;
        ws.v_regs[dst] = ov;
        ws.state.pc += 1u;
        V11_DISPATCH();
    }

    if (subop == subop::kVEC_3VL_OP_IS_NULL || subop == subop::kVEC_3VL_OP_IS_NOT_NULL) {
        uint64_t* out_valid = ws.arena.alloc_array<uint64_t>(vwords, 8);
        uint64_t* out_data  = ws.arena.alloc_array<uint64_t>(vwords, 8);
        auto* ov = static_cast<V11Vector*>(ws.arena.alloc(sizeof(V11Vector), alignof(V11Vector)));
        if (!out_valid || !out_data || !ov) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
        std::memset(out_valid, 0, vwords * sizeof(uint64_t));
        std::memset(out_data, 0, vwords * sizeof(uint64_t));
        for (uint32_t i = 0; i < n; ++i) {
            if (!p4_lane_active(mask, i)) continue;
            bool res = (subop == subop::kVEC_3VL_OP_IS_NULL) ? !va->is_valid_at(i) : va->is_valid_at(i);
            uint64_t bit = 1ULL << (i & 63u);
            out_valid[i >> 6] |= bit;
            if (res) out_data[i >> 6] |= bit;
        }
        new (ov) V11Vector();
        ov->elem_tag = kVecTagBool3vl; ov->length = n; ov->valid_bits = out_valid; ov->bool_data_bits = out_data;
        ws.v_regs[dst] = ov;
        ws.state.pc += 1u;
        V11_DISPATCH();
    }

    if (va->elem_tag != kVecTagBool3vl) { ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; V11_DISPATCH(); }
    const bool is_binary = (subop == 0u || subop == 1u || subop == 3u);
    const bool scalar_rhs = (flags & FLAG_SCALAR_RHS) != 0u;
    const V11Vector* vb = nullptr;
    if (is_binary && !scalar_rhs) {
        vb = ws.v_regs[s2];
        if (!vb || vb->elem_tag != kVecTagBool3vl) { ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; V11_DISPATCH(); }
        if (vb->length != n) { ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; V11_DISPATCH(); }
    }
    const bool s_valid = is_binary && scalar_rhs && ws.is_r_valid(s2);
    const bool s_val   = s_valid && (ws.read_r_raw(s2) != 0ULL);

    uint64_t* out_valid = ws.arena.alloc_array<uint64_t>(vwords, 8);
    uint64_t* out_data  = ws.arena.alloc_array<uint64_t>(vwords, 8);
    auto* ov = static_cast<V11Vector*>(ws.arena.alloc(sizeof(V11Vector), alignof(V11Vector)));
    if (!out_valid || !out_data || !ov) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
    std::memset(out_valid, 0, vwords * sizeof(uint64_t));
    std::memset(out_data, 0, vwords * sizeof(uint64_t));

    for (uint32_t i = 0; i < n; ++i) {
        if (!p4_lane_active(mask, i)) continue;
        bool av = va->is_valid_at(i);
        bool ad = av && va->bool_data_at(i);
        bool rv = false, rd = false;
        switch (subop) {
            case 0: { // and
                bool bv = scalar_rhs ? s_valid : vb->is_valid_at(i);
                bool bd = bv && (scalar_rhs ? s_val : vb->bool_data_at(i));
                if ((av && !ad) || (bv && !bd)) { rv = true; rd = false; }
                else if (av && ad && bv && bd)  { rv = true; rd = true; }
                break;
            }
            case 1: { // or
                bool bv = scalar_rhs ? s_valid : vb->is_valid_at(i);
                bool bd = bv && (scalar_rhs ? s_val : vb->bool_data_at(i));
                if ((av && ad) || (bv && bd))      { rv = true; rd = true; }
                else if (av && !ad && bv && !bd)   { rv = true; rd = false; }
                break;
            }
            case 2: rv = av; rd = av && !ad; break; // not
            case 3: { // xor
                bool bv = scalar_rhs ? s_valid : vb->is_valid_at(i);
                bool bd = bv && (scalar_rhs ? s_val : vb->bool_data_at(i));
                if (av && bv) { rv = true; rd = (ad != bd); }
                break;
            }
            case subop::kVEC_3VL_OP_IS_TRUE:  rv = true; rd = (av && ad); break;
            case subop::kVEC_3VL_OP_IS_FALSE: rv = true; rd = (av && !ad); break;
            default: break;
        }
        if (rv) {
            uint64_t bit = 1ULL << (i & 63u);
            out_valid[i >> 6] |= bit;
            if (rd) out_data[i >> 6] |= bit;
        }
    }
    new (ov) V11Vector();
    ov->elem_tag = kVecTagBool3vl; ov->length = n; ov->valid_bits = out_valid; ov->bool_data_bits = out_data;
    ws.v_regs[dst] = ov;
    ws.state.pc += 1u;
    V11_DISPATCH();
}
