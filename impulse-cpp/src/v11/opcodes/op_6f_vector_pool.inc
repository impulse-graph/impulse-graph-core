// Opcode 0x6F: OP_VECTOR_POOL
V11_OP_CASE(VECTOR_POOL) {
    const uint8_t subop = IMP_V11_SUBOP(w);
    const uint8_t dst = IMP_V11_DST(w);
    const uint8_t s1 = IMP_V11_S1(w);
    const uint16_t dom = IMP_V11_IMM16_LO(w);
    const uint16_t aid = IMP_V11_IMM16_HI(w);

    if (subop == 3u) { // .l2_norm
        const V11Vector* v = ws.v_regs[s1];
        if (!p4_is_numeric_vec(v)) { ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; V11_DISPATCH(); }
        uint32_t n = v->length;
        double norm_sq = 0.0;
        for (uint32_t i = 0; i < n; ++i) {
            if (!v->is_valid_at(i)) continue;
            double x = p4_vec_to_f64(*v, i);
            norm_sq += x * x;
        }
        double norm = std::sqrt(norm_sq);
        size_t vwords = (n > 0u) ? ((static_cast<size_t>(n) + 63u) >> 6) : 1u;
        double* out = (n > 0u) ? ws.arena.alloc_array<double>(n, 8) : nullptr;
        uint64_t* valid = (norm == 0.0) ? p4_alloc_valid_all_true(ws.arena, n) : ws.arena.alloc_array<uint64_t>(vwords, 8);
        auto* ov = static_cast<V11Vector*>(ws.arena.alloc(sizeof(V11Vector), alignof(V11Vector)));
        if ((n > 0u && !out) || !valid || !ov) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
        if (n > 0u) std::memset(out, 0, static_cast<size_t>(n) * sizeof(double));
        if (norm != 0.0) {
            std::memset(valid, 0, vwords * sizeof(uint64_t));
            for (uint32_t i = 0; i < n; ++i) {
                if (!v->is_valid_at(i)) continue;
                out[i] = p4_vec_to_f64(*v, i) / norm;
                valid[i >> 6] |= (1ULL << (i & 63u));
            }
        }
        new (ov) V11Vector();
        ov->elem_tag = kVecTagF64; ov->length = n; ov->f64_data = out; ov->valid_bits = valid;
        ws.v_regs[dst] = ov;
        ws.set_zf(norm == 0.0);
        ws.state.pc += 1u;
        V11_DISPATCH();
    }

    if (subop > 2u) { ws.status = IMPULSE_VM_V11_ERR_INVALID_OPCODE; V11_DISPATCH(); }
    if (!snap) { ws.status = IMPULSE_VM_V11_ERR_NULL_SNAPSHOT; V11_DISPATCH(); }
    const V11Vector* col = p3_find_node_attr(snap, dom, aid);
    if (!col || col->elem_tag != kVecTagEmb) { ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; V11_DISPATCH(); }
    const uint32_t dim = col->emb_dim;

    if (subop == 0u) { // .load_node
        if (!ws.is_r_valid(s1)) { ws.status = IMPULSE_VM_V11_ERR_INVALID_OPERAND; V11_DISPATCH(); }
        int64_t u_s = static_cast<int64_t>(ws.read_r_raw(s1));
        if (u_s < 0 || static_cast<uint64_t>(u_s) >= col->length) { ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; V11_DISPATCH(); }
        uint32_t u = static_cast<uint32_t>(u_s);
        size_t vwords = (dim > 0u) ? ((static_cast<size_t>(dim) + 63u) >> 6) : 1u;
        double* out = (dim > 0u) ? ws.arena.alloc_array<double>(dim, 8) : nullptr;
        uint64_t* valid = col->is_valid_at(u) ? p4_alloc_valid_all_true(ws.arena, dim) : ws.arena.alloc_array<uint64_t>(vwords, 8);
        auto* ov = static_cast<V11Vector*>(ws.arena.alloc(sizeof(V11Vector), alignof(V11Vector)));
        if ((dim > 0u && !out) || !valid || !ov) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
        if (!col->is_valid_at(u)) {
            if (dim > 0u) std::memset(out, 0, static_cast<size_t>(dim) * sizeof(double));
            std::memset(valid, 0, vwords * sizeof(uint64_t));
            ws.set_zf(true);
        } else {
            if (dim > 0u) std::memcpy(out, col->emb_rows[u], static_cast<size_t>(dim) * sizeof(double));
            ws.set_zf(false);
        }
        new (ov) V11Vector();
        ov->elem_tag = kVecTagF64; ov->length = dim; ov->f64_data = out; ov->valid_bits = valid;
        ws.v_regs[dst] = ov;
        ws.state.pc += 1u;
        V11_DISPATCH();
    }

    // 1 = .mean_pool, 2 = .sum_pool
    const V11Frontier* fa = p3_get_f1bit(ws, s1);
    if (!fa) V11_DISPATCH();
    if (fa->domain_id != dom) { ws.status = IMPULSE_VM_V11_ERR_DOMAIN_MISMATCH; V11_DISPATCH(); }
    const uint32_t* ids = nullptr; uint32_t id_len = 0u;
    if (!fa->materialize_1bit(ws.arena, &ids, &id_len)) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }

    double* acc = (dim > 0u) ? ws.arena.alloc_array<double>(dim, 8) : nullptr;
    uint64_t* valid = p4_alloc_valid_all_true(ws.arena, dim);
    auto* ov = static_cast<V11Vector*>(ws.arena.alloc(sizeof(V11Vector), alignof(V11Vector)));
    if ((dim > 0u && !acc) || !valid || !ov) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
    if (dim > 0u) std::memset(acc, 0, static_cast<size_t>(dim) * sizeof(double));

    uint32_t cnt = 0u;
    for (uint32_t k = 0; k < id_len; ++k) {
        uint32_t u = ids[k];
        if (u < col->length && col->is_valid_at(u)) {
            cnt++;
            const double* r = col->emb_rows[u];
            for (uint32_t i = 0; i < dim; ++i) acc[i] += r[i];
        }
    }
    if (cnt > 0u && subop == 1u) {
        double inv = 1.0 / static_cast<double>(cnt);
        for (uint32_t i = 0; i < dim; ++i) acc[i] *= inv;
    }
    new (ov) V11Vector();
    ov->elem_tag = kVecTagF64; ov->length = dim; ov->f64_data = acc; ov->valid_bits = valid;
    ws.v_regs[dst] = ov;
    ws.set_zf(cnt == 0u);
    ws.state.pc += 1u;
    V11_DISPATCH();
}
