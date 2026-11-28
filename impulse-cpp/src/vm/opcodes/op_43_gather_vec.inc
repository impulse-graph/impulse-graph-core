// Opcode 0x43: OP_GATHER_VEC
VM_OP_CASE(GATHER_VEC) {
    const uint8_t flags = IMP_FLAGS(w);
    const uint8_t dst = IMP_DST(w);
    const uint8_t s1 = IMP_S1(w);
    const uint8_t s2 = IMP_S2(w);
    const uint8_t s3 = IMP_S3(w);

    const VmFrontier* mask = p4_get_mask_frontier(ws, flags, s3);
    if (ws.status != IMPULSE_VM_OK) VM_DISPATCH();
    const VmVector* src_vec = ws.v_regs[s1];
    const VmVector* idx_vec = ws.v_regs[s2];
    if (!src_vec || src_vec->elem_tag == kVecTagLaneState || !idx_vec ||
        (idx_vec->elem_tag != kVecTagI64 && idx_vec->elem_tag != kVecTagU64)) {
        ws.status = IMPULSE_VM_ERR_TYPE_MISMATCH; VM_DISPATCH();
    }
    uint32_t n = idx_vec->length;
    uint32_t* indices = (n > 0u) ? ws.arena.alloc_array<uint32_t>(n, 8) : nullptr;
    uint8_t* valid = (n > 0u) ? ws.arena.alloc_array<uint8_t>(n, 8) : nullptr;
    if (n > 0u && (!indices || !valid)) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
    if (n > 0u) std::memset(valid, 0, n);
    for (uint32_t i = 0; i < n; ++i) {
        if (!p4_lane_active(mask, i) || !idx_vec->is_valid_at(i)) continue;
        int64_t idx = idx_vec->i64_data[i];
        if (idx < 0 || static_cast<uint64_t>(idx) >= src_vec->length) {
            ws.status = IMPULSE_VM_ERR_OUT_OF_BOUNDS; VM_DISPATCH();
        }
        indices[i] = static_cast<uint32_t>(idx);
        valid[i] = 1u;
    }
    VmVector* ov = p4_gather_from_vec(ws.arena, *src_vec, indices, valid, n, mask);
    if (!ov) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
    ws.v_regs[dst] = ov;
    ws.state.pc += 1u;
    VM_DISPATCH();
}
