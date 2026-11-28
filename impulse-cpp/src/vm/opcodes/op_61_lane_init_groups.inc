// Opcode 0x61: OP_LANE_INIT_GROUPS
VM_OP_CASE(LANE_INIT_GROUPS) {
    const uint8_t flags = IMP_FLAGS(w);
    const uint8_t dst = IMP_DST(w);
    const uint8_t s1 = IMP_S1(w);
    const uint8_t s2 = IMP_S2(w);
    const uint16_t dom = IMP_IMM16_LO(w);

    if (!snap) { ws.status = IMPULSE_VM_ERR_NULL_SNAPSHOT; VM_DISPATCH(); }
    if (dom >= snap->domain_count) { ws.status = IMPULSE_VM_ERR_OUT_OF_BOUNDS; VM_DISPATCH(); }
    const uint64_t dcard = snap->domain_cards[dom];

    const VmVector* nodes = ws.v_regs[s1];
    const VmVector* lanes = ws.v_regs[s2];
    if (!nodes || !lanes ||
        (nodes->elem_tag != kVecTagU64 && nodes->elem_tag != kVecTagI64) ||
        (lanes->elem_tag != kVecTagU64 && lanes->elem_tag != kVecTagI64)) {
        ws.status = IMPULSE_VM_ERR_TYPE_MISMATCH; VM_DISPATCH();
    }
    if (nodes->length != lanes->length) { ws.status = IMPULSE_VM_ERR_OUT_OF_BOUNDS; VM_DISPATCH(); }

    VmMlMap init_m{};
    int32_t max_lane = -1;
    if ((flags & FLAG_ACCUM) != 0u) {
        const VmFrontier* fd = p5_get_fml(ws, dst);
        if (!fd) VM_DISPATCH();
        if (fd->domain_id != dom) { ws.status = IMPULSE_VM_ERR_DOMAIN_MISMATCH; VM_DISPATCH(); }
        init_m = p5_ml_view(*fd);
        max_lane = static_cast<int32_t>(fd->active_lane_count) - 1;
    }

    size_t cap = static_cast<size_t>(init_m.len) + static_cast<size_t>(nodes->length);
    VmMlPair* pairs = (cap > 0u) ? ws.arena.alloc_array<VmMlPair>(cap, 8) : nullptr;
    if (cap > 0u && !pairs) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
    uint32_t cnt = 0u;
    for (uint32_t i = 0; i < init_m.len; ++i) {
        if (init_m.masks[i] != 0ULL) pairs[cnt++] = {init_m.nodes[i], init_m.masks[i]};
    }
    for (uint32_t i = 0; i < nodes->length; ++i) {
        if (!nodes->is_valid_at(i) || !lanes->is_valid_at(i)) continue;
        int64_t u_s = nodes->i64_data[i];
        int64_t l64 = lanes->i64_data[i];
        uint64_t u = static_cast<uint64_t>(u_s);
        if (u_s < 0 || u >= dcard || l64 < 0 || l64 >= 64) {
            ws.status = IMPULSE_VM_ERR_OUT_OF_BOUNDS; VM_DISPATCH();
        }
        int32_t l = static_cast<int32_t>(l64);
        if (l > max_lane) max_lane = l;
        pairs[cnt++] = {u, 1ULL << static_cast<unsigned>(l)};
    }

    VmMlMap m{};
    if (!p5_ml_from_pairs(ws.arena, pairs, cnt, &m)) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
    uint8_t ac = (max_lane < 0) ? 0u : static_cast<uint8_t>(max_lane + 1);
    if (!ws.f_regs[dst].init_multi_lane(ws.arena, dom, dcard, p5_choose_width(ac), ac, m.nodes, m.masks, m.len, ws.geometry)) {
        ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH();
    }
    ws.set_f_init(dst, true);
    ws.set_zf(m.len == 0u);
    ws.state.pc += 1u;
    VM_DISPATCH();
}
