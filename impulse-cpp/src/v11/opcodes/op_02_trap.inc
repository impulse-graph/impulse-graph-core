// Opcode 0x02: OP_TRAP
V11_OP_CASE(TRAP) {
    ws.user_throw_code = IMP_V11_IMM32(w);
    ws.status = IMPULSE_VM_V11_ERR_TRAP;
    V11_DISPATCH();
}
