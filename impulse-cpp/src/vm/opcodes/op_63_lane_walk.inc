// Opcode 0x63: OP_LANE_WALK
V11_OP_CASE(LANE_WALK) {
    const uint8_t subop = IMP_V11_SUBOP(w);
    const uint8_t flags = IMP_V11_FLAGS(w);
    const uint8_t dst = IMP_V11_DST(w);
    const uint8_t s1 = IMP_V11_S1(w);
    const uint8_t s2 = IMP_V11_S2(w);
    const uint8_t s3 = IMP_V11_S3(w);
    const uint16_t rel_id = IMP_V11_IMM16_LO(w);

    const V11RelView* rel = p3_get_rel(snap, ws, rel_id);
    if (!rel) V11_DISPATCH();
    const V11Frontier* f_src = p5_get_fml(ws, s1);
    if (!f_src) V11_DISPATCH();
    if (subop > 9u) { ws.status = IMPULSE_VM_V11_ERR_INVALID_OPCODE; V11_DISPATCH(); }

    const bool endo = (rel->src_dom == rel->tgt_dom);
    const bool is_rev = (subop == 1u || subop == 5u || subop == 6u);
    const bool needs_fwd = (subop == 0u || subop == 2u || subop == 3u || subop == 4u || subop == 7u || subop == 8u || subop == 9u);
    const bool needs_rev = (subop == 1u || subop == 2u || subop == 5u || subop == 6u || subop == 8u || subop == 9u);
    const bool needs_endo = (subop >= 2u && subop != 7u);
    const uint16_t in_dom  = is_rev ? rel->tgt_dom : rel->src_dom;
    const uint16_t out_dom = is_rev ? rel->src_dom : rel->tgt_dom;
    if (needs_fwd && !rel->has_csr && (rel->has_csc || rel->has_coo)) {
        ws.status = IMPULSE_VM_V11_ERR_INVALID_OPERAND;
        V11_DISPATCH();
    }
    if (needs_rev && rel->csc_row_off_raw == nullptr) {
        ws.status = IMPULSE_VM_V11_ERR_INVALID_OPERAND;
        V11_DISPATCH();
    }
    if (needs_endo && !endo) { ws.status = IMPULSE_VM_V11_ERR_DOMAIN_MISMATCH; V11_DISPATCH(); }
    if (f_src->domain_id != in_dom) { ws.status = IMPULSE_VM_V11_ERR_DOMAIN_MISMATCH; V11_DISPATCH(); }

    const V11Vector* edge_mask = nullptr;
    if (subop == 7u || subop == 9u) {
        edge_mask = ws.v_regs[s2];
        if (!edge_mask || (edge_mask->elem_tag != kVecTagU64 && edge_mask->elem_tag != kVecTagI64)) {
            ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; V11_DISPATCH();
        }
        if (edge_mask->length < rel->col_tgt_len) {
            ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; V11_DISPATCH();
        }
    }

    if (subop == 0u && (flags & (FLAG_MASKED_S3 | FLAG_ACCUM)) == 0u) {
        const uint64_t out_card = (out_dom < snap->domain_count) ? snap->domain_cards[out_dom] : 0ULL;
        if (out_card <= 1000000ULL) {
            const uint32_t oc = static_cast<uint32_t>(out_card);
            uint64_t* dense = (oc > 0u) ? ws.arena.alloc_array<uint64_t>(oc, 64) : nullptr;
            if (oc > 0u && !dense) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
            if (oc > 0u) std::memset(dense, 0, static_cast<size_t>(oc) * sizeof(uint64_t));
            const uint32_t roff_len = rel->row_off_len;
            const uint32_t ctgt_len = rel->col_tgt_len;
            for (uint32_t i = 0; i < f_src->ml_count; ++i) {
                uint64_t m = f_src->ml_masks[i];
                if (m == 0ULL) continue;
                uint64_t u64 = f_src->ml_nodes[i];
                if (u64 + 1ULL >= roff_len) continue;
                uint32_t s = rel->csr_row_off(static_cast<uint32_t>(u64));
                uint32_t e_end = rel->csr_row_off(static_cast<uint32_t>(u64) + 1u);
                if (e_end > ctgt_len) e_end = ctgt_len;
                for (uint32_t e = s; e < e_end; ++e) {
                    uint64_t v = rel->csr_col_tgt(e);
                    if (v < oc) dense[v] |= m;
                }
            }
            uint32_t cnt = 0u;
            for (uint32_t v = 0; v < oc; ++v) if (dense[v] != 0ULL) ++cnt;
            uint64_t* out_nodes = (cnt > 0u) ? ws.arena.alloc_array<uint64_t>(cnt, 8) : nullptr;
            uint64_t* out_masks = (cnt > 0u) ? ws.arena.alloc_array<uint64_t>(cnt, 8) : nullptr;
            if (cnt > 0u && (!out_nodes || !out_masks)) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
            uint32_t k = 0u;
            for (uint32_t v = 0; v < oc; ++v) {
                uint64_t m = dense[v];
                if (m != 0ULL) { out_nodes[k] = v; out_masks[k] = m; ++k; }
            }
            if (!ws.f_regs[dst].init_multi_lane(ws.arena, out_dom, out_card, f_src->alloc_width, f_src->active_lane_count, out_nodes, out_masks, cnt, ws.geometry)) {
                ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH();
            }
            ws.set_f_init(dst, true);
            ws.set_zf(cnt == 0u);
            ws.state.pc += 1u;
            V11_DISPATCH();
        }
    }

    V11MlMap m_excl{};
    if ((flags & FLAG_MASKED_S3) != 0u) {
        const V11Frontier* fe = p5_get_fml(ws, s3);
        if (!fe) V11_DISPATCH();
        if (fe->domain_id != out_dom) { ws.status = IMPULSE_VM_V11_ERR_DOMAIN_MISMATCH; V11_DISPATCH(); }
        m_excl = p5_ml_view(*fe);
    }

    V11MlMap m_accum{};
    if ((flags & FLAG_ACCUM) != 0u) {
        const V11Frontier* fd = p5_get_fml(ws, dst);
        if (!fd) V11_DISPATCH();
        if (fd->domain_id != out_dom) { ws.status = IMPULSE_VM_V11_ERR_DOMAIN_MISMATCH; V11_DISPATCH(); }
        m_accum = p5_ml_view(*fd);
    }

    const V11MlMap m_src = p5_ml_view(*f_src);
    const uint8_t aw = f_src->alloc_width;
    const uint8_t ac = f_src->active_lane_count;

    V11MlMap out_m{};
    bool st_flag = ws.flag_st();
    if (subop == 0u || subop == 1u || subop == 2u || subop == 7u) {
        if (!p5_lane_walk_step_once(ws.arena, subop, *rel, edge_mask, m_src, m_excl, &out_m)) {
            ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH();
        }
    } else if (subop == 3u || subop == 5u) { // fixpoint_plus
        V11MlMap visited{};
        V11MlMap frontier{};
        if (!p5_lane_walk_step_once(ws.arena, subop, *rel, edge_mask, m_src, m_excl, &frontier)) {
            ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH();
        }
        while (frontier.len > 0u) {
            V11MlMap next_vis{}, excl_u{}, next_fr{};
            if (!p5_ml_union(ws.arena, visited, frontier, &next_vis) ||
                !p5_ml_union(ws.arena, m_excl, next_vis, &excl_u) ||
                !p5_lane_walk_step_once(ws.arena, subop, *rel, edge_mask, frontier, excl_u, &next_fr)) {
                ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH();
            }
            visited = next_vis;
            frontier = next_fr;
        }
        out_m = visited;
        st_flag = true;
    } else { // 4, 6, 8, 9: fixpoint_star
        V11MlMap visited{};
        if (!p5_ml_diff(ws.arena, m_src, m_excl, &visited)) {
            ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH();
        }
        V11MlMap frontier = visited;
        while (frontier.len > 0u) {
            V11MlMap excl_u{}, next_fr{}, next_vis{};
            if (!p5_ml_union(ws.arena, m_excl, visited, &excl_u) ||
                !p5_lane_walk_step_once(ws.arena, subop, *rel, edge_mask, frontier, excl_u, &next_fr) ||
                !p5_ml_union(ws.arena, visited, next_fr, &next_vis)) {
                ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH();
            }
            visited = next_vis;
            frontier = next_fr;
        }
        out_m = visited;
        st_flag = true;
    }

    V11MlMap final_m{};
    if (!p5_ml_union(ws.arena, m_accum, out_m, &final_m)) {
        ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH();
    }
    uint64_t out_card = (out_dom < snap->domain_count) ? snap->domain_cards[out_dom] : 0ULL;
    if (!ws.f_regs[dst].init_multi_lane(ws.arena, out_dom, out_card, aw, ac, final_m.nodes, final_m.masks, final_m.len, ws.geometry)) {
        ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH();
    }
    ws.set_f_init(dst, true);
    ws.set_zf(final_m.len == 0u);
    ws.set_st(st_flag);
    ws.state.pc += 1u;
    V11_DISPATCH();
}
