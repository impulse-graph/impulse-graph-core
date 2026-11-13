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
#define OP_2D_TYPE_TAG_F32     0u
#define OP_2D_TYPE_TAG_F64     1u
#define OP_2D_ALLOWED_SRC_TYPES (IMP_TYPES_FLOAT_OR_DBL | IMP_TYPE_BIT(TYPE_FLOAT))
#endif

// Opcode 0x2D: OP_VEC_MATH_UNARY
op_VEC_MATH_UNARY: {
    const auto& inst = bytecode[vm_state->pc];
    const uint16_t dst = IMP_INST_DST(inst);
    VALIDATE_REG(dst);
    IMPULSE_ASSERT(dst < IMP_VM_GPR_COUNT);

    IMP_REQUIRE_CONTEXT(vm_state);
    IMPULSE_ASSERT(vm_state->query_context != nullptr);

    const uint16_t p0 = IMP_INST_U8_0(inst);
    const uint16_t p1 = IMP_INST_U8_1(inst);
    const uint16_t p2 = IMP_INST_U8_2(inst);

    bool p0_is_vec = (p0 < IMP_VM_GPR_COUNT && (vm_state->register_types[p0] == TYPE_DOUBLE_VECTOR || vm_state->register_types[p0] == TYPE_FLOAT_VECTOR || vm_state->register_types[p0] == TYPE_FLOAT));
    bool p1_is_vec = (p1 < IMP_VM_GPR_COUNT && (vm_state->register_types[p1] == TYPE_DOUBLE_VECTOR || vm_state->register_types[p1] == TYPE_FLOAT_VECTOR || vm_state->register_types[p1] == TYPE_FLOAT));

    uint16_t src;
    uint8_t func_id;
    IMPULSE_TESTCASE(!p0_is_vec && p1_is_vec);
    if (!p0_is_vec && p1_is_vec) {
        src = p1;
        func_id = static_cast<uint8_t>(p0);
    } else {
        src = p0;
        func_id = static_cast<uint8_t>(p1 ? p1 : p2);
    }
    VALIDATE_REG(src);
    IMPULSE_ASSERT(src < IMP_VM_GPR_COUNT);
    IMPULSE_CHECK_REG_TYPE(vm_state, src, OP_2D_ALLOWED_SRC_TYPES);

    const uint8_t type_tag = IMP_INST_FLAGS(inst);
    IMPULSE_TESTCASE(vm_state->register_types[src] == TYPE_FLOAT);
    IMPULSE_TESTCASE(dst == src);

    if (vm_state->register_types[src] == TYPE_FLOAT) {
        uint32_t bits_in = static_cast<uint32_t>(vm_state->registers[src] & IMP_SENTINEL_U32);
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
    const bool use_f64 = (type_tag == OP_2D_TYPE_TAG_F64 || vm_state->register_types[src] == TYPE_DOUBLE_VECTOR);
    IMPULSE_TESTCASE(use_f64);
    if (use_f64) {
        size_t src_len = (h_src >= 0 && static_cast<size_t>(h_src) < VM_MAX_VECTOR_HANDLES)
            ? vm_state->query_context->double_vector_lengths[h_src] : 0;
        N = std::min(src_len, vm_state->query_context->max_nodes);
        if (vm_state->register_types[dst] != TYPE_DOUBLE_VECTOR) {
            int h = acquire_double_vector(vm_state->query_context);
            if (h < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
            IMPULSE_ASSERT(h >= 0 && static_cast<size_t>(h) < vm_state->query_context->double_vectors.size());
            vm_state->registers[dst] = static_cast<uint64_t>(h);
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
        N = std::min(src_len, vm_state->query_context->max_nodes);
        if (vm_state->register_types[dst] != TYPE_FLOAT_VECTOR) {
            int h = acquire_float_vector(vm_state->query_context);
            if (h < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
            IMPULSE_ASSERT(h >= 0 && static_cast<size_t>(h) < vm_state->query_context->float_vectors.size());
            vm_state->registers[dst] = static_cast<uint64_t>(h);
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

