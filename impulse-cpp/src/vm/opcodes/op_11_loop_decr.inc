// Opcode 0x11: OP_LOOP_DECR
V11_OP_CASE(LOOP_DECR) {
    const uint8_t dst = IMP_V11_DST(w);
    const int32_t imm32_s = IMP_V11_IMM32_S(w);
    if (!ws.is_r_valid(dst)) {
        ws.status = IMPULSE_VM_V11_ERR_INVALID_OPERAND;
    } else {
        uint64_t next_u = ws.read_r_raw(dst) - 1ULL;
        int64_t next_s = static_cast<int64_t>(next_u);
        uint8_t ty = ws.read_r_type(dst);
        ws.write_r(dst, ty, next_u);
        ws.set_zf(next_s == 0);
        ws.state.pc = (next_s > 0)
            ? static_cast<uint32_t>(static_cast<int64_t>(ws.state.pc) + imm32_s)
            : (ws.state.pc + 1u);
    }
    V11_DISPATCH();
}
