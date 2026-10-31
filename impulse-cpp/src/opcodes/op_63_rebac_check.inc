// Opcode 0x63: OP_REBAC_CHECK
op_REBAC_CHECK: {
    const auto& inst = bytecode[vm_state->pc];
    VALIDATE_REG(inst.dst_reg);
    vm_state->pc++;
    DISPATCH();
}
