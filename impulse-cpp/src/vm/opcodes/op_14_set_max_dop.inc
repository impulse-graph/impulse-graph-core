// Opcode 0x14: OP_SET_MAX_DOP
VM_OP_CASE(SET_MAX_DOP) {
    const uint8_t subop = IMP_SUBOP(w);
    const uint8_t s1 = IMP_S1(w);
    const uint16_t imm_lo = IMP_IMM16_LO(w);
    if (subop == 0u) {
        uint16_t d = imm_lo;
        if (d == 0u) d = 1u;
        else if (d > ws.host_max_threads) d = ws.host_max_threads;
        ws.state.max_dop = d;
        ws.state.pc += 1u;
    } else if (subop == 1u) {
        if (!ws.is_r_valid(s1)) {
            ws.status = IMPULSE_VM_ERR_INVALID_OPERAND;
        } else {
            int64_t d64 = static_cast<int64_t>(ws.read_r_raw(s1));
            uint16_t d = (d64 <= 0) ? 1u
                : (d64 >= static_cast<int64_t>(ws.host_max_threads)) ? ws.host_max_threads
                : static_cast<uint16_t>(d64);
            ws.state.max_dop = d;
            ws.state.pc += 1u;
        }
    } else {
        ws.status = IMPULSE_VM_ERR_INVALID_OPCODE;
    }
    VM_DISPATCH();
}
