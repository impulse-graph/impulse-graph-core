// Opcode 0x42: OP_GATHER_EDGE_ATTR
V11_OP_CASE(GATHER_EDGE_ATTR) {
    const uint8_t subop = IMP_V11_SUBOP(w);
    const uint8_t flags = IMP_V11_FLAGS(w);
    const uint8_t dst = IMP_V11_DST(w);
    const uint8_t s1 = IMP_V11_S1(w);
    const uint8_t s3 = IMP_V11_S3(w);
    const uint16_t rel_id = IMP_V11_IMM16_LO(w);
    const uint16_t attr_id = IMP_V11_IMM16_HI(w);

    if (!snap) { ws.status = IMPULSE_VM_V11_ERR_NULL_SNAPSHOT; V11_DISPATCH(); }
    if (rel_id >= snap->relation_count) { ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; V11_DISPATCH(); }
    const V11Vector* col = p3_find_edge_attr(snap->relations[rel_id], attr_id);
    if (!col) { ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; V11_DISPATCH(); }

    if (subop == 0u) { // .vec
        const V11Frontier* mask = p4_get_mask_frontier(ws, flags, s3);
        if (ws.status != IMPULSE_VM_V11_OK) V11_DISPATCH();
        const V11Vector* idx_vec = ws.v_regs[s1];
        if (!idx_vec || (idx_vec->elem_tag != kVecTagI64 && idx_vec->elem_tag != kVecTagU64)) {
            ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; V11_DISPATCH();
        }
        uint32_t n = idx_vec->length;
        uint32_t* indices = (n > 0u) ? ws.arena.alloc_array<uint32_t>(n, 8) : nullptr;
        uint8_t* valid = (n > 0u) ? ws.arena.alloc_array<uint8_t>(n, 8) : nullptr;
        if (n > 0u && (!indices || !valid)) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
        if (n > 0u) std::memset(valid, 0, n);
        for (uint32_t i = 0; i < n; ++i) {
            if (!p4_lane_active(mask, i) || !idx_vec->is_valid_at(i)) continue;
            int64_t e = idx_vec->i64_data[i];
            if (e < 0 || static_cast<uint64_t>(e) >= col->length) {
                ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; V11_DISPATCH();
            }
            indices[i] = static_cast<uint32_t>(e);
            valid[i] = 1u;
        }
        V11Vector* ov = p4_gather_from_vec(ws.arena, *col, indices, valid, n, mask);
        if (!ov) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
        ws.v_regs[dst] = ov;
        ws.state.pc += 1u;
    } else if (subop == 1u) { // .scalar
        if (!ws.is_r_valid(s1)) {
            ws.write_r_null(dst);
            ws.set_zf(true);
            ws.set_nf(true);
            ws.state.pc += 1u;
            V11_DISPATCH();
        }
        int64_t e = static_cast<int64_t>(ws.read_r_raw(s1));
        if (e < 0 || static_cast<uint64_t>(e) >= col->length) {
            ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; V11_DISPATCH();
        }
        p4_write_vec_elem_to_scalar(ws, dst, *col, static_cast<uint32_t>(e));
        if (ws.status == IMPULSE_VM_V11_OK) ws.state.pc += 1u;
    } else {
        ws.status = IMPULSE_VM_V11_ERR_INVALID_OPCODE;
    }
    V11_DISPATCH();
}
