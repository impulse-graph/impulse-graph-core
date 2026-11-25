// Opcode 0x01: OP_NOP
V11_OP_CASE(NOP) {
    ws.state.pc += 1u;
    V11_DISPATCH();
}
