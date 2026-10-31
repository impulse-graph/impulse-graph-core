// Opcode 0x62: OP_SCATTER_GATHER
op_SCATTER_GATHER: {
    const auto& inst = bytecode[vm_state->pc];
    VALIDATE_REG(inst.dst_reg);
    vm_state->pc++;
    DISPATCH();
}
