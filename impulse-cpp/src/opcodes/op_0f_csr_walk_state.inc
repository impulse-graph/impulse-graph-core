// Opcode 0x0F: OP_CSR_WALK_STATE
op_CSR_WALK_STATE: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    if (inst.flags & 0x80) {
        if (vm_state->pc + 1 >= instruction_count) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        uint16_t rel = bytecode[vm_state->pc + 1].dst_reg;
        vm_state->pc++;
        if (rel >= vm_state->query_context->slots.size()) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
    VALIDATE_REG(dst);
    vm_state->register_types[dst] = TYPE_FRONTIER_STATE;
    vm_state->registers[dst] = 0;
    vm_state->pc++;
    DISPATCH();
}

