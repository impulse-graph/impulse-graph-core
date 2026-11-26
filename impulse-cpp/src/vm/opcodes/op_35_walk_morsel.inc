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
    if (!ws.is_f_init(s1)) { ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; V11_DISPATCH(); }
    const V11Frontier* f_src = &ws.f_regs[s1];

    bool is_coo = (subop == 2u || subop == 3u);
    bool is_rev = (subop == 1u || subop == 3u);
    uint16_t in_dom  = is_rev ? rel->tgt_dom : rel->src_dom;
    uint16_t out_dom = is_rev ? rel->src_dom : rel->tgt_dom;
    if (is_coo && !rel->has_coo) { ws.status = IMPULSE_VM_V11_ERR_INVALID_OPERAND; V11_DISPATCH(); }
    if (f_src->domain_id != in_dom) { ws.status = IMPULSE_VM_V11_ERR_DOMAIN_MISMATCH; V11_DISPATCH(); }

    const uint32_t* s_excl = nullptr; uint32_t s_excl_len = 0u;
    if ((flags & FLAG_MASKED_S3) != 0u) {
        const V11Frontier* fe = p3_get_f1bit(ws, s3);
        if (!fe) V11_DISPATCH();
        if (fe->domain_id != out_dom) { ws.status = IMPULSE_VM_V11_ERR_DOMAIN_MISMATCH; V11_DISPATCH(); }
        if (!fe->materialize_1bit(ws.arena, &s_excl, &s_excl_len)) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
    }

    if (!is_coo && !is_rev && !f_src->multi_lane) {
        const uint32_t* src_ids = nullptr; uint32_t src_len = 0u;
        if (!f_src->materialize_1bit(ws.arena, &src_ids, &src_len)) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
        V11MorselFwdCsr fwd_csr{rel, src_ids, src_len, s_excl, s_excl_len};
        p3_run_morsel_block(ws, *snap, words, word_count, hdr_pc, end_pc, out_dom, false, nullptr, 0u, steps, max_steps, &fwd_csr);
        V11_DISPATCH();
    }

    V11MorselItem* items = nullptr;
    uint32_t item_cnt = 0u;
    if (is_coo) {
        uint64_t in_card = (in_dom < snap->domain_count) ? snap->domain_cards[in_dom] : 0ULL;
        const uint32_t n_in = static_cast<uint32_t>(in_card);
        uint64_t* src_lanes = (n_in > 0u) ? ws.arena.alloc_array<uint64_t>(n_in, 8) : nullptr;
        if (n_in > 0u && !src_lanes) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
        if (src_lanes) std::memset(src_lanes, 0, static_cast<size_t>(n_in) * sizeof(uint64_t));
        if (f_src->multi_lane) {
            for (uint32_t k = 0; k < f_src->ml_count; ++k) {
                uint64_t node = f_src->ml_nodes[k];
                if (node < n_in) src_lanes[node] = f_src->ml_masks[k];
            }
        } else {
            const uint32_t* src_ids = nullptr; uint32_t src_len = 0u;
            if (!f_src->materialize_1bit(ws.arena, &src_ids, &src_len)) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
            for (uint32_t i = 0; i < src_len; ++i) {
                if (src_ids[i] < n_in) src_lanes[src_ids[i]] = 1ULL;
            }
        }
        const uint32_t m = rel->coo_edge_count;
        size_t cap = 0u;
        for (uint32_t i = 0; i < m; ++i) {
            uint64_t key = is_rev ? rel->coo_tgt(i) : rel->coo_src(i);
            if (key < n_in && src_lanes[key] != 0ULL) ++cap;
        }
        if (cap > 0u) {
            items = ws.arena.alloc_array<V11MorselItem>(cap, 8);
            if (!items) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
            for (uint32_t i = 0; i < m; ++i) {
                uint64_t u = rel->coo_src(i);
                uint64_t v = rel->coo_tgt(i);
                uint64_t eid = rel->coo_edge_id(i);
                if (!is_rev) {
                    uint64_t lm = (u < n_in) ? src_lanes[u] : 0ULL;
                    if (lm != 0ULL && !p3_sorted_contains(s_excl, s_excl_len, static_cast<uint32_t>(v))) {
                        items[item_cnt++] = {u, v, eid, lm};
                    }
                } else {
                    uint64_t lm = (v < n_in) ? src_lanes[v] : 0ULL;
                    if (lm != 0ULL && !p3_sorted_contains(s_excl, s_excl_len, static_cast<uint32_t>(u))) {
                        items[item_cnt++] = {u, v, eid, lm};
                    }
                }
            }
        }
    } else if (!is_rev) {
        // Multi-lane CSR forward morsel
        size_t cap = 0u;
        for (uint32_t k = 0; k < f_src->ml_count; ++k) {
            if (f_src->ml_masks[k] == 0ULL) continue;
            uint32_t u = static_cast<uint32_t>(f_src->ml_nodes[k]);
            if (u + 1u < rel->row_off_len) cap += (rel->csr_row_off(u + 1u) - rel->csr_row_off(u));
        }
        if (cap > 0u) {
            items = ws.arena.alloc_array<V11MorselItem>(cap, 8);
            if (!items) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
            for (uint32_t k = 0; k < f_src->ml_count; ++k) {
                uint64_t lm = f_src->ml_masks[k];
                if (lm == 0ULL) continue;
                uint32_t u = static_cast<uint32_t>(f_src->ml_nodes[k]);
                if (u + 1u < rel->row_off_len) {
                    uint32_t e0 = rel->csr_row_off(u), e1 = rel->csr_row_off(u + 1u);
                    for (uint32_t e = e0; e < e1; ++e) {
                        uint64_t v = rel->csr_col_tgt(e);
                        if (!p3_sorted_contains(s_excl, s_excl_len, static_cast<uint32_t>(v))) {
                            items[item_cnt++] = {u, v, e, lm};
                        }
                    }
                }
            }
        }
    } else {
        V11RevAdj rev_adj{};
        if (!p3_build_rev_adj(ws.arena, *snap, *rel, &rev_adj)) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
        if (f_src->multi_lane) {
            size_t cap = 0u;
            for (uint32_t k = 0; k < f_src->ml_count; ++k) {
                if (f_src->ml_masks[k] == 0ULL) continue;
                uint32_t v = static_cast<uint32_t>(f_src->ml_nodes[k]);
                if (v < rev_adj.n_tgt) cap += (rev_adj.offsets[v + 1u] - rev_adj.offsets[v]);
            }
            if (cap > 0u) {
                items = ws.arena.alloc_array<V11MorselItem>(cap, 8);
                if (!items) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
                for (uint32_t k = 0; k < f_src->ml_count; ++k) {
                    uint64_t lm = f_src->ml_masks[k];
                    if (lm == 0ULL) continue;
                    uint32_t v = static_cast<uint32_t>(f_src->ml_nodes[k]);
                    if (v < rev_adj.n_tgt) {
                        for (uint32_t p = rev_adj.offsets[v]; p < rev_adj.offsets[v + 1u]; ++p) {
                            uint32_t u = rev_adj.srcs[p];
                            if (!p3_sorted_contains(s_excl, s_excl_len, u)) {
                                items[item_cnt++] = {u, v, rev_adj.edges[p], lm};
                            }
                        }
                    }
                }
            }
        } else {
            const uint32_t* src_ids = nullptr; uint32_t src_len = 0u;
            if (!f_src->materialize_1bit(ws.arena, &src_ids, &src_len)) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
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
    }

    p3_run_morsel_block(ws, *snap, words, word_count, hdr_pc, end_pc, out_dom, false, items, item_cnt, steps, max_steps);
    V11_DISPATCH();
}
