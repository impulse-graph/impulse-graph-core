// Opcode 0x39: OP_SAMPLE_NEIGHBORS
V11_OP_CASE(SAMPLE_NEIGHBORS) {
    const uint8_t subop = IMP_V11_SUBOP(w);
    const uint8_t dst = IMP_V11_DST(w);
    const uint8_t s1 = IMP_V11_S1(w);
    const uint8_t s2 = IMP_V11_S2(w);
    const uint16_t rel_id = IMP_V11_IMM16_LO(w);
    const uint16_t k = IMP_V11_IMM16_HI(w);

    const V11RelView* rel = p3_get_rel(snap, ws, rel_id);
    if (!rel) V11_DISPATCH();
    if (!ws.is_r_valid(s2)) { ws.status = IMPULSE_VM_V11_ERR_INVALID_OPERAND; V11_DISPATCH(); }
    const uint64_t seed = ws.read_r_raw(s2);

    if (subop == 0u) {
        // .frontier_wor: SplitMix64 + Floyd's O(k) without replacement
        const V11Frontier* f_src = p3_get_f1bit(ws, s1);
        if (!f_src) V11_DISPATCH();
        if (f_src->domain_id != rel->src_dom) { ws.status = IMPULSE_VM_V11_ERR_DOMAIN_MISMATCH; V11_DISPATCH(); }
        if (k == 0u) { ws.status = IMPULSE_VM_V11_ERR_INVALID_OPERAND; V11_DISPATCH(); }

        const uint32_t* src_ids = nullptr; uint32_t src_len = 0u;
        if (!f_src->materialize_1bit(ws.arena, &src_ids, &src_len)) {
            ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH();
        }
        size_t cap = 0u;
        for (uint32_t i = 0; i < src_len; ++i) {
            uint32_t u = src_ids[i];
            if (u + 1u < rel->row_off_len) {
                uint32_t deg = rel->row_offsets[u + 1u] - rel->row_offsets[u];
                cap += (deg <= k) ? deg : k;
            }
        }
        uint32_t* out_buf = (cap > 0u) ? ws.arena.alloc_array<uint32_t>(cap, 8) : nullptr;
        if (cap > 0u && !out_buf) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }

        // Lazy-allocate Floyd open-addressing hash table only if some node has deg > k
        size_t ht_cap = 1u;
        while (ht_cap < static_cast<size_t>(k) * 2u) ht_cap <<= 1;
        uint32_t* ht = nullptr;
        const size_t ht_mask = ht_cap - 1u;

        uint32_t cnt = 0u;
        for (uint32_t i = 0; i < src_len; ++i) {
            uint32_t u = src_ids[i];
            if (rel->row_off_len == 0u || u >= rel->row_off_len - 1u) continue;
            uint32_t lo = rel->row_offsets[u], hi = rel->row_offsets[u + 1u];
            uint32_t deg = hi - lo;
            if (deg <= k) {
                for (uint32_t e = lo; e < hi; ++e) out_buf[cnt++] = static_cast<uint32_t>(rel->col_targets[e]);
            } else {
                if (!ht) {
                    ht = ws.arena.alloc_array<uint32_t>(ht_cap, 8);
                    if (!ht) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
                }
                std::memset(ht, 0xFF, ht_cap * sizeof(uint32_t));
                for (uint32_t t = deg - k; t < deg; ++t) {
                    uint64_t r = p3_splitmix_sample(seed, static_cast<uint64_t>(u), static_cast<uint64_t>(t));
                    uint32_t p = static_cast<uint32_t>(r % (static_cast<uint64_t>(t) + 1ULL));
                    // Probe hash table for p
                    size_t slot = (static_cast<size_t>(p) * 2654435761u) & ht_mask;
                    bool found = false;
                    while (ht[slot] != UINT32_MAX) {
                        if (ht[slot] == p) { found = true; break; }
                        slot = (slot + 1u) & ht_mask;
                    }
                    uint32_t pick = found ? t : p;
                    // Insert pick
                    size_t islot = (static_cast<size_t>(pick) * 2654435761u) & ht_mask;
                    while (ht[islot] != UINT32_MAX && ht[islot] != pick) islot = (islot + 1u) & ht_mask;
                    ht[islot] = pick;
                    out_buf[cnt++] = static_cast<uint32_t>(rel->col_targets[lo + pick]);
                }
            }
        }
        uint32_t unique_cnt = (cnt > 0u) ? p2_dedupe_sorted_inplace(out_buf, cnt) : 0u;
        uint64_t tgt_card = snap->domain_cards[rel->tgt_dom];
        if (!ws.f_regs[dst].init_from_sorted_ids(ws.arena, rel->tgt_dom, tgt_card, out_buf, unique_cnt, ws.geometry, false)) {
            ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH();
        }
        ws.set_f_init(dst, true);
        ws.set_zf(unique_cnt == 0u);
        ws.state.pc += 1u;
    } else if (subop == 1u) {
        // .vec_wr: with replacement, len = len(V[s1]) * k
        const V11Vector* seeds = ws.v_regs[s1];
        if (!seeds || (seeds->elem_tag != kVecTagU64 && seeds->elem_tag != kVecTagI64)) {
            ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; V11_DISPATCH();
        }
        if (k == 0u) { ws.status = IMPULSE_VM_V11_ERR_INVALID_OPERAND; V11_DISPATCH(); }
        uint32_t n = seeds->length;
        uint64_t out_len64 = static_cast<uint64_t>(n) * static_cast<uint64_t>(k);
        if (out_len64 > UINT32_MAX) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
        uint32_t out_len = static_cast<uint32_t>(out_len64);

        size_t vwords = (static_cast<size_t>(out_len) + 63u) >> 6;
        if (vwords == 0u) vwords = 1u;
        int64_t* out = (out_len > 0u) ? ws.arena.alloc_array<int64_t>(out_len, 8) : nullptr;
        uint64_t* vbits = ws.arena.alloc_array<uint64_t>(vwords, 8);
        auto* ov = static_cast<V11Vector*>(ws.arena.alloc(sizeof(V11Vector), alignof(V11Vector)));
        if ((out_len > 0u && !out) || !vbits || !ov) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
        if (out_len > 0u) std::memset(out, 0, static_cast<size_t>(out_len) * sizeof(int64_t));
        std::memset(vbits, 0, vwords * sizeof(uint64_t));

        bool any_valid = false;
        uint64_t dcard = (rel->src_dom < snap->domain_count) ? snap->domain_cards[rel->src_dom] : 0ULL;
        for (uint32_t i = 0; i < n; ++i) {
            if (!seeds->is_valid_at(i)) continue;
            int64_t u_s = seeds->i64_data[i];
            uint64_t u64 = static_cast<uint64_t>(u_s);
            if (u_s < 0 || u64 >= dcard) { ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; V11_DISPATCH(); }
            uint32_t u = static_cast<uint32_t>(u64);
            uint32_t lo = (rel->row_off_len > 0u && u < rel->row_off_len - 1u) ? rel->row_offsets[u] : 0u;
            uint32_t hi = (rel->row_off_len > 0u && u < rel->row_off_len - 1u) ? rel->row_offsets[u + 1u] : 0u;
            uint32_t deg = hi - lo;
            if (deg > 0u) {
                for (uint32_t j = 0; j < k; ++j) {
                    uint64_t r = p3_splitmix_sample(seed, u64, static_cast<uint64_t>(j));
                    uint32_t idx = static_cast<uint32_t>(r % static_cast<uint64_t>(deg));
                    uint32_t pos = i * k + j;
                    out[pos] = static_cast<int64_t>(rel->col_targets[lo + idx]);
                    vbits[pos >> 6] |= (1ULL << (pos & 63u));
                    any_valid = true;
                }
            }
        }
        new (ov) V11Vector();
        ov->elem_tag = kVecTagU64; ov->length = out_len; ov->i64_data = out; ov->valid_bits = vbits;
        ws.v_regs[dst] = ov;
        ws.set_zf(!any_valid);
        ws.state.pc += 1u;
    } else if (subop == 2u || subop == 3u) {
        // .rw_step_fwd / .rw_step_stay
        const V11Vector* walkers = ws.v_regs[s1];
        if (!walkers || (walkers->elem_tag != kVecTagU64 && walkers->elem_tag != kVecTagI64)) {
            ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; V11_DISPATCH();
        }
        bool stay = (subop == 3u);
        uint32_t n = walkers->length;
        size_t vwords = (static_cast<size_t>(n) + 63u) >> 6;
        if (vwords == 0u) vwords = 1u;
        int64_t* out = (n > 0u) ? ws.arena.alloc_array<int64_t>(n, 8) : nullptr;
        uint64_t* vbits = ws.arena.alloc_array<uint64_t>(vwords, 8);
        auto* ov = static_cast<V11Vector*>(ws.arena.alloc(sizeof(V11Vector), alignof(V11Vector)));
        if ((n > 0u && !out) || !vbits || !ov) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
        if (n > 0u) std::memset(out, 0, static_cast<size_t>(n) * sizeof(int64_t));
        std::memset(vbits, 0, vwords * sizeof(uint64_t));

        bool any_valid = false;
        uint64_t dcard = (rel->src_dom < snap->domain_count) ? snap->domain_cards[rel->src_dom] : 0ULL;
        for (uint32_t i = 0; i < n; ++i) {
            if (!walkers->is_valid_at(i)) continue;
            int64_t u_s = walkers->i64_data[i];
            uint64_t u64 = static_cast<uint64_t>(u_s);
            if (u_s < 0 || u64 >= dcard) { ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; V11_DISPATCH(); }
            uint32_t u = static_cast<uint32_t>(u64);
            uint32_t lo = (rel->row_off_len > 0u && u < rel->row_off_len - 1u) ? rel->row_offsets[u] : 0u;
            uint32_t hi = (rel->row_off_len > 0u && u < rel->row_off_len - 1u) ? rel->row_offsets[u + 1u] : 0u;
            uint32_t deg = hi - lo;
            if (deg == 0u) {
                if (stay) {
                    out[i] = u_s;
                    vbits[i >> 6] |= (1ULL << (i & 63u));
                    any_valid = true;
                }
            } else {
                uint64_t r = p3_splitmix_sample(seed, u64, static_cast<uint64_t>(i));
                uint32_t idx = static_cast<uint32_t>(r % static_cast<uint64_t>(deg));
                out[i] = static_cast<int64_t>(rel->col_targets[lo + idx]);
                vbits[i >> 6] |= (1ULL << (i & 63u));
                any_valid = true;
            }
        }
        new (ov) V11Vector();
        ov->elem_tag = kVecTagU64; ov->length = n; ov->i64_data = out; ov->valid_bits = vbits;
        ws.v_regs[dst] = ov;
        ws.set_zf(!any_valid);
        ws.state.pc += 1u;
    } else {
        ws.status = IMPULSE_VM_V11_ERR_INVALID_OPCODE;
    }
    V11_DISPATCH();
}
