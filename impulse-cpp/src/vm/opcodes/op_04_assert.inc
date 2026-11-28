// Opcode 0x04: OP_ASSERT
VM_OP_CASE(ASSERT) {
    const uint8_t subop = IMP_SUBOP(w);
    const uint8_t s1 = IMP_S1(w);
    bool ok = false;
    switch (subop) {
        case 0: ok = ws.is_r_valid(s1) && ws.read_r_raw(s1) != 0ULL; break;
        case 1: ok = !p1_is_frontier_empty(ws, s1); break;
        case 2: ok = p1_is_frontier_empty(ws, s1); break;
        case 3: ok = ws.flag_zf(); break;
        case 4: ok = !ws.flag_zf(); break;
        case 5: ok = ws.flag_st(); break;
        case 6: ok = ws.is_r_valid(s1); break;
        case 7: ok = !ws.is_r_valid(s1); break;
        default: ok = false; break;
    }
    if (ok) ws.state.pc += 1u;
    else ws.status = IMPULSE_VM_ERR_ASSERTION_FAILED;
    VM_DISPATCH();
}
