// Opcode 0x6A: OP_VECTOR_RERANK
V11_OP_CASE(VECTOR_RERANK) {
    const uint8_t subop = IMP_V11_SUBOP(w);
    const uint8_t flags = IMP_V11_FLAGS(w);
    const uint8_t dst = IMP_V11_DST(w);
    const uint8_t s1 = IMP_V11_S1(w);
    const uint8_t s2 = IMP_V11_S2(w);
    const uint8_t s3 = IMP_V11_S3(w);
    const uint16_t dom = IMP_V11_IMM16_LO(w);
    const uint16_t aid = IMP_V11_IMM16_HI(w);

    if (!snap) { ws.status = IMPULSE_VM_V11_ERR_NULL_SNAPSHOT; V11_DISPATCH(); }
    if (subop > 7u) { ws.status = IMPULSE_VM_V11_ERR_INVALID_OPCODE; V11_DISPATCH(); }
    const V11Vector* col = p3_find_node_attr(snap, dom, aid);
    if (!col || col->elem_tag != kVecTagEmb) { ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; V11_DISPATCH(); }

    const V11Frontier* mask = nullptr;
    if ((flags & FLAG_MASKED_S3) != 0u) {
        mask = p3_get_f1bit(ws, s3);
        if (!mask) V11_DISPATCH();
    }
    const uint8_t metric = subop & 3u;

    if (subop <= 3u) {
        const V11Vector* ids = ws.v_regs[s1];
        if (!ids || (ids->elem_tag != kVecTagU64 && ids->elem_tag != kVecTagI64) || !p4_is_numeric_vec(ws.v_regs[s2])) {
            ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; V11_DISPATCH();
        }
        double* q = nullptr; uint32_t q_len = 0u;
        if (!p5_vec_to_f64_dense(ws.arena, ws.v_regs[s2], &q, &q_len)) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
        uint32_t n = ids->length;
        size_t vwords = (n > 0u) ? ((static_cast<size_t>(n) + 63u) >> 6) : 1u;
        double* out = (n > 0u) ? ws.arena.alloc_array<double>(n, 8) : nullptr;
        uint64_t* valid = ws.arena.alloc_array<uint64_t>(vwords, 8);
        auto* ov = static_cast<V11Vector*>(ws.arena.alloc(sizeof(V11Vector), alignof(V11Vector)));
        if ((n > 0u && !out) || !valid || !ov) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
        if (n > 0u) std::memset(out, 0, static_cast<size_t>(n) * sizeof(double));
        std::memset(valid, 0, vwords * sizeof(uint64_t));
        bool any = false;
        for (uint32_t i = 0; i < n; ++i) {
            if (mask && !mask->contains_1bit(i)) continue;
            if (!ids->is_valid_at(i)) continue;
            int64_t u_s = ids->i64_data[i];
            if (u_s < 0 || static_cast<uint64_t>(u_s) >= col->length) { ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; V11_DISPATCH(); }
            uint32_t u = static_cast<uint32_t>(u_s);
            if (col->is_valid_at(u)) {
                double sc = p5_sim_metric(metric, q, q_len, col->emb_rows[u], col->emb_dim);
                if (!std::isnan(sc)) {
                    out[i] = sc;
                    valid[i >> 6] |= (1ULL << (i & 63u));
                    any = true;
                }
            }
        }
        new (ov) V11Vector();
        ov->elem_tag = kVecTagF64; ov->length = n; ov->f64_data = out; ov->valid_bits = valid;
        ws.v_regs[dst] = ov;
        ws.set_zf(!any);
        ws.state.pc += 1u;
    } else {
        const V11Vector* va = ws.v_regs[s1];
        const V11Vector* vb = ws.v_regs[s2];
        if (!va || !vb ||
            (va->elem_tag != kVecTagU64 && va->elem_tag != kVecTagI64) ||
            (vb->elem_tag != kVecTagU64 && vb->elem_tag != kVecTagI64) ||
            va->length != vb->length) {
            ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; V11_DISPATCH();
        }
        uint32_t n = va->length;
        size_t vwords = (n > 0u) ? ((static_cast<size_t>(n) + 63u) >> 6) : 1u;
        double* out = (n > 0u) ? ws.arena.alloc_array<double>(n, 8) : nullptr;
        uint64_t* valid = ws.arena.alloc_array<uint64_t>(vwords, 8);
        auto* ov = static_cast<V11Vector*>(ws.arena.alloc(sizeof(V11Vector), alignof(V11Vector)));
        if ((n > 0u && !out) || !valid || !ov) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
        if (n > 0u) std::memset(out, 0, static_cast<size_t>(n) * sizeof(double));
        std::memset(valid, 0, vwords * sizeof(uint64_t));
        bool any = false;
        for (uint32_t i = 0; i < n; ++i) {
            if (mask && !mask->contains_1bit(i)) continue;
            if (!va->is_valid_at(i) || !vb->is_valid_at(i)) continue;
            int64_t u_s = va->i64_data[i];
            int64_t v_s = vb->i64_data[i];
            if (u_s < 0 || v_s < 0 || static_cast<uint64_t>(u_s) >= col->length || static_cast<uint64_t>(v_s) >= col->length) {
                ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; V11_DISPATCH();
            }
            uint32_t u = static_cast<uint32_t>(u_s), v = static_cast<uint32_t>(v_s);
            if (col->is_valid_at(u) && col->is_valid_at(v)) {
                double sc = p5_sim_metric(metric, col->emb_rows[u], col->emb_dim, col->emb_rows[v], col->emb_dim);
                if (!std::isnan(sc)) {
                    out[i] = sc;
                    valid[i >> 6] |= (1ULL << (i & 63u));
                    any = true;
                }
            }
        }
        new (ov) V11Vector();
        ov->elem_tag = kVecTagF64; ov->length = n; ov->f64_data = out; ov->valid_bits = valid;
        ws.v_regs[dst] = ov;
        ws.set_zf(!any);
        ws.state.pc += 1u;
    }
    V11_DISPATCH();
}
