// Opcode 0x0F: OP_JMP
VM_OP_CASE(JMP) {
    ws.state.pc = static_cast<uint32_t>(static_cast<int64_t>(ws.state.pc) + IMP_IMM32_S(w));
    VM_DISPATCH();
}
