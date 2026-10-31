// Opcode 0x71: OP_CLEAR_REG
op_CLEAR_REG: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    VALIDATE_REG(dst);
    if (vm_state->register_types[dst] == TYPE_BITSET_HANDLE) {
        release_bitset(vm_state->query_context, static_cast<int>(vm_state->registers[dst]));
    }
    vm_state->registers[dst] = 0;
    vm_state->register_types[dst] = TYPE_NULL;
    vm_state->pc++;
    DISPATCH();
}



