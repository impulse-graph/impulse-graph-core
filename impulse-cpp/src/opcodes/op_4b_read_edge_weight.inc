// Opcode 0x4B: OP_READ_EDGE_WEIGHT
op_READ_EDGE_WEIGHT: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint8_t u_reg = inst.payload & 0xFF;
    uint8_t v_reg = (inst.payload >> 8) & 0xFF;
    uint16_t rel;
    if (inst.flags & 0x80) {
        if (vm_state->pc + 1 >= instruction_count) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        rel = bytecode[vm_state->pc + 1].dst_reg;
        vm_state->pc++;
    } else {
        rel = (inst.payload >> 16) & 0xFFFF;
    }
    VALIDATE_REG(dst);
    VALIDATE_REG(u_reg);
    VALIDATE_REG(v_reg);

    float weight = 1.0f;
    uint64_t u = vm_state->registers[u_reg];
    uint64_t v = vm_state->registers[v_reg];
    (void)v;

    if (rel < vm_state->query_context->attribute_slots.size() &&
        !vm_state->query_context->attribute_slots[rel].empty() &&
        vm_state->query_context->attribute_slots[rel][0].data_ptr != nullptr) {
        const auto& attr = vm_state->query_context->attribute_slots[rel][0];
        const float* weights = static_cast<const float*>(attr.data_ptr);
        if (weights && u < vm_state->query_context->max_nodes) {
            weight = weights[u];
        }
    }

    vm_state->registers[dst] = 0;
    reinterpret_cast<float&>(vm_state->registers[dst]) = weight;
    vm_state->register_types[dst] = TYPE_FLOAT;

    vm_state->pc++;
    DISPATCH();
}

