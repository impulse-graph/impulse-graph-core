// Opcode 0x34: OP_SPMV
V11_OP_CASE(SPMV) {
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

    bool is_rev = (subop == 1u || subop == 3u);
    uint16_t in_dom  = is_rev ? rel->tgt_dom : rel->src_dom;
    uint16_t out_dom = is_rev ? rel->src_dom : rel->tgt_dom;
    if (f_src->domain_id != in_dom) { ws.status = IMPULSE_VM_V11_ERR_DOMAIN_MISMATCH; V11_DISPATCH(); }

    const V11Frontier* tgt_mask = nullptr;
    if ((flags & FLAG_MASKED_S3) != 0u) {
        tgt_mask = p3_get_f1bit(ws, s3);
        if (!tgt_mask) V11_DISPATCH();
        if (tgt_mask->domain_id != out_dom) { ws.status = IMPULSE_VM_V11_ERR_DOMAIN_MISMATCH; V11_DISPATCH(); }
    }

    const V11Vector* weight_col = nullptr;
    if (attr_id != 0xFFFFu) {
        weight_col = p3_find_edge_attr(*rel, attr_id);
        if (!weight_col || !p2_is_numeric_vec(weight_col)) { ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; V11_DISPATCH(); }
    }

    uint64_t n_in64  = (in_dom  < snap->domain_count) ? snap->domain_cards[in_dom]  : 0ULL;
    uint64_t n_out64 = (out_dom < snap->domain_count) ? snap->domain_cards[out_dom] : 0ULL;
    if (n_in64 > UINT32_MAX || n_out64 > UINT32_MAX) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
    uint32_t n_in  = static_cast<uint32_t>(n_in64);
    uint32_t n_out = static_cast<uint32_t>(n_out64);

    const uint32_t* src_ids = nullptr; uint32_t src_len = 0u;
    if (!f_src->materialize_1bit(ws.arena, &src_ids, &src_len)) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }

    size_t vwords = (n_out > 0u) ? ((static_cast<size_t>(n_out) + 63u) >> 6) : 1u;

    // Streaming forward SpMV path (!is_rev): avoids materializing an O(E) SpmvEdge array in arena.
    if (!is_rev) {
        const uint32_t row_off_len = rel->row_off_len;
        const uint32_t* row_offsets = rel->row_offsets;
        const uint64_t* col_targets = rel->col_targets;

        if (subop == 7u) { // .fwd_first_src
            int64_t* out = (n_out > 0u) ? ws.arena.alloc_array<int64_t>(n_out, 8) : nullptr;
            uint64_t* valid = ws.arena.alloc_array<uint64_t>(vwords, 8);
            auto* ov = static_cast<V11Vector*>(ws.arena.alloc(sizeof(V11Vector), alignof(V11Vector)));
            if ((n_out > 0u && !out) || !valid || !ov) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
            std::memset(valid, 0, vwords * sizeof(uint64_t));
            uint32_t hits = 0u;
            for (uint32_t i = 0; i < src_len; ++i) {
                uint32_t u = src_ids[i];
                if (u + 1u >= row_off_len) continue;
                uint64_t u64 = static_cast<uint64_t>(u);
                for (uint32_t e = rel->csr_row_off(u); e < rel->csr_row_off(u + 1u); ++e) {
                    uint32_t v = static_cast<uint32_t>(rel->csr_col_tgt(e));
                    if (v >= n_out || (tgt_mask && !tgt_mask->contains_1bit(v))) continue;
                    bool has = ((valid[v >> 6] >> (v & 63u)) & 1ULL) != 0ULL;
                    if (!has) {
                        out[v] = static_cast<int64_t>(u64);
                        valid[v >> 6] |= (1ULL << (v & 63u));
                        ++hits;
                    } else if (u64 < static_cast<uint64_t>(out[v])) {
                        out[v] = static_cast<int64_t>(u64);
                    }
                }
            }
            new (ov) V11Vector();
            ov->elem_tag = kVecTagU64; ov->length = n_out; ov->i64_data = out; ov->valid_bits = valid;
            ws.v_regs[dst] = ov;
            ws.set_zf(hits == 0u);
            ws.state.pc += 1u;
            V11_DISPATCH();
        }

        bool scalar_rhs = (flags & FLAG_SCALAR_RHS) != 0u;
        bool scalar_valid = scalar_rhs && ws.is_r_valid(s2);
        double scalar_val = scalar_valid ? p1_bits_to_f64(ws.read_r_raw(s2)) : std::numeric_limits<double>::quiet_NaN();
        const V11Vector* rv = nullptr;
        double* rhs_snap = nullptr;
        uint8_t* rhs_snap_valid = nullptr;
        bool direct_f64_rhs = false;
        if (!scalar_rhs) {
            rv = ws.v_regs[s2];
            if (!rv || !p2_is_numeric_vec(rv)) { ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; V11_DISPATCH(); }
            if (rv->length < n_in) { ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; V11_DISPATCH(); }
            if (rv->elem_tag == kVecTagF64 && dst != s2) {
                direct_f64_rhs = true;
            } else if (rv->length > 0u) {
                rhs_snap = ws.arena.alloc_array<double>(rv->length, 8);
                rhs_snap_valid = ws.arena.alloc_array<uint8_t>(rv->length, 8);
                if (!rhs_snap || !rhs_snap_valid) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
                for (uint32_t i = 0; i < rv->length; ++i) {
                    if (rv->is_valid_at(i)) {
                        rhs_snap_valid[i] = 1u;
                        rhs_snap[i] = p2_read_numeric_as_f64(*rv, i);
                    } else {
                        rhs_snap_valid[i] = 0u;
                    }
                }
            }
        }

        if (subop == 8u) { // .fwd_argmin_plus
            double* best_cost = (n_out > 0u) ? ws.arena.alloc_array<double>(n_out, 8) : nullptr;
            int64_t* best_src = (n_out > 0u) ? ws.arena.alloc_array<int64_t>(n_out, 8) : nullptr;
            uint64_t* valid = ws.arena.alloc_array<uint64_t>(vwords, 8);
            auto* ov = static_cast<V11Vector*>(ws.arena.alloc(sizeof(V11Vector), alignof(V11Vector)));
            if ((n_out > 0u && (!best_cost || !best_src)) || !valid || !ov) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
            std::memset(valid, 0, vwords * sizeof(uint64_t));
            uint32_t hits = 0u;
            for (uint32_t i = 0; i < src_len; ++i) {
                uint32_t u = src_ids[i];
                if (u + 1u >= row_off_len) continue;
                bool u_ok = scalar_rhs ? scalar_valid : (u < n_in && (direct_f64_rhs ? rv->is_valid_at(u) : rhs_snap_valid[u] != 0u));
                if (!u_ok) continue;
                double du = scalar_rhs ? scalar_val : (direct_f64_rhs ? rv->f64_data[u] : rhs_snap[u]);
                uint64_t u64 = static_cast<uint64_t>(u);
                for (uint32_t e = rel->csr_row_off(u); e < rel->csr_row_off(u + 1u); ++e) {
                    uint32_t v = static_cast<uint32_t>(rel->csr_col_tgt(e));
                    if (v >= n_out || (tgt_mask && !tgt_mask->contains_1bit(v))) continue;
                    if (weight_col && (e >= weight_col->length || !weight_col->is_valid_at(e))) continue;
                    double ew = weight_col ? p2_read_numeric_as_f64(*weight_col, e) : 1.0;
                    double c = du + ew;
                    if (std::isnan(c)) continue;
                    bool has = ((valid[v >> 6] >> (v & 63u)) & 1ULL) != 0ULL;
                    if (!has) {
                        best_cost[v] = c;
                        best_src[v] = static_cast<int64_t>(u64);
                        valid[v >> 6] |= (1ULL << (v & 63u));
                        ++hits;
                    } else if (c < best_cost[v] || (c == best_cost[v] && u64 < static_cast<uint64_t>(best_src[v]))) {
                        best_cost[v] = c;
                        best_src[v] = static_cast<int64_t>(u64);
                    }
                }
            }
            new (ov) V11Vector();
            ov->elem_tag = kVecTagU64; ov->length = n_out; ov->i64_data = best_src; ov->valid_bits = valid;
            ws.v_regs[dst] = ov;
            ws.set_zf(hits == 0u);
            ws.state.pc += 1u;
            V11_DISPATCH();
        }

        bool is_plus_times = (subop == 0u);
        double* out = (n_out > 0u) ? ws.arena.alloc_array<double>(n_out, 8) : nullptr;
        uint64_t* out_valid = ws.arena.alloc_array<uint64_t>(vwords, 8);
        auto* ov = static_cast<V11Vector*>(ws.arena.alloc(sizeof(V11Vector), alignof(V11Vector)));
        if ((n_out > 0u && !out) || !out_valid || !ov) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
        std::memset(out_valid, 0, vwords * sizeof(uint64_t));

        if ((flags & FLAG_ACCUM) != 0u) {
            const V11Vector* dv = ws.v_regs[dst];
            if (!dv) { ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; V11_DISPATCH(); }
            if (dv->elem_tag != kVecTagF64 || dv->length != n_out) { ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; V11_DISPATCH(); }
            if (n_out > 0u) std::memcpy(out, dv->f64_data, static_cast<size_t>(n_out) * sizeof(double));
            if (dv->valid_bits == nullptr) {
                for (uint32_t i = 0; i < n_out; ++i) out_valid[i >> 6] |= (1ULL << (i & 63u));
            } else {
                std::memcpy(out_valid, dv->valid_bits, vwords * sizeof(uint64_t));
            }
        } else if (is_plus_times) {
            if (n_out > 0u) std::memset(out, 0, static_cast<size_t>(n_out) * sizeof(double));
            for (size_t wi = 0; wi < vwords; ++wi) out_valid[wi] = ~0ULL;
            if ((n_out & 63u) != 0u) out_valid[vwords - 1u] = (1ULL << (n_out & 63u)) - 1ULL;
            else if (n_out == 0u) out_valid[0] = 0ULL;
        }

        uint32_t hits = 0u;
        if (subop == 0u && !tgt_mask && (!weight_col || weight_col->elem_tag == kVecTagF64)) {
            const double* w_data = weight_col ? weight_col->f64_data : nullptr;
            const uint64_t* w_vbits = weight_col ? weight_col->valid_bits : nullptr;
            const uint32_t w_len = weight_col ? weight_col->length : 0u;
            for (uint32_t i = 0; i < src_len; ++i) {
                uint32_t u = src_ids[i];
                if (u + 1u >= row_off_len) continue;
                bool u_ok = scalar_rhs ? scalar_valid : (u < n_in && (direct_f64_rhs ? rv->is_valid_at(u) : rhs_snap_valid[u] != 0u));
                if (!u_ok) continue;
                double ru = scalar_rhs ? scalar_val : (direct_f64_rhs ? rv->f64_data[u] : rhs_snap[u]);
                if (std::isnan(ru)) continue;
                uint32_t e0 = rel->csr_row_off(u), e1 = rel->csr_row_off(u + 1u);
                for (uint32_t e = e0; e < e1; ++e) {
                    uint32_t v = static_cast<uint32_t>(rel->csr_col_tgt(e));
                    if (v >= n_out) continue;
                    double ew = 1.0;
                    if (w_data) {
                        if (e >= w_len || (w_vbits && (((w_vbits[e >> 6] >> (e & 63u)) & 1ULL) == 0ULL))) continue;
                        ew = w_data[e];
                    }
                    double contrib = ru * ew;
                    if (std::isnan(contrib)) continue;
                    ++hits;
                    uint64_t bit = 1ULL << (v & 63u);
                    size_t wi = v >> 6;
                    double prev = (out_valid[wi] & bit) ? out[v] : 0.0;
                    double sum = prev + contrib;
                    if (std::isnan(sum)) out_valid[wi] &= ~bit;
                    else { out[v] = sum; out_valid[wi] |= bit; }
                }
            }
        } else {
            for (uint32_t i = 0; i < src_len; ++i) {
                uint32_t u = src_ids[i];
                if (u + 1u >= row_off_len) continue;
                bool u_ok = scalar_rhs ? scalar_valid : (u < n_in && (direct_f64_rhs ? rv->is_valid_at(u) : rhs_snap_valid[u] != 0u));
                if (!u_ok) continue;
                double ru = scalar_rhs ? scalar_val : (direct_f64_rhs ? rv->f64_data[u] : rhs_snap[u]);
                uint32_t e0 = rel->csr_row_off(u), e1 = rel->csr_row_off(u + 1u);
                for (uint32_t e = e0; e < e1; ++e) {
                    uint32_t v = static_cast<uint32_t>(rel->csr_col_tgt(e));
                    if (v >= n_out || (tgt_mask && !tgt_mask->contains_1bit(v))) continue;
                    if (weight_col && (e >= weight_col->length || !weight_col->is_valid_at(e))) continue;
                    double ew = weight_col ? p2_read_numeric_as_f64(*weight_col, e) : 1.0;
                    double contrib = ru;
                    if (subop == 0u || subop == 5u) contrib = ru * ew;
                    else if (subop == 2u || subop == 4u) contrib = ru + ew;
                    else if (subop == 6u) contrib = p1_ieee_max_f64(ru, ew);
                    if (std::isnan(contrib)) continue;
                    ++hits;
                    bool has = ((out_valid[v >> 6] >> (v & 63u)) & 1ULL) != 0ULL;
                    if (subop == 0u) {
                        double prev = has ? out[v] : 0.0;
                        double sum = prev + contrib;
                        if (std::isnan(sum)) out_valid[v >> 6] &= ~(1ULL << (v & 63u));
                        else { out[v] = sum; out_valid[v >> 6] |= (1ULL << (v & 63u)); }
                    } else if (subop == 2u || subop == 6u) {
                        out[v] = has ? p1_ieee_min_f64(out[v], contrib) : contrib;
                        out_valid[v >> 6] |= (1ULL << (v & 63u));
                    } else if (subop == 4u || subop == 5u) {
                        out[v] = has ? p1_ieee_max_f64(out[v], contrib) : contrib;
                        out_valid[v >> 6] |= (1ULL << (v & 63u));
                    }
                }
            }
        }

        new (ov) V11Vector();
        ov->elem_tag = kVecTagF64; ov->length = n_out; ov->f64_data = out; ov->valid_bits = out_valid;
        ws.v_regs[dst] = ov;
        ws.set_zf(hits == 0u);
        ws.state.pc += 1u;
        V11_DISPATCH();
    }

    // Reverse SpMV path (is_rev)
    struct SpmvEdge { uint32_t u; uint32_t v; uint32_t e; };
    SpmvEdge* edges = nullptr;
    uint32_t edge_cnt = 0u;
    {
        V11RevAdj rev_adj{};
        if (!p3_build_rev_adj(ws.arena, *snap, *rel, &rev_adj)) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
        size_t cap = 0u;
        for (uint32_t i = 0; i < src_len; ++i) {
            uint32_t v = src_ids[i];
            if (v < rev_adj.n_tgt) cap += (rev_adj.offsets[v + 1u] - rev_adj.offsets[v]);
        }
        if (cap > 0u) {
            edges = ws.arena.alloc_array<SpmvEdge>(cap, 8);
            if (!edges) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
            for (uint32_t i = 0; i < src_len; ++i) {
                uint32_t v = src_ids[i];
                if (v < rev_adj.n_tgt) {
                    for (uint32_t k = rev_adj.offsets[v]; k < rev_adj.offsets[v + 1u]; ++k) {
                        uint32_t u = rev_adj.srcs[k];
                        if (!tgt_mask || tgt_mask->contains_1bit(u)) edges[edge_cnt++] = {v, u, rev_adj.edges[k]};
                    }
                }
            }
        }
    }

    if (subop == 7u) { // .fwd_first_src
        int64_t* out = (n_out > 0u) ? ws.arena.alloc_array<int64_t>(n_out, 8) : nullptr;
        uint64_t* valid = ws.arena.alloc_array<uint64_t>(vwords, 8);
        auto* ov = static_cast<V11Vector*>(ws.arena.alloc(sizeof(V11Vector), alignof(V11Vector)));
        if ((n_out > 0u && !out) || !valid || !ov) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
        std::memset(valid, 0, vwords * sizeof(uint64_t));
        uint32_t hits = 0u;
        for (uint32_t i = 0; i < edge_cnt; ++i) {
            uint32_t u = edges[i].u, v = edges[i].v;
            if (v >= n_out) continue;
            uint64_t u64 = static_cast<uint64_t>(u);
            bool has = ((valid[v >> 6] >> (v & 63u)) & 1ULL) != 0ULL;
            if (!has) {
                out[v] = static_cast<int64_t>(u64);
                valid[v >> 6] |= (1ULL << (v & 63u));
                ++hits;
            } else if (u64 < static_cast<uint64_t>(out[v])) {
                out[v] = static_cast<int64_t>(u64);
            }
        }
        new (ov) V11Vector();
        ov->elem_tag = kVecTagU64; ov->length = n_out; ov->i64_data = out; ov->valid_bits = valid;
        ws.v_regs[dst] = ov;
        ws.set_zf(hits == 0u);
        ws.state.pc += 1u;
        V11_DISPATCH();
    }

    bool scalar_rhs = (flags & FLAG_SCALAR_RHS) != 0u;
    bool scalar_valid = scalar_rhs && ws.is_r_valid(s2);
    double scalar_val = scalar_valid ? p1_bits_to_f64(ws.read_r_raw(s2)) : std::numeric_limits<double>::quiet_NaN();
    double* rhs_snap = nullptr;
    uint8_t* rhs_snap_valid = nullptr;
    if (!scalar_rhs) {
        const V11Vector* rv = ws.v_regs[s2];
        if (!rv || !p2_is_numeric_vec(rv)) { ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; V11_DISPATCH(); }
        if (rv->length < n_in) { ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; V11_DISPATCH(); }
        if (rv->length > 0u) {
            rhs_snap = ws.arena.alloc_array<double>(rv->length, 8);
            rhs_snap_valid = ws.arena.alloc_array<uint8_t>(rv->length, 8);
            if (!rhs_snap || !rhs_snap_valid) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
            for (uint32_t i = 0; i < rv->length; ++i) {
                if (rv->is_valid_at(i)) {
                    rhs_snap_valid[i] = 1u;
                    rhs_snap[i] = p2_read_numeric_as_f64(*rv, i);
                } else {
                    rhs_snap_valid[i] = 0u;
                }
            }
        }
    }

    if (subop == 8u) { // .fwd_argmin_plus
        double* best_cost = (n_out > 0u) ? ws.arena.alloc_array<double>(n_out, 8) : nullptr;
        int64_t* best_src = (n_out > 0u) ? ws.arena.alloc_array<int64_t>(n_out, 8) : nullptr;
        uint64_t* valid = ws.arena.alloc_array<uint64_t>(vwords, 8);
        auto* ov = static_cast<V11Vector*>(ws.arena.alloc(sizeof(V11Vector), alignof(V11Vector)));
        if ((n_out > 0u && (!best_cost || !best_src)) || !valid || !ov) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
        std::memset(valid, 0, vwords * sizeof(uint64_t));
        uint32_t hits = 0u;
        for (uint32_t i = 0; i < edge_cnt; ++i) {
            uint32_t u = edges[i].u, v = edges[i].v, e = edges[i].e;
            if (v >= n_out) continue;
            if (scalar_rhs ? !scalar_valid : (u >= n_in || !rhs_snap_valid[u])) continue;
            double du = scalar_rhs ? scalar_val : rhs_snap[u];
            if (weight_col && (e >= weight_col->length || !weight_col->is_valid_at(e))) continue;
            double ew = weight_col ? p2_read_numeric_as_f64(*weight_col, e) : 1.0;
            double c = du + ew;
            if (std::isnan(c)) continue;
            uint64_t u64 = static_cast<uint64_t>(u);
            bool has = ((valid[v >> 6] >> (v & 63u)) & 1ULL) != 0ULL;
            if (!has) {
                best_cost[v] = c;
                best_src[v] = static_cast<int64_t>(u64);
                valid[v >> 6] |= (1ULL << (v & 63u));
                ++hits;
            } else if (c < best_cost[v] || (c == best_cost[v] && u64 < static_cast<uint64_t>(best_src[v]))) {
                best_cost[v] = c;
                best_src[v] = static_cast<int64_t>(u64);
            }
        }
        new (ov) V11Vector();
        ov->elem_tag = kVecTagU64; ov->length = n_out; ov->i64_data = best_src; ov->valid_bits = valid;
        ws.v_regs[dst] = ov;
        ws.set_zf(hits == 0u);
        ws.state.pc += 1u;
        V11_DISPATCH();
    }

    bool is_plus_times = (subop == 0u || subop == 1u);
    double* out = (n_out > 0u) ? ws.arena.alloc_array<double>(n_out, 8) : nullptr;
    uint64_t* out_valid = ws.arena.alloc_array<uint64_t>(vwords, 8);
    auto* ov = static_cast<V11Vector*>(ws.arena.alloc(sizeof(V11Vector), alignof(V11Vector)));
    if ((n_out > 0u && !out) || !out_valid || !ov) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
    std::memset(out_valid, 0, vwords * sizeof(uint64_t));

    if ((flags & FLAG_ACCUM) != 0u) {
        const V11Vector* dv = ws.v_regs[dst];
        if (!dv) { ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; V11_DISPATCH(); }
        if (dv->elem_tag != kVecTagF64 || dv->length != n_out) { ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; V11_DISPATCH(); }
        if (n_out > 0u) std::memcpy(out, dv->f64_data, static_cast<size_t>(n_out) * sizeof(double));
        for (uint32_t i = 0; i < n_out; ++i) {
            if (dv->is_valid_at(i)) out_valid[i >> 6] |= (1ULL << (i & 63u));
        }
    } else if (is_plus_times) {
        for (uint32_t i = 0; i < n_out; ++i) {
            out[i] = 0.0;
            out_valid[i >> 6] |= (1ULL << (i & 63u));
        }
    }

    uint32_t hits = 0u;
    for (uint32_t i = 0; i < edge_cnt; ++i) {
        uint32_t u = edges[i].u, v = edges[i].v, e = edges[i].e;
        if (v >= n_out) continue;
        if (scalar_rhs ? !scalar_valid : (u >= n_in || !rhs_snap_valid[u])) continue;
        double ru = scalar_rhs ? scalar_val : rhs_snap[u];
        if (weight_col && (e >= weight_col->length || !weight_col->is_valid_at(e))) continue;
        double ew = weight_col ? p2_read_numeric_as_f64(*weight_col, e) : 1.0;
        double contrib = ru;
        if (subop == 0u || subop == 1u || subop == 5u) contrib = ru * ew;
        else if (subop == 2u || subop == 3u || subop == 4u) contrib = ru + ew;
        else if (subop == 6u) contrib = p1_ieee_max_f64(ru, ew);
        if (std::isnan(contrib)) continue;
        ++hits;
        bool has = ((out_valid[v >> 6] >> (v & 63u)) & 1ULL) != 0ULL;
        if (subop == 0u || subop == 1u) {
            double prev = has ? out[v] : 0.0;
            double sum = prev + contrib;
            if (std::isnan(sum)) out_valid[v >> 6] &= ~(1ULL << (v & 63u));
            else { out[v] = sum; out_valid[v >> 6] |= (1ULL << (v & 63u)); }
        } else if (subop == 2u || subop == 3u || subop == 6u) {
            out[v] = has ? p1_ieee_min_f64(out[v], contrib) : contrib;
            out_valid[v >> 6] |= (1ULL << (v & 63u));
        } else if (subop == 4u || subop == 5u) {
            out[v] = has ? p1_ieee_max_f64(out[v], contrib) : contrib;
            out_valid[v >> 6] |= (1ULL << (v & 63u));
        }
    }

    new (ov) V11Vector();
    ov->elem_tag = kVecTagF64; ov->length = n_out; ov->f64_data = out; ov->valid_bits = out_valid;
    ws.v_regs[dst] = ov;
    ws.set_zf(hits == 0u);
    ws.state.pc += 1u;
    V11_DISPATCH();
}
