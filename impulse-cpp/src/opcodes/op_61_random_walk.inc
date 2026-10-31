// Opcode 0x61: OP_RANDOM_WALK
op_RANDOM_WALK: {
    const auto& inst = bytecode[vm_state->pc];
    VALIDATE_REG(inst.dst_reg);
    vm_state->pc++;
    DISPATCH();
}
