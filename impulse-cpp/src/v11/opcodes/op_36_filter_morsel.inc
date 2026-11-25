// Opcode 0x36: OP_FILTER_MORSEL
V11_OP_CASE(FILTER_MORSEL) {
    const uint8_t s1 = IMP_V11_S1(w);
    const uint16_t end_off = IMP_V11_IMM16_HI(w);
    const uint32_t hdr_pc = ws.state.pc;
    const uint32_t end_pc = hdr_pc + end_off;

    if (!snap) { ws.status = IMPULSE_VM_V11_ERR_NULL_SNAPSHOT; V11_DISPATCH(); }
    const V11Frontier* f_src = p3_get_f1bit(ws, s1);
    if (!f_src) V11_DISPATCH();

    const uint32_t* src_ids = nullptr; uint32_t src_len = 0u;
    if (!f_src->materialize_1bit(ws.arena, &src_ids, &src_len)) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }

    V11MorselItem* items = (src_len > 0u) ? ws.arena.alloc_array<V11MorselItem>(src_len, 8) : nullptr;
    if (src_len > 0u && !items) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
    for (uint32_t i = 0; i < src_len; ++i) {
        items[i] = {src_ids[i], 0ULL, 0ULL, 1ULL};
    }

    p3_run_morsel_block(ws, *snap, words, word_count, hdr_pc, end_pc, f_src->domain_id, true, items, src_len, steps, max_steps);
    V11_DISPATCH();
}
