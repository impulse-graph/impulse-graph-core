// Opcode 0x0F: OP_JMP
V11_OP_CASE(JMP) {
    ws.state.pc = static_cast<uint32_t>(static_cast<int64_t>(ws.state.pc) + IMP_V11_IMM32_S(w));
    V11_DISPATCH();
}
