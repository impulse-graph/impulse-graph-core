// Opcode 0x66: OP_LANE_REDUCE_AND
V11_OP_CASE(LANE_REDUCE_AND) {
    const uint8_t flags = IMP_V11_FLAGS(w);
    const uint8_t dst = IMP_V11_DST(w);
    const uint8_t s1 = IMP_V11_S1(w);
    const uint8_t s2 = IMP_V11_S2(w);

    const V11Frontier* fa = p5_get_fml(ws, s1);
    if (!fa) V11_DISPATCH();

    uint64_t req = 0ULL;
    if ((flags & FLAG_SCALAR_RHS) != 0u) {
        if (!ws.is_r_valid(s2)) { ws.status = IMPULSE_VM_V11_ERR_INVALID_OPERAND; V11_DISPATCH(); }
        req = ws.read_r_raw(s2);
    } else {
        req = p5_active_lane_mask(fa->active_lane_count);
    }

    const uint16_t dom_id = fa->domain_id;
    const uint64_t dom_card = fa->domain_card;
    const uint32_t* acc_ids = nullptr; uint32_t acc_len = 0u;
    if ((flags & FLAG_ACCUM) != 0u) {
        const V11Frontier* fd = p3_get_f1bit(ws, dst);
        if (!fd) V11_DISPATCH();
        if (fd->domain_id != dom_id) { ws.status = IMPULSE_VM_V11_ERR_DOMAIN_MISMATCH; V11_DISPATCH(); }
        if (!fd->materialize_1bit(ws.arena, &acc_ids, &acc_len)) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
    }

    uint32_t* kept = (req != 0ULL && fa->ml_count > 0u) ? ws.arena.alloc_array<uint32_t>(fa->ml_count, 8) : nullptr;
    if (req != 0ULL && fa->ml_count > 0u && !kept) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
    uint32_t cnt = 0u;
    if (req != 0ULL) {
        for (uint32_t i = 0; i < fa->ml_count; ++i) {
            if ((fa->ml_masks[i] & req) == req) kept[cnt++] = static_cast<uint32_t>(fa->ml_nodes[i]);
        }
    }

    uint32_t* final_ids = kept;
    uint32_t final_len = cnt;
    if ((flags & FLAG_ACCUM) != 0u && acc_len > 0u) {
        if (!p3_union_sorted(ws.arena, acc_ids, acc_len, kept, cnt, &final_ids, &final_len)) {
            ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH();
        }
    }
    if (!ws.f_regs[dst].init_from_sorted_ids(ws.arena, dom_id, dom_card, final_ids, final_len, ws.geometry, true)) {
        ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH();
    }
    ws.set_f_init(dst, true);
    ws.set_zf(final_len == 0u);
    ws.state.pc += 1u;
    V11_DISPATCH();
}
