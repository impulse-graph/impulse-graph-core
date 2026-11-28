// Opcode 0x3A: OP_HAS_TOPOLOGY
VM_OP_CASE(HAS_TOPOLOGY) {
    const uint8_t subop = IMP_SUBOP(w);
    const uint8_t dst = IMP_DST(w);
    const uint16_t lo = IMP_IMM16_LO(w);
    const uint16_t hi = IMP_IMM16_HI(w);

    if (!snap) { ws.status = IMPULSE_VM_ERR_NULL_SNAPSHOT; VM_DISPATCH(); }

    uint64_t val = 0ULL;
    switch (subop) {
        case 0: val = (lo < snap->relation_count && snap->relations[lo].has_csr) ? 1ULL : 0ULL; break;
        case 1: val = (lo < snap->relation_count && snap->relations[lo].has_csc) ? 1ULL : 0ULL; break;
        case 2: val = (lo < snap->relation_count && snap->relations[lo].has_coo) ? 1ULL : 0ULL; break;
        case 3: val = (lo < snap->relation_count && snap->relations[lo].has_dense) ? 1ULL : 0ULL; break;
        case 4: val = p3_has_key_catalog(snap, lo) ? 1ULL : 0ULL; break;
        case 5: val = (p3_find_node_attr(snap, lo, hi) != nullptr) ? 1ULL : 0ULL; break;
        case 6: val = (lo < snap->relation_count && p3_find_edge_attr(snap->relations[lo], hi) != nullptr) ? 1ULL : 0ULL; break;
        case 7: val = p3_has_index(snap, lo, hi) ? 1ULL : 0ULL; break;
        case 8:
            if (lo >= snap->domain_count) { ws.status = IMPULSE_VM_ERR_OUT_OF_BOUNDS; VM_DISPATCH(); }
            val = snap->domain_cards[lo];
            break;
        case 9:
            if (lo >= snap->relation_count) { ws.status = IMPULSE_VM_ERR_OUT_OF_BOUNDS; VM_DISPATCH(); }
            val = snap->relations[lo].col_tgt_len;
            break;
        default:
            ws.status = IMPULSE_VM_ERR_INVALID_OPCODE;
            VM_DISPATCH();
    }
    ws.write_r(dst, static_cast<uint8_t>(ScalarType::SCALAR_U64), val);
    p1_set_scalar_flags(ws, true, val);
    ws.state.pc += 1u;
    VM_DISPATCH();
}
