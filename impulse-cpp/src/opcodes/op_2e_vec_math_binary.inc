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
#define OP_2E_TYPE_TAG_F32     0u
#define OP_2E_TYPE_TAG_F64     1u
#define OP_2E_ALLOWED_SRC_TYPES (IMP_TYPES_FLOAT_OR_DBL | IMP_TYPE_BIT(TYPE_FLOAT))
#endif

// Opcode 0x2E: OP_VEC_MATH_BINARY
op_VEC_MATH_BINARY: {
    const auto& inst = bytecode[vm_state->pc];
    const uint16_t dst = IMP_INST_DST(inst);
    VALIDATE_REG(dst);
    IMPULSE_ASSERT(dst < IMP_VM_GPR_COUNT);

    IMP_REQUIRE_CONTEXT(vm_state);
    IMPULSE_ASSERT(vm_state->query_context != nullptr);

    const uint16_t p0 = IMP_INST_U8_0(inst);
    const uint16_t p1 = IMP_INST_U8_1(inst);
    const uint16_t p2 = IMP_INST_U8_2(inst);

    bool p0_is_vec_b = (p0 < IMP_VM_GPR_COUNT && (vm_state->register_types[p0] == TYPE_DOUBLE_VECTOR || vm_state->register_types[p0] == TYPE_FLOAT_VECTOR || vm_state->register_types[p0] == TYPE_FLOAT));
    bool p2_is_vec_b = (p2 < IMP_VM_GPR_COUNT && (vm_state->register_types[p2] == TYPE_DOUBLE_VECTOR || vm_state->register_types[p2] == TYPE_FLOAT_VECTOR || vm_state->register_types[p2] == TYPE_FLOAT));

    uint16_t src1, src2;
    uint8_t func_id;
    IMPULSE_TESTCASE(!p0_is_vec_b && p2_is_vec_b);
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
    IMPULSE_ASSERT(src1 < IMP_VM_GPR_COUNT && src2 < IMP_VM_GPR_COUNT);
    IMPULSE_CHECK_REG_TYPE(vm_state, src1, OP_2E_ALLOWED_SRC_TYPES);
    IMPULSE_CHECK_REG_TYPE(vm_state, src2, OP_2E_ALLOWED_SRC_TYPES);
    const uint8_t type_tag = IMP_INST_FLAGS(inst);

    uint8_t t1 = vm_state->register_types[src1];
    uint8_t t2 = vm_state->register_types[src2];
    IMPULSE_TESTCASE(t1 == TYPE_FLOAT && t2 == TYPE_FLOAT);
    IMPULSE_TESTCASE((t1 == TYPE_FLOAT) != (t2 == TYPE_FLOAT));
    IMPULSE_TESTCASE(src1 == src2);

    if (t1 == TYPE_FLOAT && t2 == TYPE_FLOAT) {
        uint32_t b1 = static_cast<uint32_t>(vm_state->registers[src1] & IMP_SENTINEL_U32);
        uint32_t b2 = static_cast<uint32_t>(vm_state->registers[src2] & IMP_SENTINEL_U32);
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
        size_t N = std::min(v_len, vm_state->query_context->max_nodes);
        uint32_t b_flt = static_cast<uint32_t>(vm_state->registers[flt_reg] & IMP_SENTINEL_U32);
        float f_val = 0.0f;
        std::memcpy(&f_val, &b_flt, sizeof(float));
        if (vm_state->register_types[dst] != TYPE_FLOAT_VECTOR) {
            int h = acquire_float_vector(vm_state->query_context);
            if (h < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
            IMPULSE_ASSERT(h >= 0 && static_cast<size_t>(h) < vm_state->query_context->float_vectors.size());
            vm_state->registers[dst] = static_cast<uint64_t>(h);
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
    const bool use_f64 = (type_tag == OP_2E_TYPE_TAG_F64 || vm_state->register_types[src1] == TYPE_DOUBLE_VECTOR);
    IMPULSE_TESTCASE(use_f64);
    if (use_f64) {
        size_t len1 = (h_s1 >= 0 && static_cast<size_t>(h_s1) < VM_MAX_VECTOR_HANDLES) ? vm_state->query_context->double_vector_lengths[h_s1] : 0;
        size_t len2 = (h_s2 >= 0 && static_cast<size_t>(h_s2) < VM_MAX_VECTOR_HANDLES) ? vm_state->query_context->double_vector_lengths[h_s2] : 0;
        size_t out_len = std::min(len1, len2);
        N = std::min(out_len, vm_state->query_context->max_nodes);
        if (vm_state->register_types[dst] != TYPE_DOUBLE_VECTOR) {
            int h = acquire_double_vector(vm_state->query_context);
            if (h < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
            IMPULSE_ASSERT(h >= 0 && static_cast<size_t>(h) < vm_state->query_context->double_vectors.size());
            vm_state->registers[dst] = static_cast<uint64_t>(h);
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
        size_t out_len = std::min(len1, len2);
        N = std::min(out_len, vm_state->query_context->max_nodes);
        if (vm_state->register_types[dst] != TYPE_FLOAT_VECTOR) {
            int h = acquire_float_vector(vm_state->query_context);
            if (h < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
            IMPULSE_ASSERT(h >= 0 && static_cast<size_t>(h) < vm_state->query_context->float_vectors.size());
            vm_state->registers[dst] = static_cast<uint64_t>(h);
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

