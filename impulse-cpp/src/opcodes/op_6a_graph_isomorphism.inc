// Opcode 0x6A: OP_GRAPH_ISOMORPHISM
op_GRAPH_ISOMORPHISM: {
    const auto& inst = bytecode[vm_state->pc];
    VALIDATE_REG(inst.dst_reg);
    vm_state->pc++;
    DISPATCH();
}
