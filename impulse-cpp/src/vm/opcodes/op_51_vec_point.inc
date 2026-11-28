// Opcode 0x51: OP_VEC_POINT
VM_OP_CASE(VEC_POINT) {
    const uint8_t subop = IMP_SUBOP(w);
    const uint8_t dst = IMP_DST(w);
    const uint8_t s1 = IMP_S1(w);
    const uint8_t s2 = IMP_S2(w);
    const uint16_t imm_lo = IMP_IMM16_LO(w);

    switch (subop) {
        case subop::kVEC_POINT_VEC_GET: {
            const VmVector* v = ws.v_regs[s1];
            if (!v) { ws.status = IMPULSE_VM_ERR_TYPE_MISMATCH; VM_DISPATCH(); }
            if (!ws.is_r_valid(s2)) {
                ws.write_r_null(dst);
                ws.set_zf(true);
                ws.set_nf(true);
                ws.state.pc += 1u;
                VM_DISPATCH();
            }
            int64_t idx = static_cast<int64_t>(ws.read_r_raw(s2));
            if (idx < 0 || static_cast<uint64_t>(idx) >= v->length) {
                ws.status = IMPULSE_VM_ERR_OUT_OF_BOUNDS; VM_DISPATCH();
            }
            p4_write_vec_elem_to_scalar(ws, dst, *v, static_cast<uint32_t>(idx));
            if (ws.status == IMPULSE_VM_OK) ws.state.pc += 1u;
            break;
        }
        case subop::kVEC_POINT_VEC_SET: {
            const VmVector* vd = ws.v_regs[dst];
            if (!vd) { ws.status = IMPULSE_VM_ERR_TYPE_MISMATCH; VM_DISPATCH(); }
            if (!ws.is_r_valid(s1)) { ws.status = IMPULSE_VM_ERR_INVALID_OPERAND; VM_DISPATCH(); }
            int64_t idx64 = static_cast<int64_t>(ws.read_r_raw(s1));
            if (idx64 < 0 || static_cast<uint64_t>(idx64) >= vd->length) {
                ws.status = IMPULSE_VM_ERR_OUT_OF_BOUNDS; VM_DISPATCH();
            }
            uint32_t idx = static_cast<uint32_t>(idx64);
            uint32_t n = vd->length;
            size_t vwords = (n > 0u) ? ((static_cast<size_t>(n) + 63u) >> 6) : 1u;
            bool val_valid = ws.is_r_valid(s2);

            auto* ov = static_cast<VmVector*>(ws.arena.alloc(sizeof(VmVector), alignof(VmVector)));
            if (!ov) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
            new (ov) VmVector();
            ov->elem_tag = vd->elem_tag;
            ov->length = n;

            switch (vd->elem_tag) {
                case kVecTagI64:
                case kVecTagU64: {
                    int64_t* arr = ws.arena.alloc_array<int64_t>(n, 8);
                    uint64_t* valid = vd->valid_bits ? ws.arena.alloc_array<uint64_t>(vwords, 8) : p4_alloc_valid_all_true(ws.arena, n);
                    if (!arr || !valid) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
                    std::memcpy(arr, vd->i64_data, static_cast<size_t>(n) * sizeof(int64_t));
                    if (vd->valid_bits) std::memcpy(valid, vd->valid_bits, vwords * sizeof(uint64_t));
                    if (val_valid) {
                        arr[idx] = static_cast<int64_t>(ws.read_r_raw(s2));
                        valid[idx >> 6] |= (1ULL << (idx & 63u));
                    } else {
                        valid[idx >> 6] &= ~(1ULL << (idx & 63u));
                    }
                    ov->i64_data = arr;
                    ov->valid_bits = valid;
                    break;
                }
                case kVecTagF64: {
                    double* arr = ws.arena.alloc_array<double>(n, 8);
                    uint64_t* valid = vd->valid_bits ? ws.arena.alloc_array<uint64_t>(vwords, 8) : p4_alloc_valid_all_true(ws.arena, n);
                    if (!arr || !valid) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
                    std::memcpy(arr, vd->f64_data, static_cast<size_t>(n) * sizeof(double));
                    if (vd->valid_bits) std::memcpy(valid, vd->valid_bits, vwords * sizeof(uint64_t));
                    if (val_valid && p4_is_scalar_f64_valid(ws, s2)) {
                        arr[idx] = p4_read_scalar_f64(ws, s2);
                        valid[idx >> 6] |= (1ULL << (idx & 63u));
                    } else {
                        valid[idx >> 6] &= ~(1ULL << (idx & 63u));
                    }
                    ov->f64_data = arr;
                    ov->valid_bits = valid;
                    break;
                }
                case kVecTagBool3vl: {
                    uint64_t* valid = vd->valid_bits ? ws.arena.alloc_array<uint64_t>(vwords, 8) : p4_alloc_valid_all_true(ws.arena, n);
                    uint64_t* data  = ws.arena.alloc_array<uint64_t>(vwords, 8);
                    if (!valid || !data) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
                    if (vd->valid_bits) std::memcpy(valid, vd->valid_bits, vwords * sizeof(uint64_t));
                    if (vd->bool_data_bits) std::memcpy(data, vd->bool_data_bits, vwords * sizeof(uint64_t));
                    else std::memset(data, 0, vwords * sizeof(uint64_t));
                    uint64_t bit = 1ULL << (idx & 63u);
                    if (val_valid) {
                        valid[idx >> 6] |= bit;
                        if (ws.read_r_raw(s2) != 0ULL) data[idx >> 6] |= bit;
                        else data[idx >> 6] &= ~bit;
                    } else {
                        valid[idx >> 6] &= ~bit;
                        data[idx >> 6] &= ~bit;
                    }
                    ov->valid_bits = valid;
                    ov->bool_data_bits = data;
                    break;
                }
                case kVecTagStr: {
                    VmStrSlice* arr = ws.arena.alloc_array<VmStrSlice>(n, 8);
                    if (!arr) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
                    std::memcpy(arr, vd->str_data, static_cast<size_t>(n) * sizeof(VmStrSlice));
                    arr[idx] = val_valid ? ws.resolve_scalar_str(ws.read_r_raw(s2)) : VmStrSlice{nullptr, 0u, 0u};
                    ov->str_data = arr;
                    break;
                }
                default:
                    ws.status = IMPULSE_VM_ERR_TYPE_MISMATCH;
                    VM_DISPATCH();
            }
            ws.v_regs[dst] = ov;
            ws.state.pc += 1u;
            break;
        }
        case subop::kVEC_POINT_VEC_CAST: {
            const VmVector* v = ws.v_regs[s1];
            if (!v) { ws.status = IMPULSE_VM_ERR_TYPE_MISMATCH; VM_DISPATCH(); }
            if (!p4_is_numeric_vec(v)) { ws.status = IMPULSE_VM_ERR_TYPE_MISMATCH; VM_DISPATCH(); }
            uint32_t n = v->length;
            size_t vwords = (n > 0u) ? ((static_cast<size_t>(n) + 63u) >> 6) : 1u;
            uint64_t* out_valid = ws.arena.alloc_array<uint64_t>(vwords, 8);
            auto* ov = static_cast<VmVector*>(ws.arena.alloc(sizeof(VmVector), alignof(VmVector)));
            if (!out_valid || !ov) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
            std::memset(out_valid, 0, vwords * sizeof(uint64_t));
            new (ov) VmVector();
            ov->length = n;
            ov->valid_bits = out_valid;

            switch (imm_lo) {
                case 0: case 2: { // i64
                    int64_t* out = (n > 0u) ? ws.arena.alloc_array<int64_t>(n, 8) : nullptr;
                    if (n > 0u && !out) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
                    if (n > 0u) std::memset(out, 0, static_cast<size_t>(n) * sizeof(int64_t));
                    for (uint32_t i = 0; i < n; ++i) {
                        if (!v->is_valid_at(i)) continue;
                        int64_t val = 0;
                        if (v->elem_tag == kVecTagF64) {
                            double f = v->f64_data[i];
                            if (std::isnan(f)) continue;
                            if (f >= 9223372036854775808.0) val = INT64_MAX;
                            else if (f <= -9223372036854775808.0) val = INT64_MIN;
                            else val = static_cast<int64_t>(f);
                        } else {
                            val = v->i64_data[i];
                        }
                        out[i] = val;
                        out_valid[i >> 6] |= (1ULL << (i & 63u));
                    }
                    ov->elem_tag = kVecTagI64;
                    ov->i64_data = out;
                    break;
                }
                case 1: case 3: { // u64
                    int64_t* out = (n > 0u) ? ws.arena.alloc_array<int64_t>(n, 8) : nullptr;
                    if (n > 0u && !out) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
                    if (n > 0u) std::memset(out, 0, static_cast<size_t>(n) * sizeof(int64_t));
                    for (uint32_t i = 0; i < n; ++i) {
                        if (!v->is_valid_at(i)) continue;
                        uint64_t val = 0ULL;
                        if (v->elem_tag == kVecTagF64) {
                            double f = v->f64_data[i];
                            if (std::isnan(f)) continue;
                            if (f <= 0.0) val = 0ULL;
                            else if (f >= 18446744073709551616.0) val = UINT64_MAX;
                            else if (f >= 9223372036854775808.0) val = static_cast<uint64_t>(static_cast<int64_t>(f - 9223372036854775808.0)) | (1ULL << 63);
                            else val = static_cast<uint64_t>(static_cast<int64_t>(f));
                        } else {
                            val = static_cast<uint64_t>(v->i64_data[i]);
                        }
                        out[i] = static_cast<int64_t>(val);
                        out_valid[i >> 6] |= (1ULL << (i & 63u));
                    }
                    ov->elem_tag = kVecTagU64;
                    ov->i64_data = out;
                    break;
                }
                case 4: case 5: { // f64
                    double* out = (n > 0u) ? ws.arena.alloc_array<double>(n, 8) : nullptr;
                    if (n > 0u && !out) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
                    if (n > 0u) std::memset(out, 0, static_cast<size_t>(n) * sizeof(double));
                    for (uint32_t i = 0; i < n; ++i) {
                        if (!v->is_valid_at(i)) continue;
                        double f = p4_vec_to_f64(*v, i);
                        if (std::isnan(f)) continue;
                        out[i] = f;
                        out_valid[i >> 6] |= (1ULL << (i & 63u));
                    }
                    ov->elem_tag = kVecTagF64;
                    ov->f64_data = out;
                    break;
                }
                default:
                    ws.status = IMPULSE_VM_ERR_TYPE_MISMATCH;
                    VM_DISPATCH();
            }
            ws.v_regs[dst] = ov;
            ws.state.pc += 1u;
            break;
        }
        case subop::kVEC_POINT_VEC_LEN: {
            const VmVector* v = ws.v_regs[s1];
            if (!v) { ws.status = IMPULSE_VM_ERR_TYPE_MISMATCH; VM_DISPATCH(); }
            uint64_t len = static_cast<uint64_t>(v->length);
            ws.write_r(dst, static_cast<uint8_t>(ScalarType::SCALAR_U64), len);
            ws.set_zf(len == 0ULL);
            ws.set_nf(false);
            ws.state.pc += 1u;
            break;
        }
        default:
            ws.status = IMPULSE_VM_ERR_INVALID_OPCODE;
            break;
    }
    VM_DISPATCH();
}
