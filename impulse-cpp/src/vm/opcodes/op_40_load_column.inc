// Opcode 0x40: OP_LOAD_COLUMN
V11_OP_CASE(LOAD_COLUMN) {
    const uint8_t subop = IMP_V11_SUBOP(w);
    const uint8_t dst = IMP_V11_DST(w);
    const uint16_t lo = IMP_V11_IMM16_LO(w);
    const uint16_t hi = IMP_V11_IMM16_HI(w);

    if (!snap) { ws.status = IMPULSE_VM_V11_ERR_NULL_SNAPSHOT; V11_DISPATCH(); }
    const V11Vector* col = nullptr;
    if (subop == 0u) {
        if (lo >= snap->domain_count) { ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; V11_DISPATCH(); }
        col = p3_find_node_attr(snap, lo, hi);
        if (!col) { ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; V11_DISPATCH(); }
    } else if (subop == 1u) {
        if (lo >= snap->relation_count) { ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; V11_DISPATCH(); }
        col = p3_find_edge_attr(snap->relations[lo], hi);
        if (!col) { ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; V11_DISPATCH(); }
    } else {
        ws.status = IMPULSE_VM_V11_ERR_INVALID_OPCODE;
        V11_DISPATCH();
    }
    V11Vector* ov = p4_shallow_copy_vec(ws.arena, *col);
    if (!ov) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
    ws.v_regs[dst] = ov;
    ws.state.pc += 1u;
    V11_DISPATCH();
}
