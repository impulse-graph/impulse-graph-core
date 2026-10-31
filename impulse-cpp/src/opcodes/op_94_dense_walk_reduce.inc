// Opcode 0x94: OP_DENSE_WALK_REDUCE
op_DENSE_WALK_REDUCE: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    VALIDATE_REG(dst);

    if (vm_state->register_types[dst] != TYPE_FLOAT_VECTOR) {
        int h = acquire_float_vector(vm_state->query_context);
        if (h < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        vm_state->registers[dst] = h;
        vm_state->register_types[dst] = TYPE_FLOAT_VECTOR;
    }
    vm_state->pc++;
    DISPATCH();
}

