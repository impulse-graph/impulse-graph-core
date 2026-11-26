// Opcode 0x00: OP_HALT
V11_OP_CASE(HALT) {
    return ws.status;
}
