// Opcode 0x67: OP_LOUVAIN_MODULARITY
op_LOUVAIN_MODULARITY: {
    const auto& inst = bytecode[vm_state->pc];
    VALIDATE_REG(inst.dst_reg);
    vm_state->pc++;
    DISPATCH();
}
