// Opcode 0x90: OP_COLLECT_BITSET
op_COLLECT_BITSET: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src = inst.payload & 0xFFFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src);

    vm_state->registers[dst] = vm_state->registers[src];
    vm_state->register_types[dst] = vm_state->register_types[src];

    vm_state->pc++;
    DISPATCH();
}

