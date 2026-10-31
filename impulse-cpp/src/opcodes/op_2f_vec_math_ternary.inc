// Opcode 0x2F: OP_VEC_MATH_TERNARY
op_VEC_MATH_TERNARY: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    VALIDATE_REG(dst);

    if (!vm_state->query_context) return IMPULSE_VM_ERR_NULL_SNAPSHOT;

    uint16_t p0 = inst.payload & 0xFF;
    uint16_t p1 = (inst.payload >> 8) & 0xFF;
    uint16_t p2 = (inst.payload >> 16) & 0xFF;
    uint16_t p3 = (inst.payload >> 24) & 0xFF;

    bool p0_is_vec_t = (p0 < 64 && (vm_state->register_types[p0] == TYPE_DOUBLE_VECTOR || vm_state->register_types[p0] == TYPE_FLOAT_VECTOR));
    bool p3_is_vec_t = (p3 < 64 && (vm_state->register_types[p3] == TYPE_DOUBLE_VECTOR || vm_state->register_types[p3] == TYPE_FLOAT_VECTOR));

    uint16_t src1, src2, src3;
    uint8_t func_id;
    if (!p0_is_vec_t && p3_is_vec_t) {
        func_id = static_cast<uint8_t>(p0);
        src1 = p1;
        src2 = p2;
        src3 = p3;
    } else {
        src1 = p0;
        src2 = p1;
        src3 = p2;
        func_id = static_cast<uint8_t>(p3);
    }
    VALIDATE_REG(src1);
    VALIDATE_REG(src2);
    VALIDATE_REG(src3);
    uint8_t type_tag = inst.flags;

    if (vm_state->register_types[src1] != TYPE_DOUBLE_VECTOR && vm_state->register_types[src1] != TYPE_FLOAT_VECTOR) {
        return IMPULSE_VM_ERR_INVALID_REGISTER;
    }
    if (vm_state->register_types[src2] != TYPE_DOUBLE_VECTOR && vm_state->register_types[src2] != TYPE_FLOAT_VECTOR) {
        return IMPULSE_VM_ERR_INVALID_REGISTER;
    }
    if (vm_state->register_types[src3] != TYPE_DOUBLE_VECTOR && vm_state->register_types[src3] != TYPE_FLOAT_VECTOR) {
        return IMPULSE_VM_ERR_INVALID_REGISTER;
    }
    int h_s1 = static_cast<int>(vm_state->registers[src1]);
    int h_s2 = static_cast<int>(vm_state->registers[src2]);
    int h_s3 = static_cast<int>(vm_state->registers[src3]);
    size_t N = vm_state->query_context->max_nodes;
    if (type_tag == 1 || vm_state->register_types[src1] == TYPE_DOUBLE_VECTOR) {
        size_t len1 = (h_s1 >= 0 && static_cast<size_t>(h_s1) < VM_MAX_VECTOR_HANDLES) ? vm_state->query_context->double_vector_lengths[h_s1] : 0;
        if (len1 > 0) N = len1;
        if (vm_state->register_types[dst] != TYPE_DOUBLE_VECTOR) {
            int h = acquire_double_vector(vm_state->query_context);
            if (h < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
            vm_state->registers[dst] = h;
            vm_state->register_types[dst] = TYPE_DOUBLE_VECTOR;
        }
        int h_dst = static_cast<int>(vm_state->registers[dst]);
        vm_state->query_context->double_vector_lengths[h_dst] = len1;
        double* d = vm_state->query_context->double_vectors[h_dst].data();
        const double* s1 = vm_state->query_context->double_vectors[h_s1].data();
        const double* s2 = vm_state->query_context->double_vectors[h_s2].data();
        const double* s3 = vm_state->query_context->double_vectors[h_s3].data();
        impulse_vector_math_ternary_f64(func_id, d, s1, s2, s3, N);
    } else {
        size_t len1 = (h_s1 >= 0 && static_cast<size_t>(h_s1) < VM_MAX_VECTOR_HANDLES) ? vm_state->query_context->float_vector_lengths[h_s1] : 0;
        if (len1 > 0) N = len1;
        if (vm_state->register_types[dst] != TYPE_FLOAT_VECTOR) {
            int h = acquire_float_vector(vm_state->query_context);
            if (h < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
            vm_state->registers[dst] = h;
            vm_state->register_types[dst] = TYPE_FLOAT_VECTOR;
        }
        int h_dst = static_cast<int>(vm_state->registers[dst]);
        vm_state->query_context->float_vector_lengths[h_dst] = len1;
        float* d = vm_state->query_context->float_vectors[h_dst].data();
        const float* s1 = vm_state->query_context->float_vectors[h_s1].data();
        const float* s2 = vm_state->query_context->float_vectors[h_s2].data();
        const float* s3 = vm_state->query_context->float_vectors[h_s3].data();
        impulse_vector_math_ternary_f32(func_id, d, s1, s2, s3, N);
    }
    vm_state->pc++;
    DISPATCH();
}

