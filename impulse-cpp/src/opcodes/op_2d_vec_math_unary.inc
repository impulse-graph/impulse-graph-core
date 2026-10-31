/**
 * @file op_2d_vec_math_unary.inc
 * @brief Formal Contract for OP_VEC_MATH_UNARY (0x2D).
 *
 * Mnemonic: OP_VEC_MATH_UNARY
 * Opcode: 0x2D
 * Description: Vectorized unary transcendental / algebraic mathematical evaluation.
 *
 * Operands:
 *  - dst = inst.dst_reg, src = payload & 0xFF, func_id = (payload >> 8) & 0xFF
 *
 * Inputs:
 *  - src: FLOAT_OR_DBL | TYPE_FLOAT
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
#ifndef OP_2D_CONTRACT_DEFINED
#define OP_2D_CONTRACT_DEFINED 1
#endif

// Opcode 0x2D: OP_VEC_MATH_UNARY
op_VEC_MATH_UNARY: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    VALIDATE_REG(dst);

    if (!vm_state->query_context) return IMPULSE_VM_ERR_NULL_SNAPSHOT;

    uint16_t p0 = inst.payload & 0xFF;
    uint16_t p1 = (inst.payload >> 8) & 0xFF;
    uint16_t p2 = (inst.payload >> 16) & 0xFF;

    bool p0_is_vec = (p0 < 64 && (vm_state->register_types[p0] == TYPE_DOUBLE_VECTOR || vm_state->register_types[p0] == TYPE_FLOAT_VECTOR || vm_state->register_types[p0] == TYPE_FLOAT));
    bool p1_is_vec = (p1 < 64 && (vm_state->register_types[p1] == TYPE_DOUBLE_VECTOR || vm_state->register_types[p1] == TYPE_FLOAT_VECTOR || vm_state->register_types[p1] == TYPE_FLOAT));

    uint16_t src;
    uint8_t func_id;
    if (!p0_is_vec && p1_is_vec) {
        src = p1;
        func_id = static_cast<uint8_t>(p0);
    } else {
        src = p0;
        func_id = static_cast<uint8_t>(p1 ? p1 : p2);
    }
    VALIDATE_REG(src);
    IMPULSE_CHECK_REG_TYPE(vm_state, src, (IMP_TYPES_FLOAT_OR_DBL | IMP_TYPE_BIT(TYPE_FLOAT)));

    uint8_t type_tag = inst.flags;

    if (vm_state->register_types[src] == TYPE_FLOAT) {
        uint32_t bits_in = static_cast<uint32_t>(vm_state->registers[src] & 0xFFFFFFFFULL);
        float f_in = 0.0f, f_out = 0.0f;
        std::memcpy(&f_in, &bits_in, sizeof(float));
        impulse_vector_math_unary_f32(func_id, &f_out, &f_in, 1);
        uint32_t bits_out = 0;
        std::memcpy(&bits_out, &f_out, sizeof(float));
        vm_state->registers[dst] = bits_out;
        vm_state->register_types[dst] = TYPE_FLOAT;
        vm_state->pc++;
        DISPATCH();
    }

    if (vm_state->register_types[src] != TYPE_DOUBLE_VECTOR && vm_state->register_types[src] != TYPE_FLOAT_VECTOR) {
        return IMPULSE_VM_ERR_INVALID_REGISTER;
    }

    int h_src = static_cast<int>(vm_state->registers[src]);
    size_t N = vm_state->query_context->max_nodes;
    if (type_tag == 1 || vm_state->register_types[src] == TYPE_DOUBLE_VECTOR) {
        size_t src_len = (h_src >= 0 && static_cast<size_t>(h_src) < VM_MAX_VECTOR_HANDLES)
            ? vm_state->query_context->double_vector_lengths[h_src] : 0;
        if (src_len > 0) N = src_len;
        if (vm_state->register_types[dst] != TYPE_DOUBLE_VECTOR) {
            int h = acquire_double_vector(vm_state->query_context);
            if (h < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
            vm_state->registers[dst] = h;
            vm_state->register_types[dst] = TYPE_DOUBLE_VECTOR;
        }
        int h_dst = static_cast<int>(vm_state->registers[dst]);
        vm_state->query_context->double_vector_lengths[h_dst] = src_len;
        double* d = vm_state->query_context->double_vectors[h_dst].data();
        const double* s = vm_state->query_context->double_vectors[h_src].data();
        impulse_vector_math_unary_f64(func_id, d, s, N);
    } else {
        size_t src_len = (h_src >= 0 && static_cast<size_t>(h_src) < VM_MAX_VECTOR_HANDLES)
            ? vm_state->query_context->float_vector_lengths[h_src] : 0;
        if (src_len > 0) N = src_len;
        if (vm_state->register_types[dst] != TYPE_FLOAT_VECTOR) {
            int h = acquire_float_vector(vm_state->query_context);
            if (h < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
            vm_state->registers[dst] = h;
            vm_state->register_types[dst] = TYPE_FLOAT_VECTOR;
        }
        int h_dst = static_cast<int>(vm_state->registers[dst]);
        vm_state->query_context->float_vector_lengths[h_dst] = src_len;
        float* d = vm_state->query_context->float_vectors[h_dst].data();
        const float* s = vm_state->query_context->float_vectors[h_src].data();
        impulse_vector_math_unary_f32(func_id, d, s, N);
    }
    vm_state->pc++;
    DISPATCH();
}

