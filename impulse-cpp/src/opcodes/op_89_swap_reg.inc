// Opcode 0x89: OP_SWAP_REG
op_SWAP_REG: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t r1 = inst.dst_reg;
    uint16_t r2 = inst.payload & 0xFFFF;
    VALIDATE_REG(r1);
    VALIDATE_REG(r2);

    std::swap(vm_state->registers[r1], vm_state->registers[r2]);
    std::swap(vm_state->register_types[r1], vm_state->register_types[r2]);

    vm_state->pc++;
    DISPATCH();
}

