// Opcode 0x55: OP_MAP_KEYS_TO_DENSE
VM_OP_CASE(MAP_KEYS_TO_DENSE) {
    const uint8_t subop = IMP_SUBOP(w);
    const uint8_t flags = IMP_FLAGS(w);
    const uint8_t dst = IMP_DST(w);
    const uint8_t s1 = IMP_S1(w);
    const uint16_t dom_id = IMP_IMM16_LO(w);
    const uint16_t imm_hi = IMP_IMM16_HI(w);

    if (!snap) { ws.status = IMPULSE_VM_ERR_NULL_SNAPSHOT; VM_DISPATCH(); }
    if (dom_id >= snap->domain_count) { ws.status = IMPULSE_VM_ERR_OUT_OF_BOUNDS; VM_DISPATCH(); }
    const VmKeyCatalog* catalog = p4_find_key_catalog(snap, dom_id);
    if (!catalog) { ws.status = IMPULSE_VM_ERR_OUT_OF_BOUNDS; VM_DISPATCH(); }
    const uint64_t dom_card = snap->domain_cards[dom_id];
    const bool accum = (flags & FLAG_ACCUM) != 0u;

    switch (subop) {
        case subop::kMAP_KEYS_TO_DENSE_PARAM_TO_F: {
            const VmParamEntry* pe = ws.find_param(imm_hi);
            if (!pe) { ws.status = IMPULSE_VM_ERR_OUT_OF_BOUNDS; VM_DISPATCH(); }
            if (pe->kind != ParamKind::Vec || !pe->vec || pe->vec->elem_tag != kVecTagStr) {
                ws.status = IMPULSE_VM_ERR_TYPE_MISMATCH; VM_DISPATCH();
            }
            const uint32_t* acc_ids = nullptr; uint32_t acc_len = 0u;
            if (accum) {
                const VmFrontier* fd = p3_get_f1bit(ws, dst);
                if (!fd) VM_DISPATCH();
                if (fd->domain_id != dom_id) { ws.status = IMPULSE_VM_ERR_DOMAIN_MISMATCH; VM_DISPATCH(); }
                if (!fd->materialize_1bit(ws.arena, &acc_ids, &acc_len)) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
            }
            const VmVector* sv = pe->vec;
            uint32_t* buf = (sv->length > 0u) ? ws.arena.alloc_array<uint32_t>(sv->length, 8) : nullptr;
            if (sv->length > 0u && !buf) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
            uint32_t cnt = 0u;
            for (uint32_t i = 0; i < sv->length; ++i) {
                if (!sv->is_valid_at(i)) continue;
                int64_t id = p4_lookup_key(catalog, sv->str_data[i]);
                if (id >= 0 && static_cast<uint64_t>(id) < dom_card) buf[cnt++] = static_cast<uint32_t>(id);
            }
            uint32_t uniq = (cnt > 0u) ? p2_dedupe_sorted_inplace(buf, cnt) : 0u;
            uint32_t* final_ids = buf;
            uint32_t final_len = uniq;
            if (accum && acc_len > 0u) {
                if (!p3_union_sorted(ws.arena, acc_ids, acc_len, buf, uniq, &final_ids, &final_len)) {
                    ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH();
                }
            }
            if (!ws.f_regs[dst].init_from_sorted_ids(ws.arena, dom_id, dom_card, final_ids, final_len, ws.geometry, true)) {
                ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH();
            }
            ws.set_f_init(dst, true);
            ws.set_zf(final_len == 0u);
            ws.state.pc += 1u;
            break;
        }
        case subop::kMAP_KEYS_TO_DENSE_VEC_TO_V:
        case subop::kMAP_KEYS_TO_DENSE_V_TO_V: {
            const VmVector* sv = ws.v_regs[s1];
            if (!sv || sv->elem_tag != kVecTagStr) { ws.status = IMPULSE_VM_ERR_TYPE_MISMATCH; VM_DISPATCH(); }
            uint32_t n = sv->length;
            size_t vwords = (n > 0u) ? ((static_cast<size_t>(n) + 63u) >> 6) : 1u;
            int64_t* out = (n > 0u) ? ws.arena.alloc_array<int64_t>(n, 8) : nullptr;
            uint64_t* out_valid = ws.arena.alloc_array<uint64_t>(vwords, 8);
            auto* ov = static_cast<VmVector*>(ws.arena.alloc(sizeof(VmVector), alignof(VmVector)));
            if ((n > 0u && !out) || !out_valid || !ov) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
            if (n > 0u) std::memset(out, 0, static_cast<size_t>(n) * sizeof(int64_t));
            std::memset(out_valid, 0, vwords * sizeof(uint64_t));
            uint32_t hits = 0u;
            for (uint32_t i = 0; i < n; ++i) {
                if (!sv->is_valid_at(i)) continue;
                int64_t id = p4_lookup_key(catalog, sv->str_data[i]);
                if (id >= 0 && static_cast<uint64_t>(id) < dom_card) {
                    out[i] = id;
                    out_valid[i >> 6] |= (1ULL << (i & 63u));
                    hits++;
                }
            }
            new (ov) VmVector();
            ov->elem_tag = kVecTagU64; ov->length = n; ov->i64_data = out; ov->valid_bits = out_valid;
            ws.v_regs[dst] = ov;
            ws.set_zf(hits == 0u);
            ws.state.pc += 1u;
            break;
        }
        case subop::kMAP_KEYS_TO_DENSE_SCALAR_TO_R: {
            if (!ws.is_r_valid(s1)) {
                ws.write_r_null(dst); ws.set_zf(true); ws.set_nf(true); ws.state.pc += 1u; VM_DISPATCH();
            }
            VmStrSlice key = ws.resolve_scalar_str(ws.read_r_raw(s1));
            if (!key.valid) {
                ws.write_r_null(dst); ws.set_zf(true); ws.set_nf(true); ws.state.pc += 1u; VM_DISPATCH();
            }
            int64_t id = p4_lookup_key(catalog, key);
            if (id < 0 || static_cast<uint64_t>(id) >= dom_card) {
                ws.write_r_null(dst); ws.set_zf(true); ws.set_nf(true);
            } else {
                ws.write_r(dst, static_cast<uint8_t>(ScalarType::SCALAR_U64), static_cast<uint64_t>(id));
                ws.set_zf(id == 0); ws.set_nf(false);
            }
            ws.state.pc += 1u;
            break;
        }
        case subop::kMAP_KEYS_TO_DENSE_F_TO_F: {
            const VmFrontier* f_src = p3_get_f1bit(ws, s1);
            if (!f_src) VM_DISPATCH();
            const VmKeyCatalog* src_cat = p4_find_key_catalog(snap, f_src->domain_id);
            if (!src_cat) { ws.status = IMPULSE_VM_ERR_OUT_OF_BOUNDS; VM_DISPATCH(); }
            const uint32_t* acc_ids = nullptr; uint32_t acc_len = 0u;
            if (accum) {
                const VmFrontier* fd = p3_get_f1bit(ws, dst);
                if (!fd) VM_DISPATCH();
                if (fd->domain_id != dom_id) { ws.status = IMPULSE_VM_ERR_DOMAIN_MISMATCH; VM_DISPATCH(); }
                if (!fd->materialize_1bit(ws.arena, &acc_ids, &acc_len)) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
            }
            const uint32_t* src_ids = nullptr; uint32_t src_len = 0u;
            if (!f_src->materialize_1bit(ws.arena, &src_ids, &src_len)) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
            uint32_t* buf = (src_len > 0u) ? ws.arena.alloc_array<uint32_t>(src_len, 8) : nullptr;
            if (src_len > 0u && !buf) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
            uint32_t cnt = 0u;
            for (uint32_t i = 0; i < src_len; ++i) {
                uint32_t u = src_ids[i];
                if (u < src_cat->count) {
                    int64_t tid = p4_lookup_key(catalog, src_cat->keys[u]);
                    if (tid >= 0 && static_cast<uint64_t>(tid) < dom_card) buf[cnt++] = static_cast<uint32_t>(tid);
                }
            }
            uint32_t uniq = (cnt > 0u) ? p2_dedupe_sorted_inplace(buf, cnt) : 0u;
            uint32_t* final_ids = buf;
            uint32_t final_len = uniq;
            if (accum && acc_len > 0u) {
                if (!p3_union_sorted(ws.arena, acc_ids, acc_len, buf, uniq, &final_ids, &final_len)) {
                    ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH();
                }
            }
            if (!ws.f_regs[dst].init_from_sorted_ids(ws.arena, dom_id, dom_card, final_ids, final_len, ws.geometry, true)) {
                ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH();
            }
            ws.set_f_init(dst, true);
            ws.set_zf(final_len == 0u);
            ws.state.pc += 1u;
            break;
        }
        default:
            ws.status = IMPULSE_VM_ERR_INVALID_OPCODE;
            break;
    }
    VM_DISPATCH();
}
