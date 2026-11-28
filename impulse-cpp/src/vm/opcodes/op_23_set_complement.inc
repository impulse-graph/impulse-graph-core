// Opcode 0x23: OP_SET_COMPLEMENT
VM_OP_CASE(SET_COMPLEMENT) {
    const uint8_t dst = IMP_DST(w);
    const uint8_t s1 = IMP_S1(w);
    if (!ws.is_f_init(s1) || ws.f_regs[s1].multi_lane) { ws.status = IMPULSE_VM_ERR_TYPE_MISMATCH; VM_DISPATCH(); }
    const VmFrontier& fa = ws.f_regs[s1];
    uint16_t dom_id = fa.domain_id;
    uint64_t n = fa.domain_card;
    if (fa.tier == kTierAllSpan) {
        ws.f_regs[dst].reset_empty(dom_id, n);
        ws.set_f_init(dst, true);
        ws.set_zf(true);
        ws.state.pc += 1u;
        VM_DISPATCH();
    }
    if (fa.tier == kTierEmpty) {
        ws.f_regs[dst].init_all_span(dom_id, n);
        ws.set_f_init(dst, true);
        ws.set_zf(n == 0ULL);
        ws.state.pc += 1u;
        VM_DISPATCH();
    }
    if (n > UINT32_MAX) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
    const uint32_t* sa = nullptr;
    uint32_t sa_len = 0u;
    if (!fa.materialize_1bit(ws.arena, &sa, &sa_len)) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
    uint32_t n32 = static_cast<uint32_t>(n);
    uint32_t comp_len = (n32 >= sa_len) ? (n32 - sa_len) : 0u;
    uint32_t* comp = (comp_len > 0u) ? ws.arena.alloc_array<uint32_t>(comp_len, 8) : nullptr;
    if (comp_len > 0u && !comp) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
    uint32_t p = 0u, k = 0u;
    for (uint32_t u = 0u; u < n32; ++u) {
        if (p < sa_len && sa[p] == u) ++p;
        else comp[k++] = u;
    }
    bool is_all = (sa_len == 0u && n > 0ULL);
    if (!p2_set_f1bit(ws, dst, dom_id, n, comp, k, is_all, true)) { VM_DISPATCH(); }
    ws.set_zf(k == 0u);
    ws.state.pc += 1u;
    VM_DISPATCH();
}
