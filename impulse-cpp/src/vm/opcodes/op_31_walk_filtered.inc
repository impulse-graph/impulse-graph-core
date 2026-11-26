// Opcode 0x31: OP_WALK_FILTERED
V11_OP_CASE(WALK_FILTERED) {
    const uint8_t subop = IMP_V11_SUBOP(w);
    const uint8_t flags = IMP_V11_FLAGS(w);
    const uint8_t dst = IMP_V11_DST(w);
    const uint8_t s1 = IMP_V11_S1(w);
    const uint8_t s2 = IMP_V11_S2(w);
    const uint8_t s3 = IMP_V11_S3(w);
    const uint16_t rel_id = IMP_V11_IMM16_LO(w);
    const uint16_t attr_id = IMP_V11_IMM16_HI(w);

    const V11RelView* rel = p3_get_rel(snap, ws, rel_id);
    if (!rel) V11_DISPATCH();
    const V11Frontier* f_src = p3_get_f1bit(ws, s1);
    if (!f_src) V11_DISPATCH();

    bool is_rev = (subop >= 8u);
    uint8_t cmp_op = subop & 7u;
    uint16_t in_dom  = is_rev ? rel->tgt_dom : rel->src_dom;
    uint16_t out_dom = is_rev ? rel->src_dom : rel->tgt_dom;
    if (f_src->domain_id != in_dom) { ws.status = IMPULSE_VM_V11_ERR_DOMAIN_MISMATCH; V11_DISPATCH(); }

    const V11Vector* col = p3_find_edge_attr(*rel, attr_id);
    if (!col) { ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; V11_DISPATCH(); }

    const uint32_t* s_excl = nullptr; uint32_t s_excl_len = 0u;
    if ((flags & FLAG_MASKED_S3) != 0u) {
        const V11Frontier* fe = p3_get_f1bit(ws, s3);
        if (!fe) V11_DISPATCH();
        if (fe->domain_id != out_dom) { ws.status = IMPULSE_VM_V11_ERR_DOMAIN_MISMATCH; V11_DISPATCH(); }
        if (!fe->materialize_1bit(ws.arena, &s_excl, &s_excl_len)) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
    }
    const uint32_t* s_accum = nullptr; uint32_t s_accum_len = 0u;
    if ((flags & FLAG_ACCUM) != 0u) {
        const V11Frontier* fd = p3_get_f1bit(ws, dst);
        if (!fd) V11_DISPATCH();
        if (fd->domain_id != out_dom) { ws.status = IMPULSE_VM_V11_ERR_DOMAIN_MISMATCH; V11_DISPATCH(); }
        if (!fd->materialize_1bit(ws.arena, &s_accum, &s_accum_len)) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
    }

    bool rhs_valid = (cmp_op <= 5u) && ws.is_r_valid(s2);
    uint64_t rhs_bits = rhs_valid ? ws.read_r_raw(s2) : 0ULL;
    uint8_t rhs_ty = rhs_valid ? ws.read_r_type(s2) : 0u;

    V11MorselAccF out_acc{};
    if (!(cmp_op <= 5u && !rhs_valid)) {
        const uint32_t* src_ids = nullptr; uint32_t src_len = 0u;
        if (!f_src->materialize_1bit(ws.arena, &src_ids, &src_len)) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
        if (!is_rev) {
            for (uint32_t i = 0; i < src_len; ++i) {
                uint32_t u = src_ids[i];
                if (u + 1u < rel->row_off_len) {
                    for (uint32_t e = rel->csr_row_off(u); e < rel->csr_row_off(u + 1u); ++e) {
                        uint32_t v = static_cast<uint32_t>(rel->csr_col_tgt(e));
                        if (!p3_sorted_contains(s_excl, s_excl_len, v) &&
                            p3_eval_edge_pred(*col, e, cmp_op, rhs_valid, rhs_bits, rhs_ty)) {
                            if (!out_acc.push(ws.arena, v)) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
                        }
                    }
                }
            }
        } else {
            V11RevAdj rev_adj{};
            if (!p3_build_rev_adj(ws.arena, *snap, *rel, &rev_adj)) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
            for (uint32_t i = 0; i < src_len; ++i) {
                uint32_t v = src_ids[i];
                if (v < rev_adj.n_tgt) {
                    for (uint32_t k = rev_adj.offsets[v]; k < rev_adj.offsets[v + 1u]; ++k) {
                        uint32_t u = rev_adj.srcs[k];
                        uint32_t e = rev_adj.edges[k];
                        if (!p3_sorted_contains(s_excl, s_excl_len, u) &&
                            p3_eval_edge_pred(*col, e, cmp_op, rhs_valid, rhs_bits, rhs_ty)) {
                            if (!out_acc.push(ws.arena, u)) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
                        }
                    }
                }
            }
        }
    }

    uint32_t out_uniq = p2_dedupe_sorted_inplace(out_acc.data, out_acc.len);
    uint32_t* final_s = out_acc.data;
    uint32_t final_len = out_uniq;
    if (s_accum_len > 0u) {
        if (!p3_union_sorted(ws.arena, s_accum, s_accum_len, out_acc.data, out_uniq, &final_s, &final_len)) {
            ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH();
        }
    }
    uint64_t out_card = (out_dom < snap->domain_count) ? snap->domain_cards[out_dom] : 0ULL;
    if (!ws.f_regs[dst].init_from_sorted_ids(ws.arena, out_dom, out_card, final_s, final_len, ws.geometry, false)) {
        ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH();
    }
    ws.set_f_init(dst, true);
    ws.set_zf(final_len == 0u);
    ws.state.pc += 1u;
    V11_DISPATCH();
}
