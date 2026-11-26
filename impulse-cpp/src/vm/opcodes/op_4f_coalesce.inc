// Opcode 0x4F: OP_COALESCE
V11_OP_CASE(COALESCE) {
    const uint8_t flags = IMP_V11_FLAGS(w);
    const uint8_t dst = IMP_V11_DST(w);
    const uint8_t s1 = IMP_V11_S1(w);
    const uint8_t s2 = IMP_V11_S2(w);
    const uint8_t s3 = IMP_V11_S3(w);

    const V11Frontier* mask = p4_get_mask_frontier(ws, flags, s3);
    if (ws.status != IMPULSE_VM_V11_OK) V11_DISPATCH();
    const V11Vector* va = ws.v_regs[s1];
    if (!va) { ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; V11_DISPATCH(); }

    const uint32_t n = va->length;
    const bool scalar_rhs = (flags & FLAG_SCALAR_RHS) != 0u;
    const V11Vector* vb = nullptr;
    if (!scalar_rhs) {
        vb = ws.v_regs[s2];
        if (!vb) { ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; V11_DISPATCH(); }
    }
    const size_t vwords = (n > 0u) ? ((static_cast<size_t>(n) + 63u) >> 6) : 1u;

    switch (va->elem_tag) {
        case kVecTagI64:
        case kVecTagU64: {
            if (!scalar_rhs && vb->elem_tag != kVecTagI64 && vb->elem_tag != kVecTagU64) {
                ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; V11_DISPATCH();
            }
            if (!scalar_rhs && vb->length != n) { ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; V11_DISPATCH(); }
            int64_t* out = (n > 0u) ? ws.arena.alloc_array<int64_t>(n, 8) : nullptr;
            uint64_t* out_valid = ws.arena.alloc_array<uint64_t>(vwords, 8);
            auto* ov = static_cast<V11Vector*>(ws.arena.alloc(sizeof(V11Vector), alignof(V11Vector)));
            if ((n > 0u && !out) || !out_valid || !ov) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
            if (n > 0u) std::memset(out, 0, static_cast<size_t>(n) * sizeof(int64_t));
            std::memset(out_valid, 0, vwords * sizeof(uint64_t));
            bool sv = scalar_rhs && ws.is_r_valid(s2);
            int64_t sr = static_cast<int64_t>(ws.read_r_raw(s2));
            for (uint32_t i = 0; i < n; ++i) {
                if (!p4_lane_active(mask, i)) continue;
                if (va->is_valid_at(i)) {
                    out[i] = va->i64_data[i]; out_valid[i >> 6] |= (1ULL << (i & 63u));
                } else if (scalar_rhs) {
                    if (sv) { out[i] = sr; out_valid[i >> 6] |= (1ULL << (i & 63u)); }
                } else if (vb->is_valid_at(i)) {
                    out[i] = vb->i64_data[i]; out_valid[i >> 6] |= (1ULL << (i & 63u));
                }
            }
            new (ov) V11Vector();
            ov->elem_tag = va->elem_tag; ov->length = n; ov->i64_data = out; ov->valid_bits = out_valid;
            ws.v_regs[dst] = ov;
            break;
        }
        case kVecTagF64: {
            if (!scalar_rhs && !p4_is_numeric_vec(vb)) { ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; V11_DISPATCH(); }
            if (!scalar_rhs && vb->length != n) { ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; V11_DISPATCH(); }
            double* out = (n > 0u) ? ws.arena.alloc_array<double>(n, 8) : nullptr;
            uint64_t* out_valid = ws.arena.alloc_array<uint64_t>(vwords, 8);
            auto* ov = static_cast<V11Vector*>(ws.arena.alloc(sizeof(V11Vector), alignof(V11Vector)));
            if ((n > 0u && !out) || !out_valid || !ov) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
            if (n > 0u) std::memset(out, 0, static_cast<size_t>(n) * sizeof(double));
            std::memset(out_valid, 0, vwords * sizeof(uint64_t));
            bool sv = scalar_rhs && p4_is_scalar_f64_valid(ws, s2);
            double sr = sv ? p4_read_scalar_f64(ws, s2) : std::numeric_limits<double>::quiet_NaN();
            for (uint32_t i = 0; i < n; ++i) {
                if (!p4_lane_active(mask, i)) continue;
                if (va->is_valid_at(i)) {
                    out[i] = va->f64_data[i]; out_valid[i >> 6] |= (1ULL << (i & 63u));
                } else if (scalar_rhs) {
                    if (sv) { out[i] = sr; out_valid[i >> 6] |= (1ULL << (i & 63u)); }
                } else if (vb->is_valid_at(i)) {
                    out[i] = p4_vec_to_f64(*vb, i); out_valid[i >> 6] |= (1ULL << (i & 63u));
                }
            }
            new (ov) V11Vector();
            ov->elem_tag = kVecTagF64; ov->length = n; ov->f64_data = out; ov->valid_bits = out_valid;
            ws.v_regs[dst] = ov;
            break;
        }
        case kVecTagBool3vl: {
            if (!scalar_rhs && vb->elem_tag != kVecTagBool3vl) { ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; V11_DISPATCH(); }
            if (!scalar_rhs && vb->length != n) { ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; V11_DISPATCH(); }
            uint64_t* out_valid = ws.arena.alloc_array<uint64_t>(vwords, 8);
            uint64_t* out_data  = ws.arena.alloc_array<uint64_t>(vwords, 8);
            auto* ov = static_cast<V11Vector*>(ws.arena.alloc(sizeof(V11Vector), alignof(V11Vector)));
            if (!out_valid || !out_data || !ov) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
            std::memset(out_valid, 0, vwords * sizeof(uint64_t));
            std::memset(out_data, 0, vwords * sizeof(uint64_t));
            bool sv = scalar_rhs && ws.is_r_valid(s2);
            bool sr = sv && (ws.read_r_raw(s2) != 0ULL);
            for (uint32_t i = 0; i < n; ++i) {
                if (!p4_lane_active(mask, i)) continue;
                uint64_t bit = 1ULL << (i & 63u);
                if (va->is_valid_at(i)) {
                    out_valid[i >> 6] |= bit;
                    if (va->bool_data_at(i)) out_data[i >> 6] |= bit;
                } else if (scalar_rhs) {
                    if (sv) { out_valid[i >> 6] |= bit; if (sr) out_data[i >> 6] |= bit; }
                } else if (vb->is_valid_at(i)) {
                    out_valid[i >> 6] |= bit;
                    if (vb->bool_data_at(i)) out_data[i >> 6] |= bit;
                }
            }
            new (ov) V11Vector();
            ov->elem_tag = kVecTagBool3vl; ov->length = n; ov->valid_bits = out_valid; ov->bool_data_bits = out_data;
            ws.v_regs[dst] = ov;
            break;
        }
        case kVecTagStr: {
            if (!scalar_rhs && vb->elem_tag != kVecTagStr) { ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; V11_DISPATCH(); }
            if (!scalar_rhs && vb->length != n) { ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; V11_DISPATCH(); }
            V11StrSlice* out = (n > 0u) ? ws.arena.alloc_array<V11StrSlice>(n, 8) : nullptr;
            auto* ov = static_cast<V11Vector*>(ws.arena.alloc(sizeof(V11Vector), alignof(V11Vector)));
            if ((n > 0u && !out) || !ov) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
            V11StrSlice sr = (scalar_rhs && ws.is_r_valid(s2)) ? ws.resolve_scalar_str(ws.read_r_raw(s2)) : V11StrSlice{nullptr, 0u, 0u};
            for (uint32_t i = 0; i < n; ++i) {
                out[i] = {nullptr, 0u, 0u};
                if (!p4_lane_active(mask, i)) continue;
                out[i] = va->is_valid_at(i) ? va->str_data[i] : (scalar_rhs ? sr : vb->str_data[i]);
            }
            new (ov) V11Vector();
            ov->elem_tag = kVecTagStr; ov->length = n; ov->str_data = out;
            ws.v_regs[dst] = ov;
            break;
        }
        default:
            ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH;
            V11_DISPATCH();
    }
    ws.state.pc += 1u;
    V11_DISPATCH();
}
