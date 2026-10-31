// Opcode 0x47: OP_TC_SWEEP_BATCH
op_TC_SWEEP_BATCH: {
    const auto& inst = bytecode[vm_state->pc];
    VALIDATE_REG(inst.dst_reg);
    vm_state->pc++;
    DISPATCH();
}
