// Opcode 0x35: OP_WALK_MORSEL
V11_OP_CASE(WALK_MORSEL) {
    const uint8_t subop = IMP_V11_SUBOP(w);
    const uint8_t flags = IMP_V11_FLAGS(w);
    const uint8_t s1 = IMP_V11_S1(w);
    const uint8_t s3 = IMP_V11_S3(w);
    const uint16_t rel_id = IMP_V11_IMM16_LO(w);
    const uint16_t end_off = IMP_V11_IMM16_HI(w);
    const uint32_t hdr_pc = ws.state.pc;
    const uint32_t end_pc = hdr_pc + end_off;

    const V11RelView* rel = p3_get_rel(snap, ws, rel_id);
    if (!rel) V11_DISPATCH();
    const V11Frontier* f_src = p3_get_f1bit(ws, s1);
    if (!f_src) V11_DISPATCH();

    bool is_rev = (subop == 1u);
    uint16_t in_dom  = is_rev ? rel->tgt_dom : rel->src_dom;
    uint16_t out_dom = is_rev ? rel->src_dom : rel->tgt_dom;
    if (f_src->domain_id != in_dom) { ws.status = IMPULSE_VM_V11_ERR_DOMAIN_MISMATCH; V11_DISPATCH(); }

    const uint32_t* s_excl = nullptr; uint32_t s_excl_len = 0u;
    if ((flags & FLAG_MASKED_S3) != 0u) {
        const V11Frontier* fe = p3_get_f1bit(ws, s3);
        if (!fe) V11_DISPATCH();
        if (fe->domain_id != out_dom) { ws.status = IMPULSE_VM_V11_ERR_DOMAIN_MISMATCH; V11_DISPATCH(); }
        if (!fe->materialize_1bit(ws.arena, &s_excl, &s_excl_len)) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
    }

    const uint32_t* src_ids = nullptr; uint32_t src_len = 0u;
    if (!f_src->materialize_1bit(ws.arena, &src_ids, &src_len)) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }

    V11MorselItem* items = nullptr;
    uint32_t item_cnt = 0u;
    if (!is_rev) {
        size_t cap = 0u;
        for (uint32_t i = 0; i < src_len; ++i) {
            uint32_t u = src_ids[i];
            if (u + 1u < rel->row_off_len) cap += (rel->row_offsets[u + 1u] - rel->row_offsets[u]);
        }
        if (cap > 0u) {
            items = ws.arena.alloc_array<V11MorselItem>(cap, 8);
            if (!items) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
            for (uint32_t i = 0; i < src_len; ++i) {
                uint32_t u = src_ids[i];
                if (u + 1u < rel->row_off_len) {
                    for (uint32_t e = rel->row_offsets[u]; e < rel->row_offsets[u + 1u]; ++e) {
                        uint32_t v = static_cast<uint32_t>(rel->col_targets[e]);
                        if (!p3_sorted_contains(s_excl, s_excl_len, v)) {
                            items[item_cnt++] = {u, v, e, 1ULL};
                        }
                    }
                }
            }
        }
    } else {
        V11RevAdj rev_adj{};
        if (!p3_build_rev_adj(ws.arena, *snap, *rel, &rev_adj)) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
        size_t cap = 0u;
        for (uint32_t i = 0; i < src_len; ++i) {
            uint32_t v = src_ids[i];
            if (v < rev_adj.n_tgt) cap += (rev_adj.offsets[v + 1u] - rev_adj.offsets[v]);
        }
        if (cap > 0u) {
            items = ws.arena.alloc_array<V11MorselItem>(cap, 8);
            if (!items) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
            for (uint32_t i = 0; i < src_len; ++i) {
                uint32_t v = src_ids[i];
                if (v < rev_adj.n_tgt) {
                    for (uint32_t k = rev_adj.offsets[v]; k < rev_adj.offsets[v + 1u]; ++k) {
                        uint32_t u = rev_adj.srcs[k];
                        if (!p3_sorted_contains(s_excl, s_excl_len, u)) {
                            items[item_cnt++] = {u, v, rev_adj.edges[k], 1ULL};
                        }
                    }
                }
            }
        }
    }

    p3_run_morsel_block(ws, *snap, words, word_count, hdr_pc, end_pc, out_dom, false, items, item_cnt, steps, max_steps);
    V11_DISPATCH();
}
