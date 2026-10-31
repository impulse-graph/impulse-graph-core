// Opcode 0x60: OP_SAMPLE_NEIGHBORS
op_SAMPLE_NEIGHBORS: {
    const auto& inst = bytecode[vm_state->pc];
    VALIDATE_REG(inst.dst_reg);
    vm_state->pc++;
    DISPATCH();
}
