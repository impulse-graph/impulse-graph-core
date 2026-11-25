// Opcode 0x61: OP_LANE_INIT_GROUPS
V11_OP_CASE(LANE_INIT_GROUPS) {
    const uint8_t flags = IMP_V11_FLAGS(w);
    const uint8_t dst = IMP_V11_DST(w);
    const uint8_t s1 = IMP_V11_S1(w);
    const uint8_t s2 = IMP_V11_S2(w);
    const uint16_t dom = IMP_V11_IMM16_LO(w);

    if (!snap) { ws.status = IMPULSE_VM_V11_ERR_NULL_SNAPSHOT; V11_DISPATCH(); }
    if (dom >= snap->domain_count) { ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; V11_DISPATCH(); }
    const uint64_t dcard = snap->domain_cards[dom];

    const V11Vector* nodes = ws.v_regs[s1];
    const V11Vector* lanes = ws.v_regs[s2];
    if (!nodes || !lanes ||
        (nodes->elem_tag != kVecTagU64 && nodes->elem_tag != kVecTagI64) ||
        (lanes->elem_tag != kVecTagU64 && lanes->elem_tag != kVecTagI64)) {
        ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; V11_DISPATCH();
    }
    if (nodes->length != lanes->length) { ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; V11_DISPATCH(); }

    V11MlMap init_m{};
    int32_t max_lane = -1;
    if ((flags & FLAG_ACCUM) != 0u) {
        const V11Frontier* fd = p5_get_fml(ws, dst);
        if (!fd) V11_DISPATCH();
        if (fd->domain_id != dom) { ws.status = IMPULSE_VM_V11_ERR_DOMAIN_MISMATCH; V11_DISPATCH(); }
        init_m = p5_ml_view(*fd);
        max_lane = static_cast<int32_t>(fd->active_lane_count) - 1;
    }

    size_t cap = static_cast<size_t>(init_m.len) + static_cast<size_t>(nodes->length);
    V11MlPair* pairs = (cap > 0u) ? ws.arena.alloc_array<V11MlPair>(cap, 8) : nullptr;
    if (cap > 0u && !pairs) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
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
            ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; V11_DISPATCH();
        }
        int32_t l = static_cast<int32_t>(l64);
        if (l > max_lane) max_lane = l;
        pairs[cnt++] = {u, 1ULL << static_cast<unsigned>(l)};
    }

    V11MlMap m{};
    if (!p5_ml_from_pairs(ws.arena, pairs, cnt, &m)) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
    uint8_t ac = (max_lane < 0) ? 0u : static_cast<uint8_t>(max_lane + 1);
    if (!ws.f_regs[dst].init_multi_lane(ws.arena, dom, dcard, p5_choose_width(ac), ac, m.nodes, m.masks, m.len, ws.geometry)) {
        ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH();
    }
    ws.set_f_init(dst, true);
    ws.set_zf(m.len == 0u);
    ws.state.pc += 1u;
    V11_DISPATCH();
}
