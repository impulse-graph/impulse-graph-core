// Opcode 0x48: OP_BRANDES_FORWARD
op_BRANDES_FORWARD: {
    const auto& inst = bytecode[vm_state->pc];
    VALIDATE_REG(inst.dst_reg);
    vm_state->pc++;
    DISPATCH();
}
