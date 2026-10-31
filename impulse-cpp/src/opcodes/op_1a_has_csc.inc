// Opcode 0x1A: OP_HAS_CSC
op_HAS_CSC: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t rel = inst.payload & 0xFFFF;
    VALIDATE_REG(dst);

    bool present = false;
    if (rel < vm_state->query_context->slots.size()) {
        const auto& slot = vm_state->query_context->slots[rel];
        present = (slot.csc_offsets_ptr != nullptr && slot.csc_targets_ptr != nullptr);
    }

    vm_state->registers[dst] = present ? 1 : 0;
    vm_state->register_types[dst] = TYPE_INT64;

    if (!present) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
    else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

    vm_state->pc++;
    DISPATCH();
}

