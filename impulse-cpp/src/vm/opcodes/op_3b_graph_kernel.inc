// Opcode 0x3B: OP_GRAPH_KERNEL
VM_OP_CASE(GRAPH_KERNEL) {
    const uint8_t subop = IMP_SUBOP(w);
    const uint8_t flags = IMP_FLAGS(w);
    const uint8_t dst = IMP_DST(w);
    const uint8_t s3 = IMP_S3(w);
    const uint16_t rel_id = IMP_IMM16_LO(w);
    const uint16_t imm_hi = IMP_IMM16_HI(w);

    const VmRelView* rel = p3_get_rel(snap, ws, rel_id);
    if (!rel) VM_DISPATCH();
    if (rel->src_dom != rel->tgt_dom) { ws.status = IMPULSE_VM_ERR_DOMAIN_MISMATCH; VM_DISPATCH(); }
    if (subop > 4u) { ws.status = IMPULSE_VM_ERR_INVALID_OPCODE; VM_DISPATCH(); }

    const uint16_t dom = rel->src_dom;
    const uint64_t n64 = (dom < snap->domain_count) ? snap->domain_cards[dom] : 0ULL;
    if (n64 > UINT32_MAX) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
    const uint32_t n = static_cast<uint32_t>(n64);

    const VmFrontier* f_mask = nullptr;
    if ((flags & FLAG_MASKED_S3) != 0u) {
        f_mask = p3_get_f1bit(ws, s3);
        if (!f_mask) VM_DISPATCH();
        if (f_mask->domain_id != dom) { ws.status = IMPULSE_VM_ERR_DOMAIN_MISMATCH; VM_DISPATCH(); }
    }
    auto node_in = [&](uint32_t u) noexcept -> bool {
        return f_mask == nullptr || f_mask->contains_1bit(u);
    };

    // Build simple undirected deduplicated CSR adjacency list on induced subgraph in ws.arena
    uint32_t* raw_deg = ws.arena.alloc_array<uint32_t>(static_cast<size_t>(n) + 1u, 8);
    if (!raw_deg) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
    std::memset(raw_deg, 0, (static_cast<size_t>(n) + 1u) * sizeof(uint32_t));
    uint64_t total_raw = 0ULL;
    for (uint32_t u = 0; u < n; ++u) {
        if (!node_in(u) || u + 1u >= rel->row_off_len) continue;
        for (uint32_t e = rel->csr_row_off(u); e < rel->csr_row_off(u + 1u); ++e) {
            uint64_t v64 = rel->csr_col_tgt(e);
            if (v64 < n && static_cast<uint32_t>(v64) != u && node_in(static_cast<uint32_t>(v64))) {
                uint32_t v = static_cast<uint32_t>(v64);
                raw_deg[u + 1u]++;
                raw_deg[v + 1u]++;
                total_raw += 2ULL;
            }
        }
    }
    if (total_raw > UINT32_MAX) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
    for (uint32_t u = 0; u < n; ++u) raw_deg[u + 1u] += raw_deg[u];

    uint32_t* adj_nbrs = (total_raw > 0ULL) ? ws.arena.alloc_array<uint32_t>(static_cast<size_t>(total_raw), 8) : nullptr;
    uint32_t* cur = (n > 0u) ? ws.arena.alloc_array<uint32_t>(n, 8) : nullptr;
    uint32_t* adj_len = (n > 0u) ? ws.arena.alloc_array<uint32_t>(n, 8) : nullptr;
    if ((total_raw > 0ULL && !adj_nbrs) || (n > 0u && (!cur || !adj_len))) {
        ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH();
    }
    if (n > 0u) std::memcpy(cur, raw_deg, static_cast<size_t>(n) * sizeof(uint32_t));
    for (uint32_t u = 0; u < n; ++u) {
        if (!node_in(u) || u + 1u >= rel->row_off_len) continue;
        for (uint32_t e = rel->csr_row_off(u); e < rel->csr_row_off(u + 1u); ++e) {
            uint64_t v64 = rel->csr_col_tgt(e);
            if (v64 < n && static_cast<uint32_t>(v64) != u && node_in(static_cast<uint32_t>(v64))) {
                uint32_t v = static_cast<uint32_t>(v64);
                adj_nbrs[cur[u]++] = v;
                adj_nbrs[cur[v]++] = u;
            }
        }
    }
    for (uint32_t u = 0; u < n; ++u) {
        uint32_t start = raw_deg[u];
        uint32_t count = raw_deg[u + 1u] - start;
        adj_len[u] = (count > 0u) ? p2_dedupe_sorted_inplace(adj_nbrs + start, count) : 0u;
    }

    if (subop == 0u) {
        // .cc_afforest with min-ID component canonicalization
        uint32_t* parent = (n > 0u) ? ws.arena.alloc_array<uint32_t>(n, 8) : nullptr;
        if (n > 0u && !parent) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
        for (uint32_t i = 0; i < n; ++i) parent[i] = i;
        auto uf_find = [&](uint32_t x) noexcept -> uint32_t {
            while (parent[x] != x) {
                parent[x] = parent[parent[x]];
                x = parent[x];
            }
            return x;
        };
        for (uint32_t u = 0; u < n; ++u) {
            if (!node_in(u)) continue;
            uint32_t start = raw_deg[u], cnt = adj_len[u];
            for (uint32_t k = 0; k < cnt; ++k) {
                uint32_t v = adj_nbrs[start + k];
                uint32_t ra = uf_find(u), rb = uf_find(v);
                if (ra != rb) {
                    if (ra < rb) parent[rb] = ra;
                    else parent[ra] = rb;
                }
            }
        }
        size_t vwords = (static_cast<size_t>(n) + 63u) >> 6;
        if (vwords == 0u) vwords = 1u;
        int64_t* out = (n > 0u) ? ws.arena.alloc_array<int64_t>(n, 8) : nullptr;
        uint64_t* vbits = ws.arena.alloc_array<uint64_t>(vwords, 8);
        auto* ov = static_cast<VmVector*>(ws.arena.alloc(sizeof(VmVector), alignof(VmVector)));
        if ((n > 0u && !out) || !vbits || !ov) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
        if (n > 0u) std::memset(out, 0, static_cast<size_t>(n) * sizeof(int64_t));
        std::memset(vbits, 0, vwords * sizeof(uint64_t));
        bool any = false;
        for (uint32_t u = 0; u < n; ++u) {
            if (node_in(u)) {
                out[u] = static_cast<int64_t>(uf_find(u));
                vbits[u >> 6] |= (1ULL << (u & 63u));
                any = true;
            }
        }
        new (ov) VmVector();
        ov->elem_tag = kVecTagU64; ov->length = n; ov->i64_data = out; ov->valid_bits = vbits;
        ws.v_regs[dst] = ov;
        ws.set_zf(!any);
        ws.state.pc += 1u;
    } else if (subop == 1u || subop == 2u) {
        // .kcore_nums / .kcore_frontier
        int32_t* deg = (n > 0u) ? ws.arena.alloc_array<int32_t>(n, 8) : nullptr;
        int32_t* core = (n > 0u) ? ws.arena.alloc_array<int32_t>(n, 8) : nullptr;
        uint8_t* removed = (n > 0u) ? ws.arena.alloc_array<uint8_t>(n, 8) : nullptr;
        if (n > 0u && (!deg || !core || !removed)) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
        uint32_t remaining = 0u;
        for (uint32_t u = 0; u < n; ++u) {
            core[u] = 0;
            if (!node_in(u)) {
                deg[u] = -1;
                removed[u] = 1u;
            } else {
                deg[u] = static_cast<int32_t>(adj_len[u]);
                removed[u] = 0u;
                remaining++;
            }
        }
        int32_t cur_k = 0;
        while (remaining > 0u) {
            uint32_t min_u = 0u;
            int32_t min_d = INT32_MAX;
            for (uint32_t u = 0; u < n; ++u) {
                if (!removed[u] && deg[u] < min_d) {
                    min_d = deg[u];
                    min_u = u;
                }
            }
            if (min_d > cur_k) cur_k = min_d;
            removed[min_u] = 1u;
            core[min_u] = cur_k;
            remaining--;
            uint32_t start = raw_deg[min_u], cnt = adj_len[min_u];
            for (uint32_t k = 0; k < cnt; ++k) {
                uint32_t v = adj_nbrs[start + k];
                if (!removed[v] && deg[v] > cur_k) deg[v]--;
            }
        }
        if (subop == 1u) {
            size_t vwords = (static_cast<size_t>(n) + 63u) >> 6;
            if (vwords == 0u) vwords = 1u;
            int64_t* out = (n > 0u) ? ws.arena.alloc_array<int64_t>(n, 8) : nullptr;
            uint64_t* vbits = ws.arena.alloc_array<uint64_t>(vwords, 8);
            auto* ov = static_cast<VmVector*>(ws.arena.alloc(sizeof(VmVector), alignof(VmVector)));
            if ((n > 0u && !out) || !vbits || !ov) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
            if (n > 0u) std::memset(out, 0, static_cast<size_t>(n) * sizeof(int64_t));
            std::memset(vbits, 0, vwords * sizeof(uint64_t));
            bool any_pos = false;
            for (uint32_t u = 0; u < n; ++u) {
                if (node_in(u)) {
                    out[u] = static_cast<int64_t>(core[u]);
                    vbits[u >> 6] |= (1ULL << (u & 63u));
                    if (core[u] > 0) any_pos = true;
                }
            }
            new (ov) VmVector();
            ov->elem_tag = kVecTagU64; ov->length = n; ov->i64_data = out; ov->valid_bits = vbits;
            ws.v_regs[dst] = ov;
            ws.set_zf(!any_pos);
            ws.state.pc += 1u;
        } else {
            int32_t k_thresh = static_cast<int32_t>(imm_hi);
            uint32_t* buf = (n > 0u) ? ws.arena.alloc_array<uint32_t>(n, 8) : nullptr;
            if (n > 0u && !buf) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
            uint32_t cnt = 0u;
            for (uint32_t u = 0; u < n; ++u) {
                if (node_in(u) && core[u] >= k_thresh) buf[cnt++] = u;
            }
            if (!ws.f_regs[dst].init_from_sorted_ids(ws.arena, dom, n64, buf, cnt, ws.geometry, true)) {
                ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH();
            }
            ws.set_f_init(dst, true);
            ws.set_zf(cnt == 0u);
            ws.state.pc += 1u;
        }
    } else {
        // .tri_count_total (3) / .tri_count_vec (4)
        uint64_t* tri_u = (n > 0u) ? ws.arena.alloc_array<uint64_t>(n, 8) : nullptr;
        if (n > 0u && !tri_u) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
        if (n > 0u) std::memset(tri_u, 0, static_cast<size_t>(n) * sizeof(uint64_t));
        uint64_t total_tri = 0ULL;
        for (uint32_t u = 0; u < n; ++u) {
            if (!node_in(u)) continue;
            uint32_t su = raw_deg[u], lu = adj_len[u];
            for (uint32_t iv = 0; iv < lu; ++iv) {
                uint32_t v = adj_nbrs[su + iv];
                if (v <= u) continue;
                uint32_t sv = raw_deg[v], lv = adj_len[v];
                for (uint32_t iw = iv + 1u; iw < lu; ++iw) {
                    uint32_t wn = adj_nbrs[su + iw];
                    if (wn > v && p3_sorted_contains(adj_nbrs + sv, lv, wn)) {
                        total_tri++;
                        tri_u[u]++;
                        tri_u[v]++;
                        tri_u[wn]++;
                    }
                }
            }
        }
        if (subop == 3u) {
            ws.write_r(dst, static_cast<uint8_t>(ScalarType::SCALAR_U64), total_tri);
            p1_set_scalar_flags(ws, true, total_tri);
            ws.state.pc += 1u;
        } else {
            size_t vwords = (static_cast<size_t>(n) + 63u) >> 6;
            if (vwords == 0u) vwords = 1u;
            int64_t* out = (n > 0u) ? ws.arena.alloc_array<int64_t>(n, 8) : nullptr;
            uint64_t* vbits = ws.arena.alloc_array<uint64_t>(vwords, 8);
            auto* ov = static_cast<VmVector*>(ws.arena.alloc(sizeof(VmVector), alignof(VmVector)));
            if ((n > 0u && !out) || !vbits || !ov) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
            if (n > 0u) std::memset(out, 0, static_cast<size_t>(n) * sizeof(int64_t));
            std::memset(vbits, 0, vwords * sizeof(uint64_t));
            for (uint32_t u = 0; u < n; ++u) {
                if (node_in(u)) {
                    out[u] = static_cast<int64_t>(tri_u[u]);
                    vbits[u >> 6] |= (1ULL << (u & 63u));
                }
            }
            new (ov) VmVector();
            ov->elem_tag = kVecTagU64; ov->length = n; ov->i64_data = out; ov->valid_bits = vbits;
            ws.v_regs[dst] = ov;
            ws.set_zf(total_tri == 0ULL);
            ws.state.pc += 1u;
        }
    }
    VM_DISPATCH();
}
