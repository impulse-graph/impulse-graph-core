// Opcode 0x36: OP_VECTOR_DIV
op_VECTOR_DIV: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t num = inst.payload & 0xFFFF;
    uint16_t denom = (inst.payload >> 16) & 0xFFFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(num);
    VALIDATE_REG(denom);

    size_t size = vm_state->query_context->max_nodes;
    bool num_is_double = (vm_state->register_types[num] == TYPE_DOUBLE_VECTOR);
    bool denom_is_double = (vm_state->register_types[denom] == TYPE_DOUBLE_VECTOR);

    if (num_is_double || denom_is_double) {
        int h_dst = -1;
        if (vm_state->register_types[dst] == TYPE_DOUBLE_VECTOR) {
            h_dst = static_cast<int>(vm_state->registers[dst]);
        } else {
            h_dst = acquire_double_vector(vm_state->query_context);
            if (h_dst < 0) {
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
        }
        double* dst_vec = vm_state->query_context->double_vectors[h_dst].data();

        const double* num_dvec = (vm_state->register_types[num] == TYPE_DOUBLE_VECTOR)
            ? vm_state->query_context->double_vectors[vm_state->registers[num]].data() : nullptr;
        const float* num_fvec = (vm_state->register_types[num] == TYPE_FLOAT_VECTOR)
            ? vm_state->query_context->float_vectors[vm_state->registers[num]].data() : nullptr;
        double num_scalar = 0.0;
        if (!num_dvec && !num_fvec) {
            if (vm_state->register_types[num] == TYPE_DOUBLE) num_scalar = reinterpret_cast<const double&>(vm_state->registers[num]);
            else if (vm_state->register_types[num] == TYPE_FLOAT) num_scalar = reinterpret_cast<const float&>(vm_state->registers[num]);
            else num_scalar = static_cast<double>(vm_state->registers[num]);
        }

        const double* denom_dvec = (vm_state->register_types[denom] == TYPE_DOUBLE_VECTOR)
            ? vm_state->query_context->double_vectors[vm_state->registers[denom]].data() : nullptr;
        const float* denom_fvec = (vm_state->register_types[denom] == TYPE_FLOAT_VECTOR)
            ? vm_state->query_context->float_vectors[vm_state->registers[denom]].data() : nullptr;
        double denom_scalar = 0.0;
        if (!denom_dvec && !denom_fvec) {
            if (vm_state->register_types[denom] == TYPE_DOUBLE) denom_scalar = reinterpret_cast<const double&>(vm_state->registers[denom]);
            else if (vm_state->register_types[denom] == TYPE_FLOAT) denom_scalar = reinterpret_cast<const float&>(vm_state->registers[denom]);
            else denom_scalar = static_cast<double>(vm_state->registers[denom]);
        }

#if defined(_OPENMP)
        #pragma omp parallel for schedule(static)
#endif
        for (size_t i = 0; i < size; ++i) {
            double nv = num_dvec ? num_dvec[i] : (num_fvec ? static_cast<double>(num_fvec[i]) : num_scalar);
            double dv = denom_dvec ? denom_dvec[i] : (denom_fvec ? static_cast<double>(denom_fvec[i]) : denom_scalar);
            dst_vec[i] = (dv != 0.0) ? (nv / dv) : 0.0;
        }

        vm_state->registers[dst] = h_dst;
        vm_state->register_types[dst] = TYPE_DOUBLE_VECTOR;
    } else {
        int h_dst = -1;
        if (vm_state->register_types[dst] == TYPE_FLOAT_VECTOR) {
            h_dst = static_cast<int>(vm_state->registers[dst]);
        } else {
            h_dst = acquire_float_vector(vm_state->query_context);
            if (h_dst < 0) {
                return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
            }
        }
        float* dst_vec = vm_state->query_context->float_vectors[h_dst].data();

        const float* num_fvec = (vm_state->register_types[num] == TYPE_FLOAT_VECTOR)
            ? vm_state->query_context->float_vectors[vm_state->registers[num]].data() : nullptr;
        float num_scalar = 0.0f;
        if (!num_fvec) {
            if (vm_state->register_types[num] == TYPE_FLOAT) num_scalar = reinterpret_cast<const float&>(vm_state->registers[num]);
            else num_scalar = static_cast<float>(vm_state->registers[num]);
        }

        const float* denom_fvec = (vm_state->register_types[denom] == TYPE_FLOAT_VECTOR)
            ? vm_state->query_context->float_vectors[vm_state->registers[denom]].data() : nullptr;
        float denom_scalar = 0.0f;
        if (!denom_fvec) {
            if (vm_state->register_types[denom] == TYPE_FLOAT) denom_scalar = reinterpret_cast<const float&>(vm_state->registers[denom]);
            else denom_scalar = static_cast<float>(vm_state->registers[denom]);
        }

#if defined(_OPENMP)
        #pragma omp parallel for schedule(static)
#endif
        for (size_t i = 0; i < size; ++i) {
            float nv = num_fvec ? num_fvec[i] : num_scalar;
            float dv = denom_fvec ? denom_fvec[i] : denom_scalar;
            dst_vec[i] = (dv != 0.0f) ? (nv / dv) : 0.0f;
        }

        vm_state->registers[dst] = h_dst;
        vm_state->register_types[dst] = TYPE_FLOAT_VECTOR;
    }

    vm_state->pc++;
    DISPATCH();
}

