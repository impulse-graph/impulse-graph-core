// Opcode 0x66: OP_SPARSE_MATVEC
op_SPARSE_MATVEC: {
    const auto& inst = bytecode[vm_state->pc];
    VALIDATE_REG(inst.dst_reg);
    vm_state->pc++;
    DISPATCH();
}
