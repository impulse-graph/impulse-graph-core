// Opcode 0x35: OP_VECTOR_REDUCE_SUM
op_VECTOR_REDUCE_SUM: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src = inst.payload & 0xFFFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src);

    size_t size = vm_state->query_context->max_nodes;
    double sum = 0.0;

    if (vm_state->register_types[src] == TYPE_DOUBLE_VECTOR) {
        size_t h = vm_state->registers[src];
        if (h < VM_MAX_VECTOR_HANDLES && vm_state->query_context->double_vector_lengths[h] > 0) {
            size = std::min(size, vm_state->query_context->double_vector_lengths[h]);
        }
        const double* vec = vm_state->query_context->double_vectors[h].data();
        for (size_t i = 0; i < size; ++i) {
            if (!std::isnan(vec[i])) sum += vec[i];
        }
        vm_state->registers[dst] = reinterpret_cast<uint64_t&>(sum);
        vm_state->register_types[dst] = TYPE_DOUBLE;
    } else if (vm_state->register_types[src] == TYPE_FLOAT_VECTOR) {
        size_t h = vm_state->registers[src];
        if (h < VM_MAX_VECTOR_HANDLES && vm_state->query_context->float_vector_lengths[h] > 0) {
            size = std::min(size, vm_state->query_context->float_vector_lengths[h]);
        }
        const float* vec = vm_state->query_context->float_vectors[h].data();
        float fsum = 0.0f;
        for (size_t i = 0; i < size; ++i) {
            if (!std::isnan(vec[i])) fsum += vec[i];
        }
        vm_state->registers[dst] = 0;
        reinterpret_cast<float&>(vm_state->registers[dst]) = fsum;
        vm_state->register_types[dst] = TYPE_FLOAT;
        sum = fsum;
    } else if (vm_state->register_types[src] == TYPE_FLOAT) {
        uint32_t bits = static_cast<uint32_t>(vm_state->registers[src] & 0xFFFFFFFFULL);
        float fval;
        std::memcpy(&fval, &bits, sizeof(float));
        vm_state->registers[dst] = 0;
        reinterpret_cast<float&>(vm_state->registers[dst]) = fval;
        vm_state->register_types[dst] = TYPE_FLOAT;
        sum = fval;
    } else if (vm_state->register_types[src] == TYPE_DOUBLE) {
        double dval;
        uint64_t bits = vm_state->registers[src];
        std::memcpy(&dval, &bits, sizeof(double));
        vm_state->registers[dst] = reinterpret_cast<uint64_t&>(dval);
        vm_state->register_types[dst] = TYPE_DOUBLE;
        sum = dval;
    } else {
        vm_state->registers[dst] = 0;
        vm_state->register_types[dst] = TYPE_NULL;
    }

    if (sum == 0.0) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
    else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

    vm_state->pc++;
    DISPATCH();
}

