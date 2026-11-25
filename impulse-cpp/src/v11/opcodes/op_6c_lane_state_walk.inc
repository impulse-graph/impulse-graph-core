// Opcode 0x6C: OP_LANE_STATE_WALK
V11_OP_CASE(LANE_STATE_WALK) {
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
    if (subop > 9u) { ws.status = IMPULSE_VM_V11_ERR_INVALID_OPCODE; V11_DISPATCH(); }
    const V11Vector* ls = ws.v_regs[s1];
    if (!ls || ls->elem_tag != kVecTagLaneState) { ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; V11_DISPATCH(); }

    const bool is_rev = (subop >= 5u);
    const uint8_t semi = subop % 5u;
    const uint16_t in_dom  = is_rev ? rel->tgt_dom : rel->src_dom;
    const uint16_t out_dom = is_rev ? rel->src_dom : rel->tgt_dom;
    if (ls->ls_dom != in_dom) { ws.status = IMPULSE_VM_V11_ERR_DOMAIN_MISMATCH; V11_DISPATCH(); }

    const bool scalar_w = (flags & FLAG_SCALAR_RHS) != 0u;
    const bool scalar_w_valid = scalar_w && p4_is_scalar_f64_valid(ws, s2);
    const double scalar_w_val = scalar_w_valid ? p4_read_scalar_f64(ws, s2) : std::numeric_limits<double>::quiet_NaN();
    const V11Vector* weight_col = nullptr;
    if (!scalar_w && attr_id != 0xFFFFu) {
        weight_col = p3_find_edge_attr(*rel, attr_id);
        if (!weight_col) { ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; V11_DISPATCH(); }
        if (!p4_is_numeric_vec(weight_col)) { ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; V11_DISPATCH(); }
    }

    const V11Frontier* mask = nullptr;
    if ((flags & FLAG_MASKED_S3) != 0u) {
        mask = p3_get_f1bit(ws, s3);
        if (!mask) V11_DISPATCH();
        if (mask->domain_id != out_dom) { ws.status = IMPULSE_VM_V11_ERR_DOMAIN_MISMATCH; V11_DISPATCH(); }
    }

    if (out_dom >= snap->domain_count) { ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; V11_DISPATCH(); }
    uint64_t n_out64 = snap->domain_cards[out_dom];
    if (n_out64 > UINT32_MAX) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
    uint32_t n_out = static_cast<uint32_t>(n_out64);
    uint16_t k = ls->ls_k;
    bool has_wit = (ls->ls_witness != nullptr);

    const V11Vector* dst_ls = nullptr;
    if ((flags & FLAG_ACCUM) != 0u) {
        dst_ls = ws.v_regs[dst];
        if (!dst_ls || dst_ls->elem_tag != kVecTagLaneState) { ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; V11_DISPATCH(); }
        if (dst_ls->ls_dom != out_dom) { ws.status = IMPULSE_VM_V11_ERR_DOMAIN_MISMATCH; V11_DISPATCH(); }
        if (dst_ls->length != n_out || dst_ls->ls_k != k) { ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; V11_DISPATCH(); }
    }

    V11Vector* ov = p5_alloc_lane_state(ws.arena, out_dom, n_out, k, has_wit);
    if (!ov) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }

    size_t out_total = static_cast<size_t>(n_out) * k;
    if (dst_ls != nullptr) {
        if (out_total > 0u) {
            std::memcpy(ov->ls_vals, dst_ls->ls_vals, out_total * sizeof(double));
            std::memcpy(ov->ls_valid, dst_ls->ls_valid, out_total);
            if (has_wit && dst_ls->ls_witness != nullptr) {
                std::memcpy(ov->ls_witness, dst_ls->ls_witness, out_total * sizeof(int64_t));
                std::memcpy(ov->ls_witness_valid, dst_ls->ls_witness_valid, out_total);
            }
        }
    } else if (semi == 3u && out_total > 0u) {
        std::memset(ov->ls_valid, 1, out_total);
    }

    // Jacobi-safe: ov is newly allocated so ls->ls_vals / ls->ls_valid are untouched even when dst == s1
    uint64_t hits = 0ULL;
    if (!is_rev) {
        for (uint32_t u = 0; u < ls->length; ++u) {
            if (rel->row_off_len == 0u || u >= rel->row_off_len - 1u) continue;
            for (uint32_t e = rel->row_offsets[u]; e < rel->row_offsets[u + 1u]; ++e) {
                uint32_t v = static_cast<uint32_t>(rel->col_targets[e]);
                p5_relax_lane_edge(u, v, e, k, semi, mask, scalar_w, scalar_w_valid, scalar_w_val, weight_col,
                                   ls->ls_vals, ls->ls_valid, ov->ls_vals, ov->ls_valid,
                                   ov->ls_witness, ov->ls_witness_valid, n_out, hits);
            }
        }
    } else {
        V11RevAdj rev_adj{};
        if (!p3_build_rev_adj(ws.arena, *snap, *rel, &rev_adj)) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
        for (uint32_t v = 0; v < ls->length; ++v) {
            if (v >= rev_adj.n_tgt) continue;
            for (uint32_t idx = rev_adj.offsets[v]; idx < rev_adj.offsets[v + 1u]; ++idx) {
                p5_relax_lane_edge(v, rev_adj.srcs[idx], rev_adj.edges[idx], k, semi, mask,
                                   scalar_w, scalar_w_valid, scalar_w_val, weight_col,
                                   ls->ls_vals, ls->ls_valid, ov->ls_vals, ov->ls_valid,
                                   ov->ls_witness, ov->ls_witness_valid, n_out, hits);
            }
        }
    }

    ws.v_regs[dst] = ov;
    ws.set_zf(hits == 0ULL);
    ws.set_st(hits == 0ULL);
    ws.state.pc += 1u;
    V11_DISPATCH();
}
