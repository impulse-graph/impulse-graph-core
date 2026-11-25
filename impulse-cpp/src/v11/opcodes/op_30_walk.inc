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

    uint64_t out_card = (out_dom < snap->domain_count) ? snap->domain_cards[out_dom] : 0ULL;

    // Fast dense bitset + flat-queue BFS path for forward walks (subops 0, 3, 4, 7)
    // when target domain fits in <= 4M nodes; rewinds arena & falls back to sparse path on tight arena.
    if ((subop == 0u || subop == 3u || subop == 4u || subop == 7u) &&
        out_card > 0ULL && out_card <= (1ULL << 22)) {
        const uint32_t n_out = static_cast<uint32_t>(out_card);
        const size_t n_words = (static_cast<size_t>(n_out) + 63u) >> 6;
        const size_t saved_mark = ws.arena.pos;
        uint64_t* vis_bits = ws.arena.alloc_array<uint64_t>(n_words, 64);
        uint64_t* excl_bits = (s_excl_len > 0u) ? ws.arena.alloc_array<uint64_t>(n_words, 64) : nullptr;
        uint32_t* queue = (subop != 0u) ? ws.arena.alloc_array<uint32_t>(n_out, 64) : nullptr;
        if (vis_bits && (s_excl_len == 0u || excl_bits) && (subop == 0u || queue)) {
            std::memset(vis_bits, 0, n_words * sizeof(uint64_t));
            if (excl_bits) {
                std::memset(excl_bits, 0, n_words * sizeof(uint64_t));
                for (uint32_t i = 0; i < s_excl_len; ++i) {
                    uint32_t x = s_excl[i];
                    if (x < n_out) excl_bits[x >> 6] |= (1ULL << (x & 63u));
                }
            }
            const uint32_t row_off_len = rel->row_off_len;
            bool st_flag = ws.flag_st();
            uint32_t vis_cnt = 0u;

            if (subop == 0u) {
                for (uint32_t i = 0; i < s_src_len; ++i) {
                    uint32_t u = s_src[i];
                    if (u + 1u >= row_off_len) continue;
                    uint32_t e0 = rel->csr_row_off(u), e1 = rel->csr_row_off(u + 1u);
                    for (uint32_t e = e0; e < e1; ++e) {
                        uint32_t v = static_cast<uint32_t>(rel->csr_col_tgt(e));
                        if (v >= n_out) continue;
                        uint64_t bit = 1ULL << (v & 63u);
                        size_t wi = v >> 6;
                        if (excl_bits && (excl_bits[wi] & bit)) continue;
                        if ((vis_bits[wi] & bit) == 0ULL) {
                            vis_bits[wi] |= bit;
                            ++vis_cnt;
                        }
                    }
                }
            } else if (subop == 3u) { // fixpoint_plus
                uint32_t q_head = 0u, q_tail = 0u;
                for (uint32_t i = 0; i < s_src_len; ++i) {
                    uint32_t u = s_src[i];
                    if (u + 1u >= row_off_len) continue;
                    uint32_t e0 = rel->csr_row_off(u), e1 = rel->csr_row_off(u + 1u);
                    for (uint32_t e = e0; e < e1; ++e) {
                        uint32_t v = static_cast<uint32_t>(rel->csr_col_tgt(e));
                        if (v >= n_out) continue;
                        uint64_t bit = 1ULL << (v & 63u);
                        size_t wi = v >> 6;
                        if (excl_bits && (excl_bits[wi] & bit)) continue;
                        if ((vis_bits[wi] & bit) == 0ULL) {
                            vis_bits[wi] |= bit;
                            queue[q_tail++] = v;
                        }
                    }
                }
                while (q_head < q_tail) {
                    uint32_t u = queue[q_head++];
                    if (u + 1u >= row_off_len) continue;
                    uint32_t e0 = rel->csr_row_off(u), e1 = rel->csr_row_off(u + 1u);
                    for (uint32_t e = e0; e < e1; ++e) {
                        uint32_t v = static_cast<uint32_t>(rel->csr_col_tgt(e));
                        if (v >= n_out) continue;
                        uint64_t bit = 1ULL << (v & 63u);
                        size_t wi = v >> 6;
                        if (excl_bits && (excl_bits[wi] & bit)) continue;
                        if ((vis_bits[wi] & bit) == 0ULL) {
                            vis_bits[wi] |= bit;
                            queue[q_tail++] = v;
                        }
                    }
                }
                vis_cnt = q_tail;
                st_flag = true;
            } else if (subop == 4u) { // fixpoint_star
                uint32_t q_head = 0u, q_tail = 0u;
                for (uint32_t i = 0; i < s_src_len; ++i) {
                    uint32_t u = s_src[i];
                    if (u >= n_out) continue;
                    uint64_t bit = 1ULL << (u & 63u);
                    size_t wi = u >> 6;
                    if (excl_bits && (excl_bits[wi] & bit)) continue;
                    if ((vis_bits[wi] & bit) == 0ULL) {
                        vis_bits[wi] |= bit;
                        queue[q_tail++] = u;
                    }
                }
                while (q_head < q_tail) {
                    uint32_t u = queue[q_head++];
                    if (u + 1u >= row_off_len) continue;
                    uint32_t e0 = rel->csr_row_off(u), e1 = rel->csr_row_off(u + 1u);
                    for (uint32_t e = e0; e < e1; ++e) {
                        uint32_t v = static_cast<uint32_t>(rel->csr_col_tgt(e));
                        if (v >= n_out) continue;
                        uint64_t bit = 1ULL << (v & 63u);
                        size_t wi = v >> 6;
                        if (excl_bits && (excl_bits[wi] & bit)) continue;
                        if ((vis_bits[wi] & bit) == 0ULL) {
                            vis_bits[wi] |= bit;
                            queue[q_tail++] = v;
                        }
                    }
                }
                vis_cnt = q_tail;
                st_flag = true;
            } else { // subop == 7u: fwd_bounded k
                uint32_t q_head = 0u, q_tail = 0u;
                bool converged = false;
                // Hop 0 expands from s_src
                for (uint32_t i = 0; i < s_src_len; ++i) {
                    uint32_t u = s_src[i];
                    if (u + 1u >= row_off_len) continue;
                    uint32_t e0 = rel->csr_row_off(u), e1 = rel->csr_row_off(u + 1u);
                    for (uint32_t e = e0; e < e1; ++e) {
                        uint32_t v = static_cast<uint32_t>(rel->csr_col_tgt(e));
                        if (v >= n_out) continue;
                        uint64_t bit = 1ULL << (v & 63u);
                        size_t wi = v >> 6;
                        if (excl_bits && (excl_bits[wi] & bit)) continue;
                        if ((vis_bits[wi] & bit) == 0ULL) {
                            vis_bits[wi] |= bit;
                            queue[q_tail++] = v;
                        }
                    }
                }
                if (q_tail == 0u) {
                    converged = true;
                } else {
                    for (uint16_t hop = 1u; hop < imm_hi; ++hop) {
                        uint32_t level_end = q_tail;
                        while (q_head < level_end) {
                            uint32_t u = queue[q_head++];
                            if (u + 1u >= row_off_len) continue;
                            uint32_t e0 = rel->csr_row_off(u), e1 = rel->csr_row_off(u + 1u);
                            for (uint32_t e = e0; e < e1; ++e) {
                                uint32_t v = static_cast<uint32_t>(rel->csr_col_tgt(e));
                                if (v >= n_out) continue;
                                uint64_t bit = 1ULL << (v & 63u);
                                size_t wi = v >> 6;
                                if (excl_bits && (excl_bits[wi] & bit)) continue;
                                if ((vis_bits[wi] & bit) == 0ULL) {
                                    vis_bits[wi] |= bit;
                                    queue[q_tail++] = v;
                                }
                            }
                        }
                        if (q_tail == level_end) {
                            converged = true;
                            break;
                        }
                    }
                }
                vis_cnt = q_tail;
                st_flag = converged;
            }

            if (s_accum_len > 0u) {
                for (uint32_t i = 0; i < s_accum_len; ++i) {
                    uint32_t a = s_accum[i];
                    if (a < n_out) {
                        uint64_t bit = 1ULL << (a & 63u);
                        size_t wi = a >> 6;
                        if ((vis_bits[wi] & bit) == 0ULL) {
                            vis_bits[wi] |= bit;
                            ++vis_cnt;
                        }
                    }
                }
            }

            uint32_t* final_s = (vis_cnt > 0u) ? ws.arena.alloc_array<uint32_t>(vis_cnt, 8) : nullptr;
            if (vis_cnt > 0u && !final_s) {
                // Tight arena: rewind and let sparse path handle (or report OOM cleanly)
                ws.arena.pos = saved_mark;
            } else {
                uint32_t k = 0u;
                for (size_t wi = 0; wi < n_words; ++wi) {
                    uint64_t word = vis_bits[wi];
                    uint32_t base = static_cast<uint32_t>(wi << 6);
                    while (word != 0ULL) {
                        uint32_t b = static_cast<uint32_t>(__builtin_ctzll(word));
                        final_s[k++] = base + b;
                        word &= (word - 1ULL);
                    }
                }
                ws.f_regs[dst].reset_empty(out_dom, out_card);
                if (k > 0u) {
                    ws.f_regs[dst].nodes = final_s;
                    ws.f_regs[dst].node_count = k;
                    ws.f_regs[dst].tier = compute_tier(ws.geometry, out_card, k, false);
                    ws.f_regs[dst].cached_card = (ws.f_regs[dst].tier == kTier0Inline || ws.f_regs[dst].tier == kTierAllSpan)
                        ? static_cast<int64_t>(k) : -1;
                }
                ws.set_f_init(dst, true);
                ws.set_zf(k == 0u);
                ws.set_st(st_flag);
                ws.state.pc += 1u;
                V11_DISPATCH();
            }
        } else {
            ws.arena.pos = saved_mark;
        }
    }

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
    if (!ws.f_regs[dst].init_from_sorted_ids(ws.arena, out_dom, out_card, final_s, final_len, ws.geometry, false)) {
        ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH();
    }
    ws.set_f_init(dst, true);
    ws.set_zf(final_len == 0u);
    ws.set_st(st_flag);
    ws.state.pc += 1u;
    V11_DISPATCH();
}
