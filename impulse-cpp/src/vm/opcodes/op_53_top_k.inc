// Opcode 0x53: OP_TOP_K
V11_OP_CASE(TOP_K) {
    const uint8_t subop = IMP_V11_SUBOP(w);
    const uint8_t flags = IMP_V11_FLAGS(w);
    const uint8_t dst = IMP_V11_DST(w);
    const uint8_t s1 = IMP_V11_S1(w);
    const uint8_t s2 = IMP_V11_S2(w);
    const uint8_t s3 = IMP_V11_S3(w);
    const uint16_t imm_lo = IMP_V11_IMM16_LO(w);

    const V11Frontier* mask = p4_get_mask_frontier(ws, flags, s3);
    if (ws.status != IMPULSE_VM_V11_OK) V11_DISPATCH();
    const V11Vector* v = ws.v_regs[s1];
    if (!p4_is_numeric_vec(v)) { ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; V11_DISPATCH(); }

    const uint32_t n = v->length;
    uint64_t k64 = 0ULL;
    if ((flags & FLAG_SCALAR_RHS) != 0u) {
        if (!ws.is_r_valid(s2) || static_cast<int64_t>(ws.read_r_raw(s2)) < 0) {
            ws.status = IMPULSE_VM_V11_ERR_INVALID_OPERAND; V11_DISPATCH();
        }
        k64 = ws.read_r_raw(s2);
    } else {
        k64 = static_cast<uint64_t>(imm_lo);
    }
    uint32_t k = (k64 < static_cast<uint64_t>(n)) ? static_cast<uint32_t>(k64) : n;

    uint32_t* idxs = (n > 0u) ? ws.arena.alloc_array<uint32_t>(n, 8) : nullptr;
    double*   vals = (n > 0u) ? ws.arena.alloc_array<double>(n, 8) : nullptr;
    if (n > 0u && (!idxs || !vals)) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }

    uint32_t m = 0u;
    for (uint32_t i = 0; i < n; ++i) {
        if (!p4_lane_active(mask, i) || !v->is_valid_at(i)) continue;
        double x = p4_vec_to_f64(*v, i);
        if (std::isnan(x)) continue;
        vals[i] = x;
        idxs[m++] = i;
    }

    const bool desc = (subop == subop::kTOP_K_DESC_IDS || subop == subop::kTOP_K_DESC_SCORES);
    std::sort(idxs, idxs + m, [&](uint32_t a, uint32_t b) {
        double va = vals[a], vb = vals[b];
        // Note: -0.0 == +0.0 ties broken by lowest index (matches Java 05aec70 / OCaml)
        if (desc) {
            if (va > vb) return true;
            if (va < vb) return false;
        } else {
            if (va < vb) return true;
            if (va > vb) return false;
        }
        return a < b;
    });

    uint32_t take = (k < m) ? k : m;
    auto* ov = static_cast<V11Vector*>(ws.arena.alloc(sizeof(V11Vector), alignof(V11Vector)));
    uint64_t* vbits = p4_alloc_valid_all_true(ws.arena, take);
    if (!ov || !vbits) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
    new (ov) V11Vector();
    ov->length = take;
    ov->valid_bits = vbits;

    if (subop <= 1u) {
        int64_t* out = (take > 0u) ? ws.arena.alloc_array<int64_t>(take, 8) : nullptr;
        if (take > 0u && !out) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
        for (uint32_t i = 0; i < take; ++i) out[i] = static_cast<int64_t>(idxs[i]);
        ov->elem_tag = kVecTagU64;
        ov->i64_data = out;
    } else {
        double* out = (take > 0u) ? ws.arena.alloc_array<double>(take, 8) : nullptr;
        if (take > 0u && !out) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
        for (uint32_t i = 0; i < take; ++i) out[i] = vals[idxs[i]];
        ov->elem_tag = kVecTagF64;
        ov->f64_data = out;
    }
    ws.v_regs[dst] = ov;
    ws.set_zf(take == 0u);
    ws.state.pc += 1u;
    V11_DISPATCH();
}
