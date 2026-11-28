// Opcode 0x20: OP_FSEED
VM_OP_CASE(FSEED) {
    const uint8_t subop = IMP_SUBOP(w);
    const bool accum = (IMP_FLAGS(w) & FLAG_ACCUM) != 0u;
    const uint8_t dst = IMP_DST(w);
    const uint8_t s1 = IMP_S1(w);
    const uint16_t imm_lo = IMP_IMM16_LO(w);
    const uint16_t imm_hi = IMP_IMM16_HI(w);

    if (subop <= 2u) {
        uint16_t dom_id = imm_hi;
        uint64_t dom_card = 0;
        impulse_vm_status_t st = vm_lookup_domain_card(snap, dom_id, &dom_card);
        if (st != IMPULSE_VM_OK) { ws.status = st; VM_DISPATCH(); }
        const uint32_t* base = nullptr;
        uint32_t base_len = 0u;
        if (accum) {
            if (!ws.is_f_init(dst)) { ws.status = IMPULSE_VM_ERR_TYPE_MISMATCH; VM_DISPATCH(); }
            const VmFrontier& ex = ws.f_regs[dst];
            if (ex.domain_id != dom_id) { ws.status = IMPULSE_VM_ERR_DOMAIN_MISMATCH; VM_DISPATCH(); }
            if (ex.multi_lane) { ws.status = IMPULSE_VM_ERR_TYPE_MISMATCH; VM_DISPATCH(); }
            if (!ex.materialize_1bit(ws.arena, &base, &base_len)) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
        }
        switch (subop) {
            case 0:
                ws.f_regs[dst].reset_empty(dom_id, dom_card);
                ws.set_f_init(dst, true);
                ws.set_zf(true);
                ws.state.pc += 1u;
                break;
            case 1:
                if (static_cast<uint64_t>(imm_lo) >= dom_card) {
                    ws.status = IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                } else {
                    const uint32_t* next_p = nullptr;
                    uint32_t next_n = 0u;
                    if (!p2_add_sorted_unique(ws.arena, base, base_len, static_cast<uint32_t>(imm_lo), &next_p, &next_n)) {
                        ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; break;
                    }
                    if (!p2_set_f1bit(ws, dst, dom_id, dom_card, next_p, next_n, false, true)) break;
                    ws.set_zf(next_n == 0u);
                    ws.state.pc += 1u;
                }
                break;
            case 2:
                if (!ws.is_r_valid(s1)) {
                    if (!p2_set_f1bit(ws, dst, dom_id, dom_card, base, base_len, false, true)) break;
                    ws.set_zf(base_len == 0u);
                    ws.state.pc += 1u;
                } else {
                    uint64_t u = ws.read_r_raw(s1);
                    if (u >= dom_card || u > UINT32_MAX) {
                        ws.status = IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                    } else {
                        const uint32_t* next_p = nullptr;
                        uint32_t next_n = 0u;
                        if (!p2_add_sorted_unique(ws.arena, base, base_len, static_cast<uint32_t>(u), &next_p, &next_n)) {
                            ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; break;
                        }
                        if (!p2_set_f1bit(ws, dst, dom_id, dom_card, next_p, next_n, false, true)) break;
                        ws.set_zf(next_n == 0u);
                        ws.state.pc += 1u;
                    }
                }
                break;
            default: ws.status = IMPULSE_VM_ERR_INVALID_OPCODE; break;
        }
    } else if (subop == 3u || subop == 4u) {
        if (!ws.is_f_init(dst) || ws.f_regs[dst].multi_lane) { ws.status = IMPULSE_VM_ERR_TYPE_MISMATCH; VM_DISPATCH(); }
        const VmFrontier& fd = ws.f_regs[dst];
        if (!ws.is_r_valid(s1)) { ws.set_zf(fd.is_empty()); ws.state.pc += 1u; VM_DISPATCH(); }
        uint64_t u = ws.read_r_raw(s1);
        if (u >= fd.domain_card || u > UINT32_MAX) { ws.status = IMPULSE_VM_ERR_OUT_OF_BOUNDS; VM_DISPATCH(); }
        const uint32_t* cur_p = nullptr;
        uint32_t cur_n = 0u;
        if (!fd.materialize_1bit(ws.arena, &cur_p, &cur_n)) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
        bool know_card = (fd.cached_card >= 0);
        uint16_t dom_id = fd.domain_id;
        uint64_t dom_card = fd.domain_card;
        const uint32_t* next_p = nullptr;
        uint32_t next_n = 0u;
        bool ok = (subop == 3u)
            ? p2_add_sorted_unique(ws.arena, cur_p, cur_n, static_cast<uint32_t>(u), &next_p, &next_n)
            : p2_remove_sorted(ws.arena, cur_p, cur_n, static_cast<uint32_t>(u), &next_p, &next_n);
        if (!ok) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
        if (!p2_set_f1bit(ws, dst, dom_id, dom_card, next_p, next_n, false, know_card)) { VM_DISPATCH(); }
        ws.set_zf(next_n == 0u);
        ws.state.pc += 1u;
    } else {
        ws.status = IMPULSE_VM_ERR_INVALID_OPCODE;
    }
    VM_DISPATCH();
}
