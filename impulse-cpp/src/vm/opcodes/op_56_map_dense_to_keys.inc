// Opcode 0x56: OP_MAP_DENSE_TO_KEYS
V11_OP_CASE(MAP_DENSE_TO_KEYS) {
    const uint8_t subop = IMP_V11_SUBOP(w);
    const uint8_t dst = IMP_V11_DST(w);
    const uint8_t s1 = IMP_V11_S1(w);
    const uint16_t dom_id = IMP_V11_IMM16_LO(w);

    if (!snap) { ws.status = IMPULSE_VM_V11_ERR_NULL_SNAPSHOT; V11_DISPATCH(); }
    if (dom_id >= snap->domain_count) { ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; V11_DISPATCH(); }
    const V11KeyCatalog* catalog = p4_find_key_catalog(snap, dom_id);
    if (!catalog) { ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; V11_DISPATCH(); }

    switch (subop) {
        case subop::kMAP_DENSE_TO_KEYS_VEC: {
            const V11Vector* iv = ws.v_regs[s1];
            if (!iv || (iv->elem_tag != kVecTagI64 && iv->elem_tag != kVecTagU64)) {
                ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; V11_DISPATCH();
            }
            uint32_t n = iv->length;
            V11StrSlice* out = (n > 0u) ? ws.arena.alloc_array<V11StrSlice>(n, 8) : nullptr;
            auto* ov = static_cast<V11Vector*>(ws.arena.alloc(sizeof(V11Vector), alignof(V11Vector)));
            if ((n > 0u && !out) || !ov) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
            for (uint32_t i = 0; i < n; ++i) {
                out[i] = {nullptr, 0u, 0u};
                if (!iv->is_valid_at(i)) continue;
                int64_t u = iv->i64_data[i];
                if (u < 0 || static_cast<uint64_t>(u) >= catalog->count) {
                    ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; V11_DISPATCH();
                }
                out[i] = catalog->keys[static_cast<uint32_t>(u)];
            }
            new (ov) V11Vector();
            ov->elem_tag = kVecTagStr; ov->length = n; ov->str_data = out;
            ws.v_regs[dst] = ov;
            ws.state.pc += 1u;
            break;
        }
        case subop::kMAP_DENSE_TO_KEYS_FRONTIER_SORTED: {
            const V11Frontier* f = p3_get_f1bit(ws, s1);
            if (!f) V11_DISPATCH();
            if (f->domain_id != dom_id) { ws.status = IMPULSE_VM_V11_ERR_DOMAIN_MISMATCH; V11_DISPATCH(); }
            const uint32_t* ids = nullptr; uint32_t n = 0u;
            if (!f->materialize_1bit(ws.arena, &ids, &n)) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
            V11StrSlice* out = (n > 0u) ? ws.arena.alloc_array<V11StrSlice>(n, 8) : nullptr;
            auto* ov = static_cast<V11Vector*>(ws.arena.alloc(sizeof(V11Vector), alignof(V11Vector)));
            if ((n > 0u && !out) || !ov) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
            for (uint32_t i = 0; i < n; ++i) {
                uint32_t u = ids[i];
                if (u >= catalog->count) { ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; V11_DISPATCH(); }
                out[i] = catalog->keys[u];
            }
            new (ov) V11Vector();
            ov->elem_tag = kVecTagStr; ov->length = n; ov->str_data = out;
            ws.v_regs[dst] = ov;
            ws.state.pc += 1u;
            break;
        }
        case subop::kMAP_DENSE_TO_KEYS_SCALAR: {
            if (!ws.is_r_valid(s1)) { ws.write_r_null(dst); ws.state.pc += 1u; V11_DISPATCH(); }
            int64_t u = static_cast<int64_t>(ws.read_r_raw(s1));
            if (u < 0 || static_cast<uint64_t>(u) >= catalog->count) {
                ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; V11_DISPATCH();
            }
            V11StrSlice s = catalog->keys[static_cast<uint32_t>(u)];
            if (!s.valid) {
                ws.write_r_null(dst);
            } else {
                uint64_t h = ws.intern_scalar_str(s.ptr, s.len);
                if (h == 0ULL) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
                ws.write_r(dst, static_cast<uint8_t>(ScalarType::SCALAR_STR_HANDLE), h);
            }
            ws.state.pc += 1u;
            break;
        }
        default:
            ws.status = IMPULSE_VM_V11_ERR_INVALID_OPCODE;
            break;
    }
    V11_DISPATCH();
}
