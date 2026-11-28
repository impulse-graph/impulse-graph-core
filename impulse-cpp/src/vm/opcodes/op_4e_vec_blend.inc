// Opcode 0x4E: OP_VEC_BLEND
VM_OP_CASE(VEC_BLEND) {
    const uint8_t subop = IMP_SUBOP(w);
    const uint8_t flags = IMP_FLAGS(w);
    const uint8_t dst = IMP_DST(w);
    const uint8_t s1 = IMP_S1(w);
    const uint8_t s2 = IMP_S2(w);
    const uint8_t s3 = IMP_S3(w);

    const VmVector* cond = ws.v_regs[s1];
    if (!cond || cond->elem_tag != kVecTagBool3vl) { ws.status = IMPULSE_VM_ERR_TYPE_MISMATCH; VM_DISPATCH(); }
    const uint32_t n = cond->length;
    const bool is_case = (subop >= 2u);
    const bool t_is_r = (flags & FLAG_SCALAR_RHS) != 0u;
    const bool f_is_r = (subop & 1u) != 0u;
    const size_t vwords = (n > 0u) ? ((static_cast<size_t>(n) + 63u) >> 6) : 1u;

    const VmVector* vt = t_is_r ? nullptr : ws.v_regs[s2];
    if (!t_is_r && vt && (vt->elem_tag == kVecTagI64 || vt->elem_tag == kVecTagU64)) {
        if (vt->length != n) { ws.status = IMPULSE_VM_ERR_OUT_OF_BOUNDS; VM_DISPATCH(); }
        const VmVector* vf = nullptr;
        if (!f_is_r) {
            vf = ws.v_regs[s3];
            if (!vf || vf->elem_tag != vt->elem_tag) { ws.status = IMPULSE_VM_ERR_TYPE_MISMATCH; VM_DISPATCH(); }
            if (vf->length != n) { ws.status = IMPULSE_VM_ERR_OUT_OF_BOUNDS; VM_DISPATCH(); }
        }
        int64_t* out = (n > 0u) ? ws.arena.alloc_array<int64_t>(n, 8) : nullptr;
        uint64_t* out_valid = ws.arena.alloc_array<uint64_t>(vwords, 8);
        auto* ov = static_cast<VmVector*>(ws.arena.alloc(sizeof(VmVector), alignof(VmVector)));
        if ((n > 0u && !out) || !out_valid || !ov) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
        if (n > 0u) std::memset(out, 0, static_cast<size_t>(n) * sizeof(int64_t));
        std::memset(out_valid, 0, vwords * sizeof(uint64_t));

        bool sf_valid = f_is_r && ws.is_r_valid(s3);
        int64_t sf_val = static_cast<int64_t>(ws.read_r_raw(s3));
        for (uint32_t i = 0; i < n; ++i) {
            bool cv = cond->is_valid_at(i);
            bool cd = cv && cond->bool_data_at(i);
            if (!cv && !is_case) continue;
            if (cv && cd) {
                if (vt->is_valid_at(i)) { out[i] = vt->i64_data[i]; out_valid[i >> 6] |= (1ULL << (i & 63u)); }
            } else if (f_is_r) {
                if (sf_valid) { out[i] = sf_val; out_valid[i >> 6] |= (1ULL << (i & 63u)); }
            } else if (vf->is_valid_at(i)) {
                out[i] = vf->i64_data[i]; out_valid[i >> 6] |= (1ULL << (i & 63u));
            }
        }
        new (ov) VmVector();
        ov->elem_tag = vt->elem_tag; ov->length = n; ov->i64_data = out; ov->valid_bits = out_valid;
        ws.v_regs[dst] = ov;
        ws.state.pc += 1u;
        VM_DISPATCH();
    }

    // F64 path
    if (!t_is_r) {
        if (!p4_is_numeric_vec(vt)) { ws.status = IMPULSE_VM_ERR_TYPE_MISMATCH; VM_DISPATCH(); }
        if (vt->length != n) { ws.status = IMPULSE_VM_ERR_OUT_OF_BOUNDS; VM_DISPATCH(); }
    }
    const VmVector* vf = nullptr;
    if (!f_is_r) {
        vf = ws.v_regs[s3];
        if (!p4_is_numeric_vec(vf)) { ws.status = IMPULSE_VM_ERR_TYPE_MISMATCH; VM_DISPATCH(); }
        if (vf->length != n) { ws.status = IMPULSE_VM_ERR_OUT_OF_BOUNDS; VM_DISPATCH(); }
    }
    double* out = (n > 0u) ? ws.arena.alloc_array<double>(n, 8) : nullptr;
    uint64_t* out_valid = ws.arena.alloc_array<uint64_t>(vwords, 8);
    auto* ov = static_cast<VmVector*>(ws.arena.alloc(sizeof(VmVector), alignof(VmVector)));
    if ((n > 0u && !out) || !out_valid || !ov) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
    if (n > 0u) std::memset(out, 0, static_cast<size_t>(n) * sizeof(double));
    std::memset(out_valid, 0, vwords * sizeof(uint64_t));

    bool st_valid = t_is_r && p4_is_scalar_f64_valid(ws, s2);
    double st_val = st_valid ? p4_read_scalar_f64(ws, s2) : std::numeric_limits<double>::quiet_NaN();
    bool sf_valid = f_is_r && p4_is_scalar_f64_valid(ws, s3);
    double sf_val = sf_valid ? p4_read_scalar_f64(ws, s3) : std::numeric_limits<double>::quiet_NaN();

    for (uint32_t i = 0; i < n; ++i) {
        bool cv = cond->is_valid_at(i);
        bool cd = cv && cond->bool_data_at(i);
        if (!cv && !is_case) continue;
        if (cv && cd) {
            if (t_is_r) {
                if (st_valid) { out[i] = st_val; out_valid[i >> 6] |= (1ULL << (i & 63u)); }
            } else if (vt->is_valid_at(i)) {
                out[i] = p4_vec_to_f64(*vt, i); out_valid[i >> 6] |= (1ULL << (i & 63u));
            }
        } else {
            if (f_is_r) {
                if (sf_valid) { out[i] = sf_val; out_valid[i >> 6] |= (1ULL << (i & 63u)); }
            } else if (vf->is_valid_at(i)) {
                out[i] = p4_vec_to_f64(*vf, i); out_valid[i >> 6] |= (1ULL << (i & 63u));
            }
        }
    }
    new (ov) VmVector();
    ov->elem_tag = kVecTagF64; ov->length = n; ov->f64_data = out; ov->valid_bits = out_valid;
    ws.v_regs[dst] = ov;
    ws.state.pc += 1u;
    VM_DISPATCH();
}
