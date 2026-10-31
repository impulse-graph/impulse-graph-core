// Opcode 0x69: OP_MOTIF_MATCH_3
op_MOTIF_MATCH_3: {
    const auto& inst = bytecode[vm_state->pc];
    VALIDATE_REG(inst.dst_reg);
    vm_state->pc++;
    DISPATCH();
}
