// Opcode 0x65: OP_LANE_FILTER_QUORUM
VM_OP_CASE(LANE_FILTER_QUORUM) {
    const uint8_t subop = IMP_SUBOP(w);
    const uint8_t flags = IMP_FLAGS(w);
    const uint8_t dst = IMP_DST(w);
    const uint8_t s1 = IMP_S1(w);
    const uint8_t s2 = IMP_S2(w);
    const uint16_t imm_lo = IMP_IMM16_LO(w);

    const VmFrontier* fa = p5_get_fml(ws, s1);
    if (!fa) VM_DISPATCH();
    if (subop > 2u) { ws.status = IMPULSE_VM_ERR_INVALID_OPCODE; VM_DISPATCH(); }

    int64_t k = 0;
    if ((flags & FLAG_SCALAR_RHS) != 0u) {
        if (!ws.is_r_valid(s2)) { ws.status = IMPULSE_VM_ERR_INVALID_OPERAND; VM_DISPATCH(); }
        k = static_cast<int64_t>(ws.read_r_raw(s2));
    } else {
        k = static_cast<int64_t>(imm_lo);
    }

    const uint16_t dom_id = fa->domain_id;
    const uint64_t dom_card = fa->domain_card;
    const uint32_t* acc_ids = nullptr; uint32_t acc_len = 0u;
    if ((flags & FLAG_ACCUM) != 0u) {
        const VmFrontier* fd = p3_get_f1bit(ws, dst);
        if (!fd) VM_DISPATCH();
        if (fd->domain_id != dom_id) { ws.status = IMPULSE_VM_ERR_DOMAIN_MISMATCH; VM_DISPATCH(); }
        if (!fd->materialize_1bit(ws.arena, &acc_ids, &acc_len)) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
    }

    uint32_t* kept = (fa->ml_count > 0u) ? ws.arena.alloc_array<uint32_t>(fa->ml_count, 8) : nullptr;
    if (fa->ml_count > 0u && !kept) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
    uint32_t cnt = 0u;
    for (uint32_t i = 0; i < fa->ml_count; ++i) {
        int64_t p = static_cast<int64_t>(std::popcount(fa->ml_masks[i]));
        bool keep = (subop == 0u) ? (p >= k) : (subop == 1u) ? (p == k) : (p > 0 && p <= k);
        if (keep) kept[cnt++] = static_cast<uint32_t>(fa->ml_nodes[i]);
    }

    uint32_t* final_ids = kept;
    uint32_t final_len = cnt;
    if ((flags & FLAG_ACCUM) != 0u && acc_len > 0u) {
        if (!p3_union_sorted(ws.arena, acc_ids, acc_len, kept, cnt, &final_ids, &final_len)) {
            ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH();
        }
    }
    if (!ws.f_regs[dst].init_from_sorted_ids(ws.arena, dom_id, dom_card, final_ids, final_len, ws.geometry, true)) {
        ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH();
    }
    ws.set_f_init(dst, true);
    ws.set_zf(final_len == 0u);
    ws.state.pc += 1u;
    VM_DISPATCH();
}
