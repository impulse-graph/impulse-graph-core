// Opcode 0x43: OP_GATHER_VEC
V11_OP_CASE(GATHER_VEC) {
    const uint8_t flags = IMP_V11_FLAGS(w);
    const uint8_t dst = IMP_V11_DST(w);
    const uint8_t s1 = IMP_V11_S1(w);
    const uint8_t s2 = IMP_V11_S2(w);
    const uint8_t s3 = IMP_V11_S3(w);

    const V11Frontier* mask = p4_get_mask_frontier(ws, flags, s3);
    if (ws.status != IMPULSE_VM_V11_OK) V11_DISPATCH();
    const V11Vector* src_vec = ws.v_regs[s1];
    const V11Vector* idx_vec = ws.v_regs[s2];
    if (!src_vec || src_vec->elem_tag == kVecTagLaneState || !idx_vec ||
        (idx_vec->elem_tag != kVecTagI64 && idx_vec->elem_tag != kVecTagU64)) {
        ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; V11_DISPATCH();
    }
    uint32_t n = idx_vec->length;
    uint32_t* indices = (n > 0u) ? ws.arena.alloc_array<uint32_t>(n, 8) : nullptr;
    uint8_t* valid = (n > 0u) ? ws.arena.alloc_array<uint8_t>(n, 8) : nullptr;
    if (n > 0u && (!indices || !valid)) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
    if (n > 0u) std::memset(valid, 0, n);
    for (uint32_t i = 0; i < n; ++i) {
        if (!p4_lane_active(mask, i) || !idx_vec->is_valid_at(i)) continue;
        int64_t idx = idx_vec->i64_data[i];
        if (idx < 0 || static_cast<uint64_t>(idx) >= src_vec->length) {
            ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; V11_DISPATCH();
        }
        indices[i] = static_cast<uint32_t>(idx);
        valid[i] = 1u;
    }
    V11Vector* ov = p4_gather_from_vec(ws.arena, *src_vec, indices, valid, n, mask);
    if (!ov) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
    ws.v_regs[dst] = ov;
    ws.state.pc += 1u;
    V11_DISPATCH();
}
