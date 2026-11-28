// Opcode 0x41: OP_GATHER_NODE_ATTR
VM_OP_CASE(GATHER_NODE_ATTR) {
    const uint8_t subop = IMP_SUBOP(w);
    const uint8_t flags = IMP_FLAGS(w);
    const uint8_t dst = IMP_DST(w);
    const uint8_t s1 = IMP_S1(w);
    const uint8_t s3 = IMP_S3(w);
    const uint16_t dom_id = IMP_IMM16_LO(w);
    const uint16_t attr_id = IMP_IMM16_HI(w);

    if (!snap) { ws.status = IMPULSE_VM_ERR_NULL_SNAPSHOT; VM_DISPATCH(); }
    if (dom_id >= snap->domain_count) { ws.status = IMPULSE_VM_ERR_OUT_OF_BOUNDS; VM_DISPATCH(); }
    const VmVector* col = p3_find_node_attr(snap, dom_id, attr_id);
    if (!col) { ws.status = IMPULSE_VM_ERR_OUT_OF_BOUNDS; VM_DISPATCH(); }

    if (subop == 0u) { // .vec
        const VmFrontier* mask = p4_get_mask_frontier(ws, flags, s3);
        if (ws.status != IMPULSE_VM_OK) VM_DISPATCH();
        const VmVector* idx_vec = ws.v_regs[s1];
        if (!idx_vec || (idx_vec->elem_tag != kVecTagI64 && idx_vec->elem_tag != kVecTagU64)) {
            ws.status = IMPULSE_VM_ERR_TYPE_MISMATCH; VM_DISPATCH();
        }
        uint32_t n = idx_vec->length;
        uint32_t* indices = (n > 0u) ? ws.arena.alloc_array<uint32_t>(n, 8) : nullptr;
        uint8_t* valid = (n > 0u) ? ws.arena.alloc_array<uint8_t>(n, 8) : nullptr;
        if (n > 0u && (!indices || !valid)) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
        if (n > 0u) std::memset(valid, 0, n);
        for (uint32_t i = 0; i < n; ++i) {
            if (!p4_lane_active(mask, i) || !idx_vec->is_valid_at(i)) continue;
            int64_t u = idx_vec->i64_data[i];
            if (u < 0 || static_cast<uint64_t>(u) >= col->length) {
                ws.status = IMPULSE_VM_ERR_OUT_OF_BOUNDS; VM_DISPATCH();
            }
            indices[i] = static_cast<uint32_t>(u);
            valid[i] = 1u;
        }
        VmVector* ov = p4_gather_from_vec(ws.arena, *col, indices, valid, n, mask);
        if (!ov) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
        ws.v_regs[dst] = ov;
        ws.state.pc += 1u;
    } else if (subop == 1u) { // .frontier_compact
        const VmFrontier* f = p3_get_f1bit(ws, s1);
        if (!f) VM_DISPATCH();
        if (f->domain_id != dom_id) { ws.status = IMPULSE_VM_ERR_DOMAIN_MISMATCH; VM_DISPATCH(); }
        const uint32_t* ids = nullptr; uint32_t n = 0u;
        if (!f->materialize_1bit(ws.arena, &ids, &n)) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
        for (uint32_t i = 0; i < n; ++i) {
            if (ids[i] >= col->length) { ws.status = IMPULSE_VM_ERR_OUT_OF_BOUNDS; VM_DISPATCH(); }
        }
        VmVector* ov = p4_gather_from_vec(ws.arena, *col, ids, nullptr, n, nullptr);
        if (!ov) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
        ws.v_regs[dst] = ov;
        ws.state.pc += 1u;
    } else if (subop == 2u) { // .scalar
        if (!ws.is_r_valid(s1)) {
            ws.write_r_null(dst);
            ws.set_zf(true);
            ws.set_nf(true);
            ws.state.pc += 1u;
            VM_DISPATCH();
        }
        int64_t u = static_cast<int64_t>(ws.read_r_raw(s1));
        if (u < 0 || static_cast<uint64_t>(u) >= col->length) {
            ws.status = IMPULSE_VM_ERR_OUT_OF_BOUNDS; VM_DISPATCH();
        }
        p4_write_vec_elem_to_scalar(ws, dst, *col, static_cast<uint32_t>(u));
        if (ws.status == IMPULSE_VM_OK) ws.state.pc += 1u;
    } else {
        ws.status = IMPULSE_VM_ERR_INVALID_OPCODE;
    }
    VM_DISPATCH();
}
