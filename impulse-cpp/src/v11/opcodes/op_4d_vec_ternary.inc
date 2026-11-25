// Opcode 0x4D: OP_VEC_TERNARY
V11_OP_CASE(VEC_TERNARY) {
    const uint8_t subop = IMP_V11_SUBOP(w);
    const uint8_t flags = IMP_V11_FLAGS(w);
    const uint8_t dst = IMP_V11_DST(w);
    const uint8_t s1 = IMP_V11_S1(w);
    const uint8_t s2 = IMP_V11_S2(w);
    const uint8_t s3 = IMP_V11_S3(w);

    if (subop == 8u) { // .time_valid_at_r
        const V11Vector* v_start = ws.v_regs[s1];
        const V11Vector* v_end   = ws.v_regs[s2];
        if (!v_start || !v_end ||
            (v_start->elem_tag != kVecTagI64 && v_start->elem_tag != kVecTagU64) ||
            (v_end->elem_tag != kVecTagI64 && v_end->elem_tag != kVecTagU64)) {
            ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; V11_DISPATCH();
        }
        if (v_start->length != v_end->length) { ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; V11_DISPATCH(); }
        uint32_t n = v_start->length;
        size_t vwords = (n > 0u) ? ((static_cast<size_t>(n) + 63u) >> 6) : 1u;
        uint64_t* out_valid = ws.arena.alloc_array<uint64_t>(vwords, 8);
        uint64_t* out_data  = ws.arena.alloc_array<uint64_t>(vwords, 8);
        auto* ov = static_cast<V11Vector*>(ws.arena.alloc(sizeof(V11Vector), alignof(V11Vector)));
        if (!out_valid || !out_data || !ov) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
        std::memset(out_valid, 0, vwords * sizeof(uint64_t));
        std::memset(out_data, 0, vwords * sizeof(uint64_t));
        if (ws.is_r_valid(s3)) {
            int64_t ts = static_cast<int64_t>(ws.read_r_raw(s3));
            for (uint32_t i = 0; i < n; ++i) {
                if (!v_start->is_valid_at(i)) continue;
                int64_t st = v_start->i64_data[i];
                bool ok = (ts >= st) && (!v_end->is_valid_at(i) || ts < v_end->i64_data[i]);
                uint64_t bit = 1ULL << (i & 63u);
                out_valid[i >> 6] |= bit;
                if (ok) out_data[i >> 6] |= bit;
            }
        }
        new (ov) V11Vector();
        ov->elem_tag = kVecTagBool3vl; ov->length = n; ov->valid_bits = out_valid; ov->bool_data_bits = out_data;
        ws.v_regs[dst] = ov;
        ws.state.pc += 1u;
        V11_DISPATCH();
    }

    if (subop == 9u) { // .mul_div_i64_r
        const V11Vector* va = ws.v_regs[s1];
        if (!va || (va->elem_tag != kVecTagI64 && va->elem_tag != kVecTagU64)) {
            ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; V11_DISPATCH();
        }
        uint32_t n = va->length;
        bool scalar_b = (flags & FLAG_SCALAR_RHS) != 0u;
        const V11Vector* vb = nullptr;
        if (!scalar_b) {
            vb = ws.v_regs[s2];
            if (!vb || (vb->elem_tag != kVecTagI64 && vb->elem_tag != kVecTagU64)) {
                ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; V11_DISPATCH();
            }
            if (vb->length != n) { ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; V11_DISPATCH(); }
        }
        size_t vwords = (n > 0u) ? ((static_cast<size_t>(n) + 63u) >> 6) : 1u;
        int64_t* out = (n > 0u) ? ws.arena.alloc_array<int64_t>(n, 8) : nullptr;
        uint64_t* out_valid = ws.arena.alloc_array<uint64_t>(vwords, 8);
        auto* ov = static_cast<V11Vector*>(ws.arena.alloc(sizeof(V11Vector), alignof(V11Vector)));
        if ((n > 0u && !out) || !out_valid || !ov) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
        if (n > 0u) std::memset(out, 0, static_cast<size_t>(n) * sizeof(int64_t));
        std::memset(out_valid, 0, vwords * sizeof(uint64_t));

        bool b_sc_valid = scalar_b && ws.is_r_valid(s2);
        int64_t b_sc = static_cast<int64_t>(ws.read_r_raw(s2));
        bool d_valid = ws.is_r_valid(s3);
        int64_t d = static_cast<int64_t>(ws.read_r_raw(s3));
        if (d_valid && d != 0) {
            for (uint32_t i = 0; i < n; ++i) {
                if (!va->is_valid_at(i)) continue;
                if (scalar_b ? !b_sc_valid : !vb->is_valid_at(i)) continue;
                int64_t b_val = scalar_b ? b_sc : vb->i64_data[i];
                int64_t q = 0;
                if (p4_i128_mul_div(va->i64_data[i], b_val, d, &q)) {
                    out[i] = q;
                    out_valid[i >> 6] |= (1ULL << (i & 63u));
                }
            }
        }
        new (ov) V11Vector();
        ov->elem_tag = kVecTagI64; ov->length = n; ov->i64_data = out; ov->valid_bits = out_valid;
        ws.v_regs[dst] = ov;
        ws.state.pc += 1u;
        V11_DISPATCH();
    }

    // F64 ternary ops: 0/1 fma, 2/3 lerp, 4/5 clamp, 6/7 safe_div
    const V11Vector* va = ws.v_regs[s1];
    if (!p4_is_numeric_vec(va)) { ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; V11_DISPATCH(); }
    const uint32_t n = va->length;
    const bool s2_is_r = (flags & FLAG_SCALAR_RHS) != 0u;
    const bool s3_is_r = (subop & 1u) != 0u;
    const V11Vector* vb = nullptr;
    const V11Vector* vc = nullptr;
    if (!s2_is_r) {
        vb = ws.v_regs[s2];
        if (!p4_is_numeric_vec(vb)) { ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; V11_DISPATCH(); }
        if (vb->length != n) { ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; V11_DISPATCH(); }
    }
    if (!s3_is_r) {
        vc = ws.v_regs[s3];
        if (!p4_is_numeric_vec(vc)) { ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; V11_DISPATCH(); }
        if (vc->length != n) { ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; V11_DISPATCH(); }
    }
    const bool sb_valid = s2_is_r && p4_is_scalar_f64_valid(ws, s2);
    const bool sc_valid = s3_is_r && p4_is_scalar_f64_valid(ws, s3);
    const double sb_val = sb_valid ? p4_read_scalar_f64(ws, s2) : std::numeric_limits<double>::quiet_NaN();
    const double sc_val = sc_valid ? p4_read_scalar_f64(ws, s3) : std::numeric_limits<double>::quiet_NaN();

    const size_t vwords = (n > 0u) ? ((static_cast<size_t>(n) + 63u) >> 6) : 1u;
    double* out = (n > 0u) ? ws.arena.alloc_array<double>(n, 8) : nullptr;
    uint64_t* out_valid = ws.arena.alloc_array<uint64_t>(vwords, 8);
    auto* ov = static_cast<V11Vector*>(ws.arena.alloc(sizeof(V11Vector), alignof(V11Vector)));
    if ((n > 0u && !out) || !out_valid || !ov) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
    if (n > 0u) std::memset(out, 0, static_cast<size_t>(n) * sizeof(double));
    std::memset(out_valid, 0, vwords * sizeof(uint64_t));

    for (uint32_t i = 0; i < n; ++i) {
        bool a_valid = va->is_valid_at(i);
        bool b_valid = s2_is_r ? sb_valid : vb->is_valid_at(i);
        bool c_valid = s3_is_r ? sc_valid : vc->is_valid_at(i);
        double a = a_valid ? p4_vec_to_f64(*va, i) : std::numeric_limits<double>::quiet_NaN();
        double b = b_valid ? (s2_is_r ? sb_val : p4_vec_to_f64(*vb, i)) : std::numeric_limits<double>::quiet_NaN();
        double c = c_valid ? (s3_is_r ? sc_val : p4_vec_to_f64(*vc, i)) : std::numeric_limits<double>::quiet_NaN();

        double res = std::numeric_limits<double>::quiet_NaN();
        switch (subop >> 1) {
            case 0: // FMA
                if (!a_valid || !b_valid || !c_valid || std::isnan(a) || std::isnan(b) || std::isnan(c)) continue;
                res = std::fma(a, b, c);
                break;
            case 1: // LERP: fma(t, b - a, a)
                if (!a_valid || !b_valid || !c_valid || std::isnan(a) || std::isnan(b) || std::isnan(c)) continue;
                res = std::fma(c, b - a, a);
                break;
            case 2: // CLAMP(a, lo=b, hi=c)
                if (!a_valid || !b_valid || !c_valid || std::isnan(a) || std::isnan(b) || std::isnan(c)) continue;
                if (b > c) { ws.status = IMPULSE_VM_V11_ERR_INVALID_OPERAND; V11_DISPATCH(); }
                res = p1_ieee_min_f64(c, p1_ieee_max_f64(b, a));
                break;
            case 3: // SAFE_DIV
                if (!b_valid || std::isnan(b) || b == 0.0) {
                    if (!c_valid || std::isnan(c)) continue;
                    res = c;
                } else {
                    if (!a_valid || std::isnan(a)) continue;
                    res = a / b;
                }
                break;
            default: break;
        }
        if (!std::isnan(res)) {
            out[i] = res;
            out_valid[i >> 6] |= (1ULL << (i & 63u));
        }
    }
    new (ov) V11Vector();
    ov->elem_tag = kVecTagF64; ov->length = n; ov->f64_data = out; ov->valid_bits = out_valid;
    ws.v_regs[dst] = ov;
    ws.state.pc += 1u;
    V11_DISPATCH();
}
