// Opcode 0x60: OP_LANE_INIT_SEEDS
VM_OP_CASE(LANE_INIT_SEEDS) {
    const uint8_t subop = IMP_SUBOP(w);
    const uint8_t dst = IMP_DST(w);
    const uint8_t s1 = IMP_S1(w);
    const uint16_t dom = IMP_IMM16_LO(w);

    if (!snap) { ws.status = IMPULSE_VM_ERR_NULL_SNAPSHOT; VM_DISPATCH(); }
    if (dom >= snap->domain_count) { ws.status = IMPULSE_VM_ERR_OUT_OF_BOUNDS; VM_DISPATCH(); }
    const uint64_t dcard = snap->domain_cards[dom];

    const VmVector* seeds = ws.v_regs[s1];
    if (!seeds || (seeds->elem_tag != kVecTagU64 && seeds->elem_tag != kVecTagI64)) {
        ws.status = IMPULSE_VM_ERR_TYPE_MISMATCH; VM_DISPATCH();
    }
    const uint32_t k = seeds->length;
    if (k > 64u) { ws.status = IMPULSE_VM_ERR_OUT_OF_BOUNDS; VM_DISPATCH(); }

    VmMlPair* pairs = (k > 0u) ? ws.arena.alloc_array<VmMlPair>(k, 8) : nullptr;
    if (k > 0u && !pairs) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
    uint32_t cnt = 0u;
    for (uint32_t lane = 0; lane < k; ++lane) {
        if (!seeds->is_valid_at(lane)) continue;
        int64_t u_s = seeds->i64_data[lane];
        uint64_t u = static_cast<uint64_t>(u_s);
        if (u_s < 0 || u >= dcard) { ws.status = IMPULSE_VM_ERR_OUT_OF_BOUNDS; VM_DISPATCH(); }
        pairs[cnt++] = {u, 1ULL << lane};
    }
    VmMlMap m{};
    if (!p5_ml_from_pairs(ws.arena, pairs, cnt, &m)) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
    uint8_t aw = (subop == 1u) ? 64u : p5_choose_width(k);
    if (!ws.f_regs[dst].init_multi_lane(ws.arena, dom, dcard, aw, static_cast<uint8_t>(k), m.nodes, m.masks, m.len, ws.geometry)) {
        ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH();
    }
    ws.set_f_init(dst, true);
    ws.set_zf(m.len == 0u);
    ws.state.pc += 1u;
    VM_DISPATCH();
}
