// Opcode 0x57: OP_COLLECT
VM_OP_CASE(COLLECT) {
    const uint8_t subop = IMP_SUBOP(w);
    const uint8_t flags = IMP_FLAGS(w);
    const uint8_t s1 = IMP_S1(w);
    const uint8_t s3 = IMP_S3(w);
    const uint16_t slot = IMP_IMM16_LO(w);
    const bool masked = (flags & FLAG_MASKED_S3) != 0u;

    switch (subop) {
        case subop::kCOLLECT_FRONTIER_BITSET: {
            const VmFrontier* fa = p3_get_f1bit(ws, s1);
            if (!fa) VM_DISPATCH();
            if (fa->domain_card > UINT32_MAX) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
            uint32_t n = static_cast<uint32_t>(fa->domain_card);
            size_t vwords = (n > 0u) ? ((static_cast<size_t>(n) + 63u) >> 6) : 1u;
            uint64_t* valid = p4_alloc_valid_all_true(ws.arena, n);
            uint64_t* data  = ws.arena.alloc_array<uint64_t>(vwords, 8);
            auto* ov = static_cast<VmVector*>(ws.arena.alloc(sizeof(VmVector), alignof(VmVector)));
            if (!valid || !data || !ov) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
            std::memset(data, 0, vwords * sizeof(uint64_t));
            const uint32_t* ids = nullptr; uint32_t cnt = 0u;
            if (!fa->materialize_1bit(ws.arena, &ids, &cnt)) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
            for (uint32_t i = 0; i < cnt; ++i) {
                uint32_t u = ids[i];
                if (u < n) data[u >> 6] |= (1ULL << (u & 63u));
            }
            new (ov) VmVector();
            ov->elem_tag = kVecTagBool3vl; ov->length = n; ov->valid_bits = valid; ov->bool_data_bits = data;
            if (!p4_put_output(ws, slot, ov)) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
            ws.set_zf(cnt == 0u);
            ws.state.pc += 1u;
            break;
        }
        case subop::kCOLLECT_FRONTIER_IDS: {
            const VmFrontier* fa = p3_get_f1bit(ws, s1);
            if (!fa) VM_DISPATCH();
            const uint32_t* ids = nullptr; uint32_t cnt = 0u;
            if (!fa->materialize_1bit(ws.arena, &ids, &cnt)) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
            int64_t* vals = (cnt > 0u) ? ws.arena.alloc_array<int64_t>(cnt, 8) : nullptr;
            uint64_t* valid = p4_alloc_valid_all_true(ws.arena, cnt);
            auto* ov = static_cast<VmVector*>(ws.arena.alloc(sizeof(VmVector), alignof(VmVector)));
            if ((cnt > 0u && !vals) || !valid || !ov) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
            for (uint32_t i = 0; i < cnt; ++i) vals[i] = static_cast<int64_t>(ids[i]);
            new (ov) VmVector();
            ov->elem_tag = kVecTagU64; ov->length = cnt; ov->i64_data = vals; ov->valid_bits = valid;
            if (!p4_put_output(ws, slot, ov)) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
            ws.set_zf(cnt == 0u);
            ws.state.pc += 1u;
            break;
        }
        case subop::kCOLLECT_VEC:
        case subop::kCOLLECT_TABLE_COL: {
            const VmFrontier* mask = p4_get_mask_frontier(ws, flags, s3);
            if (ws.status != IMPULSE_VM_OK) VM_DISPATCH();
            const VmVector* v = ws.v_regs[s1];
            if (!v) { ws.status = IMPULSE_VM_ERR_TYPE_MISMATCH; VM_DISPATCH(); }
            VmVector* out = nullptr;
            if (masked) {
                if (v->elem_tag == kVecTagLaneState) { ws.status = IMPULSE_VM_ERR_TYPE_MISMATCH; VM_DISPATCH(); }
                const uint32_t* ids = nullptr; uint32_t cnt = 0u;
                if (!mask->materialize_1bit(ws.arena, &ids, &cnt)) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
                for (uint32_t i = 0; i < cnt; ++i) {
                    if (ids[i] >= v->length) { ws.status = IMPULSE_VM_ERR_OUT_OF_BOUNDS; VM_DISPATCH(); }
                }
                out = p4_gather_from_vec(ws.arena, *v, ids, nullptr, cnt, nullptr);
            } else {
                out = p4_shallow_copy_vec(ws.arena, *v);
            }
            if (!out) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
            if (!p4_put_output(ws, slot, out)) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
            ws.set_zf(out->length == 0u);
            ws.state.pc += 1u;
            break;
        }
        case subop::kCOLLECT_SCALAR: {
            bool valid = ws.is_r_valid(s1);
            uint8_t ty = ws.read_r_type(s1);
            uint64_t val = ws.read_r_raw(s1);
            auto* ov = static_cast<VmVector*>(ws.arena.alloc(sizeof(VmVector), alignof(VmVector)));
            uint64_t* vb = ws.arena.alloc_array<uint64_t>(1, 8);
            if (!ov || !vb) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
            new (ov) VmVector();
            ov->length = 1u;
            ov->valid_bits = vb;
            bool is_zero = false;
            if (!valid) {
                int64_t* d = ws.arena.alloc_array<int64_t>(1, 8);
                if (!d) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
                d[0] = 0; vb[0] = 0ULL;
                ov->elem_tag = kVecTagI64; ov->i64_data = d;
                is_zero = true;
            } else if (ty == static_cast<uint8_t>(ScalarType::SCALAR_F64)) {
                double* d = ws.arena.alloc_array<double>(1, 8);
                if (!d) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
                double f = p1_bits_to_f64(val);
                ov->elem_tag = kVecTagF64; ov->f64_data = d;
                if (std::isnan(f)) { d[0] = 0.0; vb[0] = 0ULL; is_zero = true; }
                else { d[0] = f; vb[0] = 1ULL; is_zero = (f == 0.0); }
            } else if (ty == static_cast<uint8_t>(ScalarType::SCALAR_STR_HANDLE)) {
                VmStrSlice* d = ws.arena.alloc_array<VmStrSlice>(1, 8);
                if (!d) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
                VmStrSlice s = ws.resolve_scalar_str(val);
                d[0] = s;
                ov->elem_tag = kVecTagStr; ov->str_data = d; ov->valid_bits = nullptr;
                is_zero = (!s.valid || s.len == 0u);
            } else if (ty == static_cast<uint8_t>(ScalarType::SCALAR_U64)) {
                int64_t* d = ws.arena.alloc_array<int64_t>(1, 8);
                if (!d) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
                d[0] = static_cast<int64_t>(val); vb[0] = 1ULL;
                ov->elem_tag = kVecTagU64; ov->i64_data = d;
                is_zero = (val == 0ULL);
            } else {
                int64_t* d = ws.arena.alloc_array<int64_t>(1, 8);
                if (!d) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
                d[0] = static_cast<int64_t>(val); vb[0] = 1ULL;
                ov->elem_tag = kVecTagI64; ov->i64_data = d;
                is_zero = (val == 0ULL);
            }
            if (!p4_put_output(ws, slot, ov)) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
            ws.set_zf(is_zero);
            ws.state.pc += 1u;
            break;
        }
        default:
            ws.status = IMPULSE_VM_ERR_INVALID_OPCODE;
            break;
    }
    VM_DISPATCH();
}
