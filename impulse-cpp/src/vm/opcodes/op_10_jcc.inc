// Opcode 0x10: OP_JCC
V11_OP_CASE(JCC) {
    const uint8_t subop = IMP_V11_SUBOP(w);
    const uint8_t s1 = IMP_V11_S1(w);
    const int32_t imm32_s = IMP_V11_IMM32_S(w);
    bool take = false;
    switch (subop) {
        case 0: take = ws.flag_zf(); break;
        case 1: take = !ws.flag_zf(); break;
        case 2: take = ws.flag_st(); break;
        case 3: take = !ws.flag_st(); break;
        case 4: take = ws.flag_nf(); break;
        case 5: take = !ws.flag_nf(); break;
        case 6: take = ws.is_r_valid(s1) && ws.read_r_raw(s1) != 0ULL; break;
        case 7: take = !ws.is_r_valid(s1) || ws.read_r_raw(s1) == 0ULL; break;
        case 8: take = p1_is_frontier_empty(ws, s1); break;
        case 9: take = !p1_is_frontier_empty(ws, s1); break;
        default: take = false; break;
    }
    ws.state.pc = take
        ? static_cast<uint32_t>(static_cast<int64_t>(ws.state.pc) + imm32_s)
        : (ws.state.pc + 1u);
    V11_DISPATCH();
}
