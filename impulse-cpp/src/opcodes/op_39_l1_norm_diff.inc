// Opcode 0x39: OP_L1_NORM_DIFF
op_L1_NORM_DIFF: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src1 = inst.payload & 0xFFFF;
    uint16_t src2 = (inst.payload >> 16) & 0xFFFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src1);
    VALIDATE_REG(src2);

    size_t N = vm_state->query_context->max_nodes;
    float l1_diff = 0.0f;

    if (vm_state->register_types[src1] == TYPE_FLOAT_VECTOR && vm_state->register_types[src2] == TYPE_FLOAT_VECTOR) {
        int h1 = static_cast<int>(vm_state->registers[src1]);
        int h2 = static_cast<int>(vm_state->registers[src2]);
        if (h1 >= 0 && h1 < static_cast<int>(vm_state->query_context->float_vectors.size()) &&
            h2 >= 0 && h2 < static_cast<int>(vm_state->query_context->float_vectors.size())) {
            const float* vec1 = vm_state->query_context->float_vectors[h1].data();
            const float* vec2 = vm_state->query_context->float_vectors[h2].data();

            #pragma omp parallel for reduction(+:l1_diff) schedule(static)
            for (size_t i = 0; i < N; ++i) {
                l1_diff += std::abs(vec1[i] - vec2[i]);
            }
        }
    } else if ((vm_state->register_types[src1] == TYPE_FLOAT || vm_state->register_types[src1] == TYPE_INT64) &&
               (vm_state->register_types[src2] == TYPE_FLOAT || vm_state->register_types[src2] == TYPE_INT64)) {
        float val1 = (vm_state->register_types[src1] == TYPE_FLOAT) ?
            reinterpret_cast<float&>(vm_state->registers[src1]) : static_cast<float>(vm_state->registers[src1]);
        float val2 = (vm_state->register_types[src2] == TYPE_FLOAT) ?
            reinterpret_cast<float&>(vm_state->registers[src2]) : static_cast<float>(vm_state->registers[src2]);
        l1_diff = std::abs(val1 - val2);
    }

    vm_state->registers[dst] = 0;
    reinterpret_cast<float&>(vm_state->registers[dst]) = l1_diff;
    vm_state->register_types[dst] = TYPE_FLOAT;

    if (l1_diff < 1e-4f) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
    else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

    vm_state->pc++;
    DISPATCH();
}

