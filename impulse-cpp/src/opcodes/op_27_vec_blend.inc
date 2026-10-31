// Opcode 0x27: OP_VEC_BLEND
op_VEC_BLEND: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t mask_reg = inst.payload & 0xFF;
    uint16_t src_true = (inst.payload >> 8) & 0xFF;
    uint16_t src_false = (inst.payload >> 16) & 0xFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(mask_reg);
    VALIDATE_REG(src_true);
    VALIDATE_REG(src_false);

    int h_mask = static_cast<int>(vm_state->registers[mask_reg]);
    int h_true = static_cast<int>(vm_state->registers[src_true]);
    int h_false = static_cast<int>(vm_state->registers[src_false]);
    uint8_t type_true = vm_state->register_types[src_true];
    const auto& mask_bs = vm_state->query_context->bitsets[h_mask];
    size_t N = vm_state->query_context->max_nodes;

    if (type_true == TYPE_DOUBLE_VECTOR) {
        if (vm_state->register_types[dst] != TYPE_DOUBLE_VECTOR) {
            int h = acquire_double_vector(vm_state->query_context);
            if (h < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
            vm_state->registers[dst] = h;
            vm_state->register_types[dst] = TYPE_DOUBLE_VECTOR;
        }
        int h_dst = static_cast<int>(vm_state->registers[dst]);
        double* d = vm_state->query_context->double_vectors[h_dst].data();
        const double* vt = vm_state->query_context->double_vectors[h_true].data();
        const double* vf = vm_state->query_context->double_vectors[h_false].data();
        for (size_t i = 0; i < N; ++i) {
            d[i] = mask_bs.test(i) ? vt[i] : vf[i];
        }
    } else {
        if (vm_state->register_types[dst] != TYPE_FLOAT_VECTOR) {
            int h = acquire_float_vector(vm_state->query_context);
            if (h < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
            vm_state->registers[dst] = h;
            vm_state->register_types[dst] = TYPE_FLOAT_VECTOR;
        }
        int h_dst = static_cast<int>(vm_state->registers[dst]);
        float* d = vm_state->query_context->float_vectors[h_dst].data();
        const float* vt = vm_state->query_context->float_vectors[h_true].data();
        const float* vf = vm_state->query_context->float_vectors[h_false].data();
        for (size_t i = 0; i < N; ++i) {
            d[i] = mask_bs.test(i) ? vt[i] : vf[i];
        }
    }
    vm_state->pc++;
    DISPATCH();
}

