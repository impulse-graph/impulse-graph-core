// Opcode 0x56: OP_MAP_DENSE_TO_KEYS
VM_OP_CASE(MAP_DENSE_TO_KEYS) {
    const uint8_t subop = IMP_SUBOP(w);
    const uint8_t dst = IMP_DST(w);
    const uint8_t s1 = IMP_S1(w);
    const uint16_t dom_id = IMP_IMM16_LO(w);

    if (!snap) { ws.status = IMPULSE_VM_ERR_NULL_SNAPSHOT; VM_DISPATCH(); }
    if (dom_id >= snap->domain_count) { ws.status = IMPULSE_VM_ERR_OUT_OF_BOUNDS; VM_DISPATCH(); }
    const VmKeyCatalog* catalog = p4_find_key_catalog(snap, dom_id);
    if (!catalog) { ws.status = IMPULSE_VM_ERR_OUT_OF_BOUNDS; VM_DISPATCH(); }

    switch (subop) {
        case subop::kMAP_DENSE_TO_KEYS_VEC: {
            const VmVector* iv = ws.v_regs[s1];
            if (!iv || (iv->elem_tag != kVecTagI64 && iv->elem_tag != kVecTagU64)) {
                ws.status = IMPULSE_VM_ERR_TYPE_MISMATCH; VM_DISPATCH();
            }
            uint32_t n = iv->length;
            VmStrSlice* out = (n > 0u) ? ws.arena.alloc_array<VmStrSlice>(n, 8) : nullptr;
            auto* ov = static_cast<VmVector*>(ws.arena.alloc(sizeof(VmVector), alignof(VmVector)));
            if ((n > 0u && !out) || !ov) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
            for (uint32_t i = 0; i < n; ++i) {
                out[i] = {nullptr, 0u, 0u};
                if (!iv->is_valid_at(i)) continue;
                int64_t u = iv->i64_data[i];
                if (u < 0 || static_cast<uint64_t>(u) >= catalog->count) {
                    ws.status = IMPULSE_VM_ERR_OUT_OF_BOUNDS; VM_DISPATCH();
                }
                out[i] = catalog->keys[static_cast<uint32_t>(u)];
            }
            new (ov) VmVector();
            ov->elem_tag = kVecTagStr; ov->length = n; ov->str_data = out;
            ws.v_regs[dst] = ov;
            ws.state.pc += 1u;
            break;
        }
        case subop::kMAP_DENSE_TO_KEYS_FRONTIER_SORTED: {
            const VmFrontier* f = p3_get_f1bit(ws, s1);
            if (!f) VM_DISPATCH();
            if (f->domain_id != dom_id) { ws.status = IMPULSE_VM_ERR_DOMAIN_MISMATCH; VM_DISPATCH(); }
            const uint32_t* ids = nullptr; uint32_t n = 0u;
            if (!f->materialize_1bit(ws.arena, &ids, &n)) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
            VmStrSlice* out = (n > 0u) ? ws.arena.alloc_array<VmStrSlice>(n, 8) : nullptr;
            auto* ov = static_cast<VmVector*>(ws.arena.alloc(sizeof(VmVector), alignof(VmVector)));
            if ((n > 0u && !out) || !ov) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
            for (uint32_t i = 0; i < n; ++i) {
                uint32_t u = ids[i];
                if (u >= catalog->count) { ws.status = IMPULSE_VM_ERR_OUT_OF_BOUNDS; VM_DISPATCH(); }
                out[i] = catalog->keys[u];
            }
            new (ov) VmVector();
            ov->elem_tag = kVecTagStr; ov->length = n; ov->str_data = out;
            ws.v_regs[dst] = ov;
            ws.state.pc += 1u;
            break;
        }
        case subop::kMAP_DENSE_TO_KEYS_SCALAR: {
            if (!ws.is_r_valid(s1)) { ws.write_r_null(dst); ws.state.pc += 1u; VM_DISPATCH(); }
            int64_t u = static_cast<int64_t>(ws.read_r_raw(s1));
            if (u < 0 || static_cast<uint64_t>(u) >= catalog->count) {
                ws.status = IMPULSE_VM_ERR_OUT_OF_BOUNDS; VM_DISPATCH();
            }
            VmStrSlice s = catalog->keys[static_cast<uint32_t>(u)];
            if (!s.valid) {
                ws.write_r_null(dst);
            } else {
                uint64_t h = ws.intern_scalar_str(s.ptr, s.len);
                if (h == 0ULL) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
                ws.write_r(dst, static_cast<uint8_t>(ScalarType::SCALAR_STR_HANDLE), h);
            }
            ws.state.pc += 1u;
            break;
        }
        default:
            ws.status = IMPULSE_VM_ERR_INVALID_OPCODE;
            break;
    }
    VM_DISPATCH();
}
