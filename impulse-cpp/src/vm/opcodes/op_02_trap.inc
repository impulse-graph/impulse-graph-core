// Opcode 0x02: OP_TRAP
VM_OP_CASE(TRAP) {
    ws.user_throw_code = IMP_IMM32(w);
    ws.status = IMPULSE_VM_ERR_TRAP;
    VM_DISPATCH();
}
