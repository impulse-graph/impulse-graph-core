// Opcode 0x47: OP_VEC_STR_PRED
V11_OP_CASE(VEC_STR_PRED) {
    const uint8_t subop = IMP_V11_SUBOP(w);
    const uint8_t flags = IMP_V11_FLAGS(w);
    const uint8_t dst = IMP_V11_DST(w);
    const uint8_t s1 = IMP_V11_S1(w);
    const uint8_t s2 = IMP_V11_S2(w);
    const uint8_t s3 = IMP_V11_S3(w);

    const V11Frontier* mask = p4_get_mask_frontier(ws, flags, s3);
    if (ws.status != IMPULSE_VM_V11_OK) V11_DISPATCH();
    const V11Vector* va = ws.v_regs[s1];
    if (!va || va->elem_tag != kVecTagStr) { ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; V11_DISPATCH(); }

    const uint32_t n = va->length;
    const bool scalar_rhs = (flags & FLAG_SCALAR_RHS) != 0u;
    const size_t vwords = (n > 0u) ? ((static_cast<size_t>(n) + 63u) >> 6) : 1u;

    if (subop == 9u) { // .len_eq
        const V11Vector* vb = nullptr;
        if (!scalar_rhs) {
            vb = ws.v_regs[s2];
            if (!vb || (vb->elem_tag != kVecTagI64 && vb->elem_tag != kVecTagU64)) {
                ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; V11_DISPATCH();
            }
            if (vb->length != n) { ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; V11_DISPATCH(); }
        }
        uint64_t* out_valid = ws.arena.alloc_array<uint64_t>(vwords, 8);
        uint64_t* out_data  = ws.arena.alloc_array<uint64_t>(vwords, 8);
        auto* ov = static_cast<V11Vector*>(ws.arena.alloc(sizeof(V11Vector), alignof(V11Vector)));
        if (!out_valid || !out_data || !ov) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
        std::memset(out_valid, 0, vwords * sizeof(uint64_t));
        std::memset(out_data, 0, vwords * sizeof(uint64_t));
        bool s_valid = scalar_rhs && ws.is_r_valid(s2);
        int64_t s_len = static_cast<int64_t>(ws.read_r_raw(s2));
        for (uint32_t i = 0; i < n; ++i) {
            if (!p4_lane_active(mask, i) || !va->is_valid_at(i)) continue;
            if (scalar_rhs ? !s_valid : !vb->is_valid_at(i)) continue;
            int64_t want = scalar_rhs ? s_len : vb->i64_data[i];
            uint64_t bit = 1ULL << (i & 63u);
            out_valid[i >> 6] |= bit;
            if (static_cast<int64_t>(va->str_data[i].len) == want) out_data[i >> 6] |= bit;
        }
        new (ov) V11Vector();
        ov->elem_tag = kVecTagBool3vl; ov->length = n; ov->valid_bits = out_valid; ov->bool_data_bits = out_data;
        ws.v_regs[dst] = ov;
        ws.state.pc += 1u;
        V11_DISPATCH();
    }

    const V11Vector* vb = nullptr;
    V11StrSlice s_str{nullptr, 0u, 0u};
    if (scalar_rhs) {
        if (ws.is_r_valid(s2)) s_str = ws.resolve_scalar_str(ws.read_r_raw(s2));
    } else {
        vb = ws.v_regs[s2];
        if (!vb || vb->elem_tag != kVecTagStr) { ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; V11_DISPATCH(); }
        if (vb->length != n) { ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; V11_DISPATCH(); }
    }

    uint64_t* out_valid = ws.arena.alloc_array<uint64_t>(vwords, 8);
    uint64_t* out_data  = ws.arena.alloc_array<uint64_t>(vwords, 8);
    auto* ov = static_cast<V11Vector*>(ws.arena.alloc(sizeof(V11Vector), alignof(V11Vector)));
    if (!out_valid || !out_data || !ov) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
    std::memset(out_valid, 0, vwords * sizeof(uint64_t));
    std::memset(out_data, 0, vwords * sizeof(uint64_t));

    for (uint32_t i = 0; i < n; ++i) {
        if (!p4_lane_active(mask, i) || !va->is_valid_at(i)) continue;
        V11StrSlice a = va->str_data[i];
        V11StrSlice b{};
        if (scalar_rhs) {
            if (!s_str.valid) continue;
            b = s_str;
        } else {
            if (!vb->is_valid_at(i)) continue;
            b = vb->str_data[i];
        }
        bool ok = false;
        switch (subop) {
            case 0: ok = p4_str_eq(a, b); break;
            case 1: ok = !p4_str_eq(a, b); break;
            case 2: ok = p4_str_cmp(a, b) <  0; break;
            case 3: ok = p4_str_cmp(a, b) <= 0; break;
            case 4: ok = p4_str_cmp(a, b) >  0; break;
            case 5: ok = p4_str_cmp(a, b) >= 0; break;
            case 6: ok = p4_str_starts_with(a, b); break;
            case 7: ok = p4_str_ends_with(a, b); break;
            case 8: ok = p4_str_contains(a, b); break;
            default: break;
        }
        uint64_t bit = 1ULL << (i & 63u);
        out_valid[i >> 6] |= bit;
        if (ok) out_data[i >> 6] |= bit;
    }
    new (ov) V11Vector();
    ov->elem_tag = kVecTagBool3vl; ov->length = n; ov->valid_bits = out_valid; ov->bool_data_bits = out_data;
    ws.v_regs[dst] = ov;
    ws.state.pc += 1u;
    V11_DISPATCH();
}
