// Opcode 0x30: OP_WALK
V11_OP_CASE(WALK) {
    const uint8_t subop = IMP_V11_SUBOP(w);
    const uint8_t flags = IMP_V11_FLAGS(w);
    const uint8_t dst = IMP_V11_DST(w);
    const uint8_t s1 = IMP_V11_S1(w);
    const uint8_t s3 = IMP_V11_S3(w);
    const uint16_t rel_id = IMP_V11_IMM16_LO(w);
    const uint16_t imm_hi = IMP_V11_IMM16_HI(w);

    const V11RelView* rel = p3_get_rel(snap, ws, rel_id);
    if (!rel) V11_DISPATCH();
    const V11Frontier* f_src = p3_get_f1bit(ws, s1);
    if (!f_src) V11_DISPATCH();

    bool endo = (rel->src_dom == rel->tgt_dom);
    uint16_t in_dom  = (subop == 1u) ? rel->tgt_dom : rel->src_dom;
    uint16_t out_dom = (subop == 1u) ? rel->src_dom : rel->tgt_dom;
    if ((subop >= 2u && !endo) || f_src->domain_id != in_dom) {
        ws.status = IMPULSE_VM_V11_ERR_DOMAIN_MISMATCH;
        V11_DISPATCH();
    }
    if (subop == 7u && imm_hi < 1u) {
        ws.status = IMPULSE_VM_V11_ERR_INVALID_OPERAND;
        V11_DISPATCH();
    }

    const uint32_t* s_excl = nullptr;
    uint32_t s_excl_len = 0u;
    if ((flags & FLAG_MASKED_S3) != 0u) {
        const V11Frontier* fe = p3_get_f1bit(ws, s3);
        if (!fe) V11_DISPATCH();
        if (fe->domain_id != out_dom) { ws.status = IMPULSE_VM_V11_ERR_DOMAIN_MISMATCH; V11_DISPATCH(); }
        if (!fe->materialize_1bit(ws.arena, &s_excl, &s_excl_len)) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
    }
    const uint32_t* s_accum = nullptr;
    uint32_t s_accum_len = 0u;
    if ((flags & FLAG_ACCUM) != 0u) {
        const V11Frontier* fd = p3_get_f1bit(ws, dst);
        if (!fd) V11_DISPATCH();
        if (fd->domain_id != out_dom) { ws.status = IMPULSE_VM_V11_ERR_DOMAIN_MISMATCH; V11_DISPATCH(); }
        if (!fd->materialize_1bit(ws.arena, &s_accum, &s_accum_len)) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
    }

    const uint32_t* s_src = nullptr;
    uint32_t s_src_len = 0u;
    if (!f_src->materialize_1bit(ws.arena, &s_src, &s_src_len)) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }

    V11RevAdj rev_adj{};
    if (subop == 1u || subop == 2u || subop == 5u || subop == 6u) {
        if (!p3_build_rev_adj(ws.arena, *snap, *rel, &rev_adj)) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
    }

    uint32_t* out_s = nullptr;
    uint32_t out_len = 0u;
    bool st_flag = ws.flag_st();

    if (subop == 0u) {
        if (!p3_fwd_step(ws.arena, *rel, s_src, s_src_len, s_excl, s_excl_len, nullptr, 0u, &out_s, &out_len)) {
            ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH();
        }
    } else if (subop == 1u) {
        if (!p3_rev_step(ws.arena, rev_adj, s_src, s_src_len, s_excl, s_excl_len, nullptr, 0u, &out_s, &out_len)) {
            ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH();
        }
    } else if (subop == 2u) {
        uint32_t* f_p = nullptr; uint32_t f_n = 0u;
        uint32_t* r_p = nullptr; uint32_t r_n = 0u;
        if (!p3_fwd_step(ws.arena, *rel, s_src, s_src_len, s_excl, s_excl_len, nullptr, 0u, &f_p, &f_n) ||
            !p3_rev_step(ws.arena, rev_adj, s_src, s_src_len, s_excl, s_excl_len, nullptr, 0u, &r_p, &r_n) ||
            !p3_union_sorted(ws.arena, f_p, f_n, r_p, r_n, &out_s, &out_len)) {
            ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH();
        }
    } else if (subop == 3u || subop == 5u) { // fixpoint_plus
        uint32_t* visited = nullptr; uint32_t vis_len = 0u;
        uint32_t* frontier = nullptr; uint32_t fr_len = 0u;
        bool ok = (subop == 3u)
            ? p3_fwd_step(ws.arena, *rel, s_src, s_src_len, s_excl, s_excl_len, nullptr, 0u, &frontier, &fr_len)
            : p3_rev_step(ws.arena, rev_adj, s_src, s_src_len, s_excl, s_excl_len, nullptr, 0u, &frontier, &fr_len);
        if (!ok) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
        while (fr_len > 0u) {
            uint32_t* nv = nullptr; uint32_t nvl = 0u;
            if (!p3_union_sorted(ws.arena, visited, vis_len, frontier, fr_len, &nv, &nvl)) {
                ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH();
            }
            visited = nv; vis_len = nvl;
            uint32_t* nf = nullptr; uint32_t nfl = 0u;
            ok = (subop == 3u)
                ? p3_fwd_step(ws.arena, *rel, frontier, fr_len, s_excl, s_excl_len, visited, vis_len, &nf, &nfl)
                : p3_rev_step(ws.arena, rev_adj, frontier, fr_len, s_excl, s_excl_len, visited, vis_len, &nf, &nfl);
            if (!ok) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
            frontier = nf; fr_len = nfl;
        }
        out_s = visited; out_len = vis_len; st_flag = true;
    } else if (subop == 4u || subop == 6u) { // fixpoint_star
        uint32_t* visited = (s_src_len > 0u) ? ws.arena.alloc_array<uint32_t>(s_src_len, 8) : nullptr;
        if (s_src_len > 0u && !visited) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
        uint32_t vis_len = 0u;
        for (uint32_t i = 0; i < s_src_len; ++i) {
            if (!p3_sorted_contains(s_excl, s_excl_len, s_src[i])) visited[vis_len++] = s_src[i];
        }
        uint32_t* frontier = visited; uint32_t fr_len = vis_len;
        while (fr_len > 0u) {
            uint32_t* nf = nullptr; uint32_t nfl = 0u;
            bool ok = (subop == 4u)
                ? p3_fwd_step(ws.arena, *rel, frontier, fr_len, s_excl, s_excl_len, visited, vis_len, &nf, &nfl)
                : p3_rev_step(ws.arena, rev_adj, frontier, fr_len, s_excl, s_excl_len, visited, vis_len, &nf, &nfl);
            if (!ok) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
            if (nfl > 0u) {
                uint32_t* nv = nullptr; uint32_t nvl = 0u;
                if (!p3_union_sorted(ws.arena, visited, vis_len, nf, nfl, &nv, &nvl)) {
                    ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH();
                }
                visited = nv; vis_len = nvl;
            }
            frontier = nf; fr_len = nfl;
        }
        out_s = visited; out_len = vis_len; st_flag = true;
    } else if (subop == 7u) { // fwd_bounded k
        uint32_t* visited = nullptr; uint32_t vis_len = 0u;
        const uint32_t* frontier = s_src; uint32_t fr_len = s_src_len;
        bool converged = false;
        for (uint16_t hop = 0u; hop < imm_hi && !converged; ++hop) {
            uint32_t* nf = nullptr; uint32_t nfl = 0u;
            if (!p3_fwd_step(ws.arena, *rel, frontier, fr_len, s_excl, s_excl_len, visited, vis_len, &nf, &nfl)) {
                ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH();
            }
            if (nfl == 0u) {
                converged = true;
            } else {
                uint32_t* nv = nullptr; uint32_t nvl = 0u;
                if (!p3_union_sorted(ws.arena, visited, vis_len, nf, nfl, &nv, &nvl)) {
                    ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH();
                }
                visited = nv; vis_len = nvl;
                frontier = nf; fr_len = nfl;
            }
        }
        out_s = visited; out_len = vis_len; st_flag = converged;
    } else {
        ws.status = IMPULSE_VM_V11_ERR_INVALID_OPCODE;
        V11_DISPATCH();
    }

    uint32_t* final_s = out_s;
    uint32_t final_len = out_len;
    if (s_accum_len > 0u) {
        if (!p3_union_sorted(ws.arena, s_accum, s_accum_len, out_s, out_len, &final_s, &final_len)) {
            ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH();
        }
    }
    uint64_t out_card = (out_dom < snap->domain_count) ? snap->domain_cards[out_dom] : 0ULL;
    if (!ws.f_regs[dst].init_from_sorted_ids(ws.arena, out_dom, out_card, final_s, final_len, ws.geometry, false)) {
        ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH();
    }
    ws.set_f_init(dst, true);
    ws.set_zf(final_len == 0u);
    ws.set_st(st_flag);
    ws.state.pc += 1u;
    V11_DISPATCH();
}
