// Opcode 0x00: OP_HALT
VM_OP_CASE(HALT) {
    return ws.status;
}
