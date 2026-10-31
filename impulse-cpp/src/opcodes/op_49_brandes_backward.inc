// Opcode 0x49: OP_BRANDES_BACKWARD
op_BRANDES_BACKWARD: {
    const auto& inst = bytecode[vm_state->pc];
    VALIDATE_REG(inst.dst_reg);
    vm_state->pc++;
    DISPATCH();
}
