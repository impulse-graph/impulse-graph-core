// Opcode 0x69: OP_VECTOR_ANN_SEARCH
VM_OP_CASE(VECTOR_ANN_SEARCH) {
    const uint8_t subop = IMP_SUBOP(w);
    const uint8_t flags = IMP_FLAGS(w);
    const uint8_t dst = IMP_DST(w);
    const uint8_t s1 = IMP_S1(w);
    const uint8_t s2 = IMP_S2(w);
    const uint8_t s3 = IMP_S3(w);
    const uint16_t dom = IMP_IMM16_LO(w);
    const uint16_t aid = IMP_IMM16_HI(w);

    if (!snap) { ws.status = IMPULSE_VM_ERR_NULL_SNAPSHOT; VM_DISPATCH(); }
    if (subop > 1u) { ws.status = IMPULSE_VM_ERR_INVALID_OPCODE; VM_DISPATCH(); }
    double* q = nullptr; uint32_t q_len = 0u;
    if (!p4_is_numeric_vec(ws.v_regs[s1])) { ws.status = IMPULSE_VM_ERR_TYPE_MISMATCH; VM_DISPATCH(); }
    if (!p5_vec_to_f64_dense(ws.arena, ws.v_regs[s1], &q, &q_len)) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
    if (!ws.is_r_valid(s2)) { ws.status = IMPULSE_VM_ERR_INVALID_OPERAND; VM_DISPATCH(); }
    const uint64_t r2_bits = ws.read_r_raw(s2);

    const VmFrontier* mask = nullptr;
    if ((flags & FLAG_MASKED_S3) != 0u) {
        mask = p3_get_f1bit(ws, s3);
        if (!mask) VM_DISPATCH();
        if (mask->domain_id != dom) { ws.status = IMPULSE_VM_ERR_DOMAIN_MISMATCH; VM_DISPATCH(); }
    }
    const VmVector* col = p3_find_node_attr(snap, dom, aid);
    if (!col || col->elem_tag != kVecTagEmb) { ws.status = IMPULSE_VM_ERR_OUT_OF_BOUNDS; VM_DISPATCH(); }
    if (subop == 0u && static_cast<int64_t>(r2_bits) < 0) { ws.status = IMPULSE_VM_ERR_INVALID_OPERAND; VM_DISPATCH(); }

    struct ScoredId { double score; uint32_t id; };
    uint32_t n = col->length;
    ScoredId* scored = (n > 0u) ? ws.arena.alloc_array<ScoredId>(n, 8) : nullptr;
    if (n > 0u && !scored) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
    uint32_t m = 0u;
    for (uint32_t u = 0; u < n; ++u) {
        if (mask && !mask->contains_1bit(u)) continue;
        if (!col->is_valid_at(u)) continue;
        double sc = p5_sim_metric(1u, q, q_len, col->emb_rows[u], col->emb_dim);
        if (!std::isnan(sc)) scored[m++] = {sc, u};
    }
    std::sort(scored, scored + m, [](const ScoredId& a, const ScoredId& b) {
        if (a.score > b.score) return true;
        if (a.score < b.score) return false;
        return a.id < b.id;
    });

    uint32_t take = 0u;
    if (subop == 0u) {
        take = (r2_bits < static_cast<uint64_t>(m)) ? static_cast<uint32_t>(r2_bits) : m;
    } else {
        double thresh = p1_bits_to_f64(r2_bits);
        for (uint32_t i = 0; i < m; ++i) {
            if (scored[i].score >= thresh) scored[take++] = scored[i];
        }
    }

    int64_t* out = (take > 0u) ? ws.arena.alloc_array<int64_t>(take, 8) : nullptr;
    uint64_t* valid = p4_alloc_valid_all_true(ws.arena, take);
    auto* ov = static_cast<VmVector*>(ws.arena.alloc(sizeof(VmVector), alignof(VmVector)));
    if ((take > 0u && !out) || !valid || !ov) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
    for (uint32_t i = 0; i < take; ++i) out[i] = static_cast<int64_t>(scored[i].id);
    new (ov) VmVector();
    ov->elem_tag = kVecTagU64; ov->length = take; ov->i64_data = out; ov->valid_bits = valid;
    ws.v_regs[dst] = ov;
    ws.set_zf(take == 0u);
    ws.state.pc += 1u;
    VM_DISPATCH();
}
