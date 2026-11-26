// Opcode 0x50: OP_VEC_INIT
V11_OP_CASE(VEC_INIT) {
    const uint8_t subop = IMP_V11_SUBOP(w);
    const uint8_t flags = IMP_V11_FLAGS(w);
    const uint8_t dst = IMP_V11_DST(w);
    const uint8_t s2 = IMP_V11_S2(w);
    const uint16_t imm_lo = IMP_V11_IMM16_LO(w);

    const bool dom_mode = (subop & 8u) != 0u;
    uint32_t len = 0u;
    if (dom_mode) {
        if (!snap) { ws.status = IMPULSE_VM_V11_ERR_NULL_SNAPSHOT; V11_DISPATCH(); }
        if (imm_lo >= snap->domain_count) { ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; V11_DISPATCH(); }
        uint64_t c = snap->domain_cards[imm_lo];
        if (c > UINT32_MAX) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
        len = static_cast<uint32_t>(c);
    } else {
        len = static_cast<uint32_t>(imm_lo);
    }

    const uint8_t base_type = subop & 7u;
    const bool fill_scalar = (flags & FLAG_SCALAR_RHS) != 0u;
    const size_t vwords = (len > 0u) ? ((static_cast<size_t>(len) + 63u) >> 6) : 1u;
    const bool valid_fill = !fill_scalar || ws.is_r_valid(s2);

    auto* ov = static_cast<V11Vector*>(ws.arena.alloc(sizeof(V11Vector), alignof(V11Vector)));
    if (!ov) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
    new (ov) V11Vector();
    ov->length = len;

    switch (base_type) {
        case 0: case 2: case 1: case 3: {
            uint64_t* valid = valid_fill ? p4_alloc_valid_all_true(ws.arena, len) : ws.arena.alloc_array<uint64_t>(vwords, 8);
            int64_t* arr = (len > 0u) ? ws.arena.alloc_array<int64_t>(len, 8) : nullptr;
            if (!valid || (len > 0u && !arr)) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
            if (!valid_fill) std::memset(valid, 0, vwords * sizeof(uint64_t));
            int64_t fv = (fill_scalar && valid_fill) ? static_cast<int64_t>(ws.read_r_raw(s2)) : 0;
            for (uint32_t i = 0; i < len; ++i) arr[i] = fv;
            ov->elem_tag = (base_type == 1u || base_type == 3u) ? kVecTagU64 : kVecTagI64;
            ov->i64_data = arr;
            ov->valid_bits = valid;
            break;
        }
        case 4: case 5: {
            bool f_valid = !fill_scalar || p4_is_scalar_f64_valid(ws, s2);
            uint64_t* valid = f_valid ? p4_alloc_valid_all_true(ws.arena, len) : ws.arena.alloc_array<uint64_t>(vwords, 8);
            double* arr = (len > 0u) ? ws.arena.alloc_array<double>(len, 8) : nullptr;
            if (!valid || (len > 0u && !arr)) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
            if (!f_valid) std::memset(valid, 0, vwords * sizeof(uint64_t));
            double fv = (fill_scalar && f_valid) ? p4_read_scalar_f64(ws, s2) : 0.0;
            for (uint32_t i = 0; i < len; ++i) arr[i] = fv;
            ov->elem_tag = kVecTagF64;
            ov->f64_data = arr;
            ov->valid_bits = valid;
            break;
        }
        case 6: {
            uint64_t* valid = valid_fill ? p4_alloc_valid_all_true(ws.arena, len) : ws.arena.alloc_array<uint64_t>(vwords, 8);
            bool d_true = fill_scalar && valid_fill && (ws.read_r_raw(s2) != 0ULL);
            uint64_t* data = d_true ? p4_alloc_valid_all_true(ws.arena, len) : ws.arena.alloc_array<uint64_t>(vwords, 8);
            if (!valid || !data) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
            if (!valid_fill) std::memset(valid, 0, vwords * sizeof(uint64_t));
            if (!d_true) std::memset(data, 0, vwords * sizeof(uint64_t));
            ov->elem_tag = kVecTagBool3vl;
            ov->valid_bits = valid;
            ov->bool_data_bits = data;
            break;
        }
        default:
            ws.status = IMPULSE_VM_V11_ERR_INVALID_OPCODE;
            V11_DISPATCH();
    }
    ws.v_regs[dst] = ov;
    ws.state.pc += 1u;
    V11_DISPATCH();
}
