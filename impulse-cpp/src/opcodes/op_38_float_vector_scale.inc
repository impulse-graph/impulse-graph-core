// Opcode 0x38: OP_FLOAT_VECTOR_SCALE
op_FLOAT_VECTOR_SCALE: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    VALIDATE_REG(dst);

    float factor = 1.0f;
    if (inst.flags == 0) {
        factor = reinterpret_cast<const float&>(inst.payload);
    } else {
        uint16_t src_reg = inst.payload & 0xFFFF;
        VALIDATE_REG(src_reg);
        if (vm_state->register_types[src_reg] == TYPE_FLOAT) {
            factor = reinterpret_cast<const float&>(vm_state->registers[src_reg]);
        } else {
            factor = static_cast<float>(vm_state->registers[src_reg]);
        }
    }

    int handle = static_cast<int>(vm_state->registers[dst]);
    if (vm_state->register_types[dst] == TYPE_FLOAT_VECTOR && handle >= 0 && handle < static_cast<int>(vm_state->query_context->float_vectors.size())) {
        float* vec = vm_state->query_context->float_vectors[handle].data();
        size_t size = vm_state->query_context->max_nodes;
        impulse_simd_vector_scale_f32(vec, factor, size);
    }

    vm_state->pc++;
    DISPATCH();
}

