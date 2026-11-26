// Opcode 0x45: OP_VEC_CMP
V11_OP_CASE(VEC_CMP) {
    const uint8_t subop = IMP_V11_SUBOP(w);
    const uint8_t flags = IMP_V11_FLAGS(w);
    const uint8_t dst = IMP_V11_DST(w);
    const uint8_t s1 = IMP_V11_S1(w);
    const uint8_t s2 = IMP_V11_S2(w);
    const uint8_t s3 = IMP_V11_S3(w);

    const V11Vector* va = ws.v_regs[s1];
    if (!va) { ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; V11_DISPATCH(); }
    const uint32_t n = va->length;
    const size_t vwords = (n > 0u) ? ((static_cast<size_t>(n) + 63u) >> 6) : 1u;

    const V11Frontier* mask = (subop == 6u) ? nullptr : p4_get_mask_frontier(ws, flags, s3);
    if (ws.status != IMPULSE_VM_V11_OK) V11_DISPATCH();

    if (subop == 7u) { // .in_set
        if ((va->elem_tag != kVecTagI64 && va->elem_tag != kVecTagU64) ||
            !ws.is_f_init(s2) || ws.f_regs[s2].multi_lane) {
            ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; V11_DISPATCH();
        }
        uint64_t* out_valid = ws.arena.alloc_array<uint64_t>(vwords, 8);
        uint64_t* out_data  = ws.arena.alloc_array<uint64_t>(vwords, 8);
        auto* ov = static_cast<V11Vector*>(ws.arena.alloc(sizeof(V11Vector), alignof(V11Vector)));
        if (!out_valid || !out_data || !ov) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
        std::memset(out_valid, 0, vwords * sizeof(uint64_t));
        std::memset(out_data, 0, vwords * sizeof(uint64_t));
        const V11Frontier& fset = ws.f_regs[s2];
        for (uint32_t i = 0; i < n; ++i) {
            if (!p4_lane_active(mask, i) || !va->is_valid_at(i)) continue;
            int64_t u = va->i64_data[i];
            bool mem = (u >= 0 && static_cast<uint64_t>(u) < fset.domain_card && fset.contains_1bit(static_cast<uint32_t>(u)));
            uint64_t bit = 1ULL << (i & 63u);
            out_valid[i >> 6] |= bit;
            if (mem) out_data[i >> 6] |= bit;
        }
        new (ov) V11Vector();
        ov->elem_tag = kVecTagBool3vl; ov->length = n; ov->valid_bits = out_valid; ov->bool_data_bits = out_data;
        ws.v_regs[dst] = ov;
        ws.state.pc += 1u;
        V11_DISPATCH();
    }

    const bool scalar_rhs = (flags & FLAG_SCALAR_RHS) != 0u;

    if (va->elem_tag == kVecTagF64) {
        const V11Vector* vb = nullptr;
        const V11Vector* vc = nullptr;
        if (!scalar_rhs) {
            vb = ws.v_regs[s2];
            if (!p4_is_numeric_vec(vb)) { ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; V11_DISPATCH(); }
            if (vb->length != n) { ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; V11_DISPATCH(); }
            if (subop == 6u) {
                vc = ws.v_regs[s3];
                if (!p4_is_numeric_vec(vc)) { ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; V11_DISPATCH(); }
                if (vc->length != n) { ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; V11_DISPATCH(); }
            }
        }
        uint64_t* out_valid = ws.arena.alloc_array<uint64_t>(vwords, 8);
        uint64_t* out_data  = ws.arena.alloc_array<uint64_t>(vwords, 8);
        auto* ov = static_cast<V11Vector*>(ws.arena.alloc(sizeof(V11Vector), alignof(V11Vector)));
        if (!out_valid || !out_data || !ov) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
        std::memset(out_valid, 0, vwords * sizeof(uint64_t));
        std::memset(out_data, 0, vwords * sizeof(uint64_t));

        bool sb_valid = scalar_rhs && p4_is_scalar_f64_valid(ws, s2);
        double sb_val = sb_valid ? p4_read_scalar_f64(ws, s2) : std::numeric_limits<double>::quiet_NaN();
        bool sc_valid = scalar_rhs && subop == 6u && p4_is_scalar_f64_valid(ws, s3);
        double sc_val = sc_valid ? p4_read_scalar_f64(ws, s3) : std::numeric_limits<double>::quiet_NaN();

        if (!mask && subop <= 5u && n > 0u &&
            (scalar_rhs || (vb && vb->elem_tag == kVecTagF64))) {
            simd::f64_cmp_words(subop, va->f64_data, vb ? vb->f64_data : nullptr,
                                scalar_rhs, sb_val, sb_valid,
                                va->valid_bits, vb ? vb->valid_bits : nullptr,
                                out_valid, out_data, n);
        } else for (uint32_t i = 0; i < n; ++i) {
            if (!p4_lane_active(mask, i) || !va->is_valid_at(i)) continue;
            double x = va->f64_data[i];
            if (std::isnan(x)) continue;
            if (scalar_rhs ? !sb_valid : !vb->is_valid_at(i)) continue;
            double y = scalar_rhs ? sb_val : p4_vec_to_f64(*vb, i);
            if (std::isnan(y)) continue;
            bool ok = false;
            if (subop == 6u) {
                if (scalar_rhs ? !sc_valid : !vc->is_valid_at(i)) continue;
                double hi = scalar_rhs ? sc_val : p4_vec_to_f64(*vc, i);
                if (std::isnan(hi)) continue;
                ok = (x >= y && x <= hi);
            } else {
                ok = p4_eval_cmp_f64(subop, x, y);
            }
            uint64_t bit = 1ULL << (i & 63u);
            out_valid[i >> 6] |= bit;
            if (ok) out_data[i >> 6] |= bit;
        }
        new (ov) V11Vector();
        ov->elem_tag = kVecTagBool3vl; ov->length = n; ov->valid_bits = out_valid; ov->bool_data_bits = out_data;
        ws.v_regs[dst] = ov;
        ws.state.pc += 1u;
        V11_DISPATCH();
    }

    if (va->elem_tag == kVecTagI64 || va->elem_tag == kVecTagU64) {
        bool is_u = (va->elem_tag == kVecTagU64);
        const V11Vector* vb = nullptr;
        const V11Vector* vc = nullptr;
        if (!scalar_rhs) {
            vb = ws.v_regs[s2];
            if (!vb || (vb->elem_tag != kVecTagI64 && vb->elem_tag != kVecTagU64)) {
                ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; V11_DISPATCH();
            }
            if (vb->length != n) { ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; V11_DISPATCH(); }
            if (subop == 6u) {
                vc = ws.v_regs[s3];
                if (!vc || (vc->elem_tag != kVecTagI64 && vc->elem_tag != kVecTagU64)) {
                    ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; V11_DISPATCH();
                }
                if (vc->length != n) { ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; V11_DISPATCH(); }
            }
        }
        uint64_t* out_valid = ws.arena.alloc_array<uint64_t>(vwords, 8);
        uint64_t* out_data  = ws.arena.alloc_array<uint64_t>(vwords, 8);
        auto* ov = static_cast<V11Vector*>(ws.arena.alloc(sizeof(V11Vector), alignof(V11Vector)));
        if (!out_valid || !out_data || !ov) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
        std::memset(out_valid, 0, vwords * sizeof(uint64_t));
        std::memset(out_data, 0, vwords * sizeof(uint64_t));

        bool sb_valid = scalar_rhs && ws.is_r_valid(s2);
        int64_t sb_val = static_cast<int64_t>(ws.read_r_raw(s2));
        bool sc_valid = scalar_rhs && subop == 6u && ws.is_r_valid(s3);
        int64_t sc_val = static_cast<int64_t>(ws.read_r_raw(s3));

        for (uint32_t i = 0; i < n; ++i) {
            if (!p4_lane_active(mask, i) || !va->is_valid_at(i)) continue;
            int64_t x = va->i64_data[i];
            if (scalar_rhs ? !sb_valid : !vb->is_valid_at(i)) continue;
            int64_t y = scalar_rhs ? sb_val : vb->i64_data[i];
            bool ok = false;
            if (subop == 6u) {
                if (scalar_rhs ? !sc_valid : !vc->is_valid_at(i)) continue;
                int64_t hi = scalar_rhs ? sc_val : vc->i64_data[i];
                if (is_u) {
                    uint64_t xu = static_cast<uint64_t>(x), yu = static_cast<uint64_t>(y), hiu = static_cast<uint64_t>(hi);
                    ok = (xu >= yu && xu <= hiu);
                } else {
                    ok = (x >= y && x <= hi);
                }
            } else {
                int cmp = 0;
                if (is_u) {
                    uint64_t xu = static_cast<uint64_t>(x), yu = static_cast<uint64_t>(y);
                    cmp = (xu < yu) ? -1 : (xu > yu ? 1 : 0);
                } else {
                    cmp = (x < y) ? -1 : (x > y ? 1 : 0);
                }
                ok = p4_eval_cmp_op(subop, cmp);
            }
            uint64_t bit = 1ULL << (i & 63u);
            out_valid[i >> 6] |= bit;
            if (ok) out_data[i >> 6] |= bit;
        }
        new (ov) V11Vector();
        ov->elem_tag = kVecTagBool3vl; ov->length = n; ov->valid_bits = out_valid; ov->bool_data_bits = out_data;
        ws.v_regs[dst] = ov;
        ws.state.pc += 1u;
        V11_DISPATCH();
    }

    ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH;
    V11_DISPATCH();
}
