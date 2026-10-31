// Opcode 0x3A: OP_PROJECT_STATE
op_PROJECT_STATE: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t src = inst.payload & 0xFFFF;
    if (src == 0 && inst.payload != 0) src = (inst.payload >> 16) & 0xFFFF;
    uint16_t dst = inst.dst_reg;
    if (vm_state->register_types[src] != TYPE_FRONTIER_STATE) {
        return IMPULSE_VM_ERR_INVALID_REGISTER;
    }
    vm_state->register_types[dst] = TYPE_FRONTIER_STATE;
    vm_state->registers[dst] = vm_state->registers[src];
    vm_state->pc++;
    DISPATCH();
}

