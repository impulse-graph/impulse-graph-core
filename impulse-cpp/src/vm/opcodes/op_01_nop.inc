// Opcode 0x01: OP_NOP
VM_OP_CASE(NOP) {
    ws.state.pc += 1u;
    VM_DISPATCH();
}
