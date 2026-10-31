// Opcode 0x1C: OP_HAS_KEY_CATALOG
op_HAS_KEY_CATALOG: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t domain_id = inst.payload & 0xFFFF;
    VALIDATE_REG(dst);

    bool present = true;
    if (vm_state->query_context && domain_id < vm_state->query_context->string_vectors.size()) {
        present = !vm_state->query_context->string_vectors[domain_id].empty();
    }

    vm_state->registers[dst] = present ? 1 : 0;
    vm_state->register_types[dst] = TYPE_INT64;

    if (!present) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
    else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

    vm_state->pc++;
    DISPATCH();
}

