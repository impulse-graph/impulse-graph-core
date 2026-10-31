/**
 * @file op_2e_vec_math_binary.inc
 * @brief Formal Contract for OP_VEC_MATH_BINARY (0x2E).
 *
 * Mnemonic: OP_VEC_MATH_BINARY
 * Opcode: 0x2E
 * Description: Vectorized binary transcendental / algebraic mathematical evaluation.
 *
 * Operands:
 *  - dst = inst.dst_reg, src1 = payload & 0xFF, src2 = (payload >> 8) & 0xFF, func_id = (payload >> 16) & 0xFF
 *
 * Inputs:
 *  - src1, src2: FLOAT_OR_DBL | TYPE_FLOAT
 *
 * Outputs:
 *  - dst: FLOAT_OR_DBL | TYPE_FLOAT
 *
 * Flags:
 *  - Unmodified
 *
 * Context Requirement:
 *  - Requires context (query_context vector pool)
 */
#ifndef OP_2E_CONTRACT_DEFINED
#define OP_2E_CONTRACT_DEFINED 1
#endif

// Opcode 0x2E: OP_VEC_MATH_BINARY
op_VEC_MATH_BINARY: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    VALIDATE_REG(dst);

    if (!vm_state->query_context) return IMPULSE_VM_ERR_NULL_SNAPSHOT;

    uint16_t p0 = inst.payload & 0xFF;
    uint16_t p1 = (inst.payload >> 8) & 0xFF;
    uint16_t p2 = (inst.payload >> 16) & 0xFF;

    bool p0_is_vec_b = (p0 < 64 && (vm_state->register_types[p0] == TYPE_DOUBLE_VECTOR || vm_state->register_types[p0] == TYPE_FLOAT_VECTOR || vm_state->register_types[p0] == TYPE_FLOAT));
    bool p2_is_vec_b = (p2 < 64 && (vm_state->register_types[p2] == TYPE_DOUBLE_VECTOR || vm_state->register_types[p2] == TYPE_FLOAT_VECTOR || vm_state->register_types[p2] == TYPE_FLOAT));

    uint16_t src1, src2;
    uint8_t func_id;
    if (!p0_is_vec_b && p2_is_vec_b) {
        func_id = static_cast<uint8_t>(p0);
        src1 = p1;
        src2 = p2;
    } else {
        src1 = p0;
        src2 = p1;
        func_id = static_cast<uint8_t>(p2);
    }
    VALIDATE_REG(src1);
    VALIDATE_REG(src2);
    IMPULSE_CHECK_REG_TYPE(vm_state, src1, (IMP_TYPES_FLOAT_OR_DBL | IMP_TYPE_BIT(TYPE_FLOAT)));
    IMPULSE_CHECK_REG_TYPE(vm_state, src2, (IMP_TYPES_FLOAT_OR_DBL | IMP_TYPE_BIT(TYPE_FLOAT)));
    uint8_t type_tag = inst.flags;

    uint8_t t1 = vm_state->register_types[src1];
    uint8_t t2 = vm_state->register_types[src2];

    if (t1 == TYPE_FLOAT && t2 == TYPE_FLOAT) {
        uint32_t b1 = static_cast<uint32_t>(vm_state->registers[src1] & 0xFFFFFFFFULL);
        uint32_t b2 = static_cast<uint32_t>(vm_state->registers[src2] & 0xFFFFFFFFULL);
        float f1 = 0.0f, f2 = 0.0f, fout = 0.0f;
        std::memcpy(&f1, &b1, sizeof(float));
        std::memcpy(&f2, &b2, sizeof(float));
        impulse_vector_math_binary_f32(func_id, &fout, &f1, &f2, 1);
        uint32_t bout = 0;
        std::memcpy(&bout, &fout, sizeof(float));
        vm_state->registers[dst] = bout;
        vm_state->register_types[dst] = TYPE_FLOAT;
        vm_state->pc++;
        DISPATCH();
    } else if (t1 == TYPE_FLOAT || t2 == TYPE_FLOAT) {
        uint16_t vec_reg = (t1 == TYPE_FLOAT) ? src2 : src1;
        uint16_t flt_reg = (t1 == TYPE_FLOAT) ? src1 : src2;
        int h_v = static_cast<int>(vm_state->registers[vec_reg]);
        size_t v_len = (h_v >= 0 && static_cast<size_t>(h_v) < VM_MAX_VECTOR_HANDLES)
            ? vm_state->query_context->float_vector_lengths[h_v] : 0;
        size_t N = (v_len > 0) ? v_len : vm_state->query_context->max_nodes;
        uint32_t b_flt = static_cast<uint32_t>(vm_state->registers[flt_reg] & 0xFFFFFFFFULL);
        float f_val = 0.0f;
        std::memcpy(&f_val, &b_flt, sizeof(float));
        if (vm_state->register_types[dst] != TYPE_FLOAT_VECTOR) {
            int h = acquire_float_vector(vm_state->query_context);
            if (h < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
            vm_state->registers[dst] = h;
            vm_state->register_types[dst] = TYPE_FLOAT_VECTOR;
        }
        int h_dst = static_cast<int>(vm_state->registers[dst]);
        vm_state->query_context->float_vector_lengths[h_dst] = v_len;
        float* d = vm_state->query_context->float_vectors[h_dst].data();
        const float* v_ptr = vm_state->query_context->float_vectors[h_v].data();
        for (size_t i = 0; i < N; ++i) {
            float a = (t1 == TYPE_FLOAT) ? f_val : v_ptr[i];
            float b = (t2 == TYPE_FLOAT) ? f_val : v_ptr[i];
            impulse_vector_math_binary_f32(func_id, &d[i], &a, &b, 1);
        }
        vm_state->pc++;
        DISPATCH();
    }

    int h_s1 = static_cast<int>(vm_state->registers[src1]);
    int h_s2 = static_cast<int>(vm_state->registers[src2]);
    size_t N = vm_state->query_context->max_nodes;
    if (type_tag == 1 || vm_state->register_types[src1] == TYPE_DOUBLE_VECTOR) {
        size_t len1 = (h_s1 >= 0 && static_cast<size_t>(h_s1) < VM_MAX_VECTOR_HANDLES) ? vm_state->query_context->double_vector_lengths[h_s1] : 0;
        size_t len2 = (h_s2 >= 0 && static_cast<size_t>(h_s2) < VM_MAX_VECTOR_HANDLES) ? vm_state->query_context->double_vector_lengths[h_s2] : 0;
        size_t out_len = (len1 > 0 && len2 > 0) ? std::min(len1, len2) : std::max(len1, len2);
        if (out_len > 0) N = out_len;
        if (vm_state->register_types[dst] != TYPE_DOUBLE_VECTOR) {
            int h = acquire_double_vector(vm_state->query_context);
            if (h < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
            vm_state->registers[dst] = h;
            vm_state->register_types[dst] = TYPE_DOUBLE_VECTOR;
        }
        int h_dst = static_cast<int>(vm_state->registers[dst]);
        vm_state->query_context->double_vector_lengths[h_dst] = out_len;
        double* d = vm_state->query_context->double_vectors[h_dst].data();
        const double* s1 = vm_state->query_context->double_vectors[h_s1].data();
        const double* s2 = vm_state->query_context->double_vectors[h_s2].data();
        impulse_vector_math_binary_f64(func_id, d, s1, s2, N);
    } else {
        size_t len1 = (h_s1 >= 0 && static_cast<size_t>(h_s1) < VM_MAX_VECTOR_HANDLES) ? vm_state->query_context->float_vector_lengths[h_s1] : 0;
        size_t len2 = (h_s2 >= 0 && static_cast<size_t>(h_s2) < VM_MAX_VECTOR_HANDLES) ? vm_state->query_context->float_vector_lengths[h_s2] : 0;
        size_t out_len = (len1 > 0 && len2 > 0) ? std::min(len1, len2) : std::max(len1, len2);
        if (out_len > 0) N = out_len;
        if (vm_state->register_types[dst] != TYPE_FLOAT_VECTOR) {
            int h = acquire_float_vector(vm_state->query_context);
            if (h < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
            vm_state->registers[dst] = h;
            vm_state->register_types[dst] = TYPE_FLOAT_VECTOR;
        }
        int h_dst = static_cast<int>(vm_state->registers[dst]);
        vm_state->query_context->float_vector_lengths[h_dst] = out_len;
        float* d = vm_state->query_context->float_vectors[h_dst].data();
        const float* s1 = vm_state->query_context->float_vectors[h_s1].data();
        const float* s2 = vm_state->query_context->float_vectors[h_s2].data();
        impulse_vector_math_binary_f32(func_id, d, s1, s2, N);
    }
    vm_state->pc++;
    DISPATCH();
}

