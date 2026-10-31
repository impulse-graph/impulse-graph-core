// Opcode 0x4A: OP_DELTA_STEP_RELAX
op_DELTA_STEP_RELAX: {
    const auto& inst = bytecode[vm_state->pc];
    VALIDATE_REG(inst.dst_reg);
    vm_state->pc++;
    DISPATCH();
}
