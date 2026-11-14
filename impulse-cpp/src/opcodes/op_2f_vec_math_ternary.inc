/**
 * @file op_2f_vec_math_ternary.inc
 * @brief Formal Contract for OP_VEC_MATH_TERNARY (0x2F).
 *
 * Mnemonic: OP_VEC_MATH_TERNARY
 * Opcode: 0x2F
 * Description: Vectorized ternary fused mathematical evaluation (e.g. LERP, FMA).
 *
 * Operands:
 *  - dst = inst.dst_reg, src1 = payload & 0xFF, src2 = (payload >> 8) & 0xFF, src3 = (payload >> 16) & 0xFF, func_id = payload >> 24
 *
 * Inputs:
 *  - src1, src2, src3: FLOAT_OR_DBL
 *
 * Outputs:
 *  - dst: FLOAT_OR_DBL
 *
 * Flags:
 *  - Unmodified
 *
 * Context Requirement:
 *  - Requires context (query_context vector pool)
 */
#ifndef OP_2F_CONTRACT_DEFINED
#define OP_2F_CONTRACT_DEFINED 1
#define OP_2F_TYPE_TAG_F32 0u
#define OP_2F_TYPE_TAG_F64 1u
#endif

// Opcode 0x2F: OP_VEC_MATH_TERNARY
op_VEC_MATH_TERNARY: {
    const auto& inst = bytecode[vm_state->pc];
    const uint16_t dst = IMP_INST_DST(inst);
    VALIDATE_REG(dst);
    IMPULSE_ASSERT(dst < IMP_VM_GPR_COUNT);

    IMP_REQUIRE_CONTEXT(vm_state);
    IMPULSE_ASSERT(vm_state->query_context != nullptr);

    const uint16_t p0 = IMP_INST_U8_0(inst);
    const uint16_t p1 = IMP_INST_U8_1(inst);
    const uint16_t p2 = IMP_INST_U8_2(inst);
    const uint16_t p3 = IMP_INST_U8_3(inst);

    bool p0_is_vec_t = (p0 < IMP_VM_GPR_COUNT && (vm_state->register_types[p0] == TYPE_DOUBLE_VECTOR || vm_state->register_types[p0] == TYPE_FLOAT_VECTOR));
    bool p3_is_vec_t = (p3 < IMP_VM_GPR_COUNT && (vm_state->register_types[p3] == TYPE_DOUBLE_VECTOR || vm_state->register_types[p3] == TYPE_FLOAT_VECTOR));

    uint16_t src1, src2, src3;
    uint8_t func_id;
    IMPULSE_TESTCASE(!p0_is_vec_t && p3_is_vec_t);
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
    IMPULSE_ASSERT(src1 < IMP_VM_GPR_COUNT && src2 < IMP_VM_GPR_COUNT && src3 < IMP_VM_GPR_COUNT);
    constexpr uint64_t allowed_types = IMP_TYPES_FLOAT_OR_DBL | IMP_TYPE_BIT(TYPE_FLOAT) | IMP_TYPE_BIT(TYPE_DOUBLE);
    IMPULSE_CHECK_REG_TYPE(vm_state, src1, allowed_types);
    IMPULSE_CHECK_REG_TYPE(vm_state, src2, allowed_types);
    IMPULSE_CHECK_REG_TYPE(vm_state, src3, allowed_types);
    const uint8_t type_tag = IMP_INST_FLAGS(inst);
    const uint8_t t1 = vm_state->register_types[src1];
    const uint8_t t2 = vm_state->register_types[src2];
    const uint8_t t3 = vm_state->register_types[src3];
    const bool is_vec1 = (t1 == TYPE_FLOAT_VECTOR || t1 == TYPE_DOUBLE_VECTOR);
    const bool is_vec2 = (t2 == TYPE_FLOAT_VECTOR || t2 == TYPE_DOUBLE_VECTOR);
    const bool is_vec3 = (t3 == TYPE_FLOAT_VECTOR || t3 == TYPE_DOUBLE_VECTOR);
    const bool use_f64 = (type_tag == OP_2F_TYPE_TAG_F64 ||
                          t1 == TYPE_DOUBLE_VECTOR || t2 == TYPE_DOUBLE_VECTOR || t3 == TYPE_DOUBLE_VECTOR ||
                          t1 == TYPE_DOUBLE || t2 == TYPE_DOUBLE || t3 == TYPE_DOUBLE);
    IMPULSE_TESTCASE(use_f64);
    IMPULSE_TESTCASE(dst == src1 || dst == src2 || dst == src3);

    auto read_scalar_f64 = [&](uint16_t r) -> double {
        if (vm_state->register_types[r] == TYPE_DOUBLE) {
            double d = 0.0;
            uint64_t b = vm_state->registers[r];
            std::memcpy(&d, &b, sizeof(double));
            return d;
        }
        uint32_t b = static_cast<uint32_t>(vm_state->registers[r] & IMP_SENTINEL_U32);
        float f = 0.0f;
        std::memcpy(&f, &b, sizeof(float));
        return static_cast<double>(f);
    };

    if (!is_vec1 && !is_vec2 && !is_vec3) {
        if (use_f64) {
            double a = read_scalar_f64(src1), b = read_scalar_f64(src2), c = read_scalar_f64(src3), out = 0.0;
            impulse_vector_math_ternary_f64(func_id, &out, &a, &b, &c, 1);
            uint64_t bits = 0;
            std::memcpy(&bits, &out, sizeof(double));
            vm_state->registers[dst] = bits;
            vm_state->register_types[dst] = TYPE_DOUBLE;
        } else {
            float a = static_cast<float>(read_scalar_f64(src1));
            float b = static_cast<float>(read_scalar_f64(src2));
            float c = static_cast<float>(read_scalar_f64(src3));
            float out = 0.0f;
            impulse_vector_math_ternary_f32(func_id, &out, &a, &b, &c, 1);
            uint32_t bits = 0;
            std::memcpy(&bits, &out, sizeof(float));
            vm_state->registers[dst] = bits;
            vm_state->register_types[dst] = TYPE_FLOAT;
        }
        vm_state->pc++;
        DISPATCH();
    }

    size_t N = vm_state->query_context->max_nodes;
    auto clamp_vec_len = [&](uint16_t r) {
        uint8_t t = vm_state->register_types[r];
        if (t == TYPE_FLOAT_VECTOR) {
            int h = static_cast<int>(vm_state->registers[r]);
            if (h >= 0 && static_cast<size_t>(h) < VM_MAX_VECTOR_HANDLES) {
                N = std::min({N, vm_state->query_context->float_vector_lengths[h],
                              vm_state->query_context->float_vectors[h].size()});
            }
        } else if (t == TYPE_DOUBLE_VECTOR) {
            int h = static_cast<int>(vm_state->registers[r]);
            if (h >= 0 && static_cast<size_t>(h) < VM_MAX_VECTOR_HANDLES) {
                N = std::min({N, vm_state->query_context->double_vector_lengths[h],
                              vm_state->query_context->double_vectors[h].size()});
            }
        }
    };
    clamp_vec_len(src1);
    clamp_vec_len(src2);
    clamp_vec_len(src3);

    int h_s1 = static_cast<int>(vm_state->registers[src1]);
    int h_s2 = static_cast<int>(vm_state->registers[src2]);
    int h_s3 = static_cast<int>(vm_state->registers[src3]);

    if (use_f64) {
        if (vm_state->register_types[dst] != TYPE_DOUBLE_VECTOR) {
            int h = acquire_double_vector(vm_state->query_context);
            if (h < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
            IMPULSE_ASSERT(h >= 0 && static_cast<size_t>(h) < vm_state->query_context->double_vectors.size());
            vm_state->registers[dst] = static_cast<uint64_t>(h);
            vm_state->register_types[dst] = TYPE_DOUBLE_VECTOR;
        }
        int h_dst = static_cast<int>(vm_state->registers[dst]);
        N = std::min(N, vm_state->query_context->double_vectors[h_dst].size());
        vm_state->query_context->double_vector_lengths[h_dst] = N;
        double* d = vm_state->query_context->double_vectors[h_dst].data();
        if (t1 == TYPE_DOUBLE_VECTOR && t2 == TYPE_DOUBLE_VECTOR && t3 == TYPE_DOUBLE_VECTOR) {
            const double* s1 = vm_state->query_context->double_vectors[h_s1].data();
            const double* s2 = vm_state->query_context->double_vectors[h_s2].data();
            const double* s3 = vm_state->query_context->double_vectors[h_s3].data();
            impulse_vector_math_ternary_f64(func_id, d, s1, s2, s3, N);
        } else {
            double c1 = is_vec1 ? 0.0 : read_scalar_f64(src1);
            double c2 = is_vec2 ? 0.0 : read_scalar_f64(src2);
            double c3 = is_vec3 ? 0.0 : read_scalar_f64(src3);
            for (size_t i = 0; i < N; ++i) {
                double a = (t1 == TYPE_DOUBLE_VECTOR) ? vm_state->query_context->double_vectors[h_s1][i]
                         : (t1 == TYPE_FLOAT_VECTOR ? static_cast<double>(vm_state->query_context->float_vectors[h_s1][i]) : c1);
                double b = (t2 == TYPE_DOUBLE_VECTOR) ? vm_state->query_context->double_vectors[h_s2][i]
                         : (t2 == TYPE_FLOAT_VECTOR ? static_cast<double>(vm_state->query_context->float_vectors[h_s2][i]) : c2);
                double c = (t3 == TYPE_DOUBLE_VECTOR) ? vm_state->query_context->double_vectors[h_s3][i]
                         : (t3 == TYPE_FLOAT_VECTOR ? static_cast<double>(vm_state->query_context->float_vectors[h_s3][i]) : c3);
                impulse_vector_math_ternary_f64(func_id, &d[i], &a, &b, &c, 1);
            }
        }
    } else {
        if (vm_state->register_types[dst] != TYPE_FLOAT_VECTOR) {
            int h = acquire_float_vector(vm_state->query_context);
            if (h < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
            IMPULSE_ASSERT(h >= 0 && static_cast<size_t>(h) < vm_state->query_context->float_vectors.size());
            vm_state->registers[dst] = static_cast<uint64_t>(h);
            vm_state->register_types[dst] = TYPE_FLOAT_VECTOR;
        }
        int h_dst = static_cast<int>(vm_state->registers[dst]);
        N = std::min(N, vm_state->query_context->float_vectors[h_dst].size());
        vm_state->query_context->float_vector_lengths[h_dst] = N;
        float* d = vm_state->query_context->float_vectors[h_dst].data();
        if (t1 == TYPE_FLOAT_VECTOR && t2 == TYPE_FLOAT_VECTOR && t3 == TYPE_FLOAT_VECTOR) {
            const float* s1 = vm_state->query_context->float_vectors[h_s1].data();
            const float* s2 = vm_state->query_context->float_vectors[h_s2].data();
            const float* s3 = vm_state->query_context->float_vectors[h_s3].data();
            impulse_vector_math_ternary_f32(func_id, d, s1, s2, s3, N);
        } else {
            float c1 = is_vec1 ? 0.0f : static_cast<float>(read_scalar_f64(src1));
            float c2 = is_vec2 ? 0.0f : static_cast<float>(read_scalar_f64(src2));
            float c3 = is_vec3 ? 0.0f : static_cast<float>(read_scalar_f64(src3));
            for (size_t i = 0; i < N; ++i) {
                float a = is_vec1 ? vm_state->query_context->float_vectors[h_s1][i] : c1;
                float b = is_vec2 ? vm_state->query_context->float_vectors[h_s2][i] : c2;
                float c = is_vec3 ? vm_state->query_context->float_vectors[h_s3][i] : c3;
                impulse_vector_math_ternary_f32(func_id, &d[i], &a, &b, &c, 1);
            }
        }
    }
    vm_state->pc++;
    DISPATCH();
}

