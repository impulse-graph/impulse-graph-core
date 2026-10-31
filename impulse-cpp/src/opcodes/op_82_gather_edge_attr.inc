// Opcode 0x82: OP_GATHER_EDGE_ATTR
op_GATHER_EDGE_ATTR: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src = (inst.flags & 0x80) ? (inst.payload & 0xFFFF) : (inst.payload & 0xFF);
    if (inst.flags & 0x80) {
        if (vm_state->pc + 1 >= instruction_count) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        vm_state->pc++;
    }
    VALIDATE_REG(dst);
    VALIDATE_REG(src);

    if (vm_state->register_types[dst] != TYPE_FLOAT_VECTOR) {
        int h = acquire_float_vector(vm_state->query_context);
        if (h < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        vm_state->registers[dst] = h;
        vm_state->register_types[dst] = TYPE_FLOAT_VECTOR;
    }
    vm_state->pc++;
    DISPATCH();
}

