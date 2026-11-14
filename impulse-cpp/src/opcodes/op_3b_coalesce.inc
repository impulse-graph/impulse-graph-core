/**
 * Formal Opcode Contract: OP_COALESCE (0x3B)
 *
 * Layout:
 *   dst_reg:          inst.dst_reg (DST_REG)
 *   src_reg:          inst.payload & 0xFFFF (PAYLOAD_LOW16)
 *   aux1_reg:         (inst.payload >> 16) & 0xFFFF (PAYLOAD_HIGH16)
 *   allowed_src:      IMP_TYPES_ANY
 *   allowed_aux1:     IMP_TYPES_ANY
 *   output_dst:       IMP_TYPES_ANY
 *   requires_ctx:     true
 */
#ifndef OP_3B_CONTRACT_DEFINED
#define OP_3B_CONTRACT_DEFINED
#define OP_3B_DEFINED_FLAGS        0x00u
#define OP_3B_ALLOWED_SRC_TYPES    (IMP_TYPES_ANY)
#define OP_3B_ALLOWED_AUX1_TYPES   (IMP_TYPES_ANY)
#define OP_3B_OUTPUT_DST_TYPES     (IMP_TYPES_ANY)
#endif

// Opcode 0x3B: OP_COALESCE
op_COALESCE: {
    const auto& inst = bytecode[vm_state->pc];
    const uint16_t dst = IMP_INST_DST(inst);
    const uint16_t src1 = IMP_INST_LOW16(inst);
    const uint16_t src2 = IMP_INST_HIGH16(inst);
    VALIDATE_REG(dst);
    VALIDATE_REG(src1);
    VALIDATE_REG(src2);
    IMPULSE_ASSERT(dst < IMP_VM_GPR_COUNT && src1 < IMP_VM_GPR_COUNT && src2 < IMP_VM_GPR_COUNT);
    IMP_REQUIRE_CONTEXT(vm_state);
    IMPULSE_ASSERT(vm_state->query_context != nullptr);

    IMPULSE_TESTCASE(vm_state->register_types[src1] == TYPE_NULL);
    if (vm_state->register_types[src1] == TYPE_NULL) {
        return IMPULSE_VM_ERR_INVALID_REGISTER;
    }
    IMPULSE_TESTCASE(dst == src1 || dst == src2);

    if (vm_state->register_types[src1] == TYPE_FLOAT_VECTOR && vm_state->register_types[src2] == TYPE_FLOAT_VECTOR) {
        int h1 = static_cast<int>(vm_state->registers[src1]);
        int h2 = static_cast<int>(vm_state->registers[src2]);
        if (h1 < 0 || static_cast<size_t>(h1) >= vm_state->query_context->float_vectors.size() ||
            h2 < 0 || static_cast<size_t>(h2) >= vm_state->query_context->float_vectors.size()) {
            return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        }
        int hDst = acquire_float_vector(vm_state->query_context);
        if (hDst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        IMPULSE_ASSERT(static_cast<size_t>(hDst) < vm_state->query_context->float_vectors.size());
        auto& vDst = vm_state->query_context->float_vectors[hDst];
        auto& v1 = vm_state->query_context->float_vectors[h1];
        auto& v2 = vm_state->query_context->float_vectors[h2];
        size_t len1 = vm_state->query_context->float_vector_lengths[h1];
        size_t len2 = vm_state->query_context->float_vector_lengths[h2];
        size_t active_len = (len1 > 0 && len2 > 0) ? std::max(len1, len2) : std::max(v1.size(), v2.size());
        active_len = std::min(active_len, vDst.size());
        vm_state->query_context->float_vector_lengths[hDst] = active_len;
        for (size_t i = 0; i < active_len; ++i) {
            float a = (i < v1.size()) ? v1[i] : std::numeric_limits<float>::quiet_NaN();
            float b = (i < v2.size()) ? v2[i] : 0.0f;
            vDst[i] = std::isnan(a) ? b : a;
        }
        vm_state->registers[dst] = hDst;
        vm_state->register_types[dst] = TYPE_FLOAT_VECTOR;
    } else if (vm_state->register_types[src1] == TYPE_FLOAT_VECTOR &&
               vm_state->register_types[src2] == TYPE_FLOAT) {
        int hDst = acquire_float_vector(vm_state->query_context);
        if (hDst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        int h1 = static_cast<int>(vm_state->registers[src1]);
        IMPULSE_ASSERT(static_cast<size_t>(hDst) < vm_state->query_context->float_vectors.size());
        IMPULSE_ASSERT(h1 >= 0 && static_cast<size_t>(h1) < vm_state->query_context->float_vectors.size());
        auto& vDst = vm_state->query_context->float_vectors[hDst];
        auto& v1 = vm_state->query_context->float_vectors[h1];
        size_t len1 = vm_state->query_context->float_vector_lengths[h1];
        size_t active_len = (len1 > 0) ? len1 : v1.size();
        active_len = std::min(active_len, vDst.size());
        vm_state->query_context->float_vector_lengths[hDst] = active_len;
        float fallback = 0.0f;
        if (vm_state->register_types[src2] == TYPE_FLOAT) {
            uint32_t bits = static_cast<uint32_t>(vm_state->registers[src2] & IMP_SENTINEL_U32);
            std::memcpy(&fallback, &bits, sizeof(float));
        } else {
            fallback = static_cast<float>(static_cast<int64_t>(vm_state->registers[src2]));
        }
        for (size_t i = 0; i < active_len; ++i) {
            float a = (i < v1.size()) ? v1[i] : std::numeric_limits<float>::quiet_NaN();
            vDst[i] = std::isnan(a) ? fallback : a;
        }
        vm_state->registers[dst] = hDst;
        vm_state->register_types[dst] = TYPE_FLOAT_VECTOR;
    } else if (vm_state->register_types[src1] == TYPE_DOUBLE_VECTOR && vm_state->register_types[src2] == TYPE_DOUBLE_VECTOR) {
        int hDst = acquire_double_vector(vm_state->query_context);
        if (hDst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        int h1 = static_cast<int>(vm_state->registers[src1]);
        int h2 = static_cast<int>(vm_state->registers[src2]);
        IMPULSE_ASSERT(static_cast<size_t>(hDst) < vm_state->query_context->double_vectors.size());
        IMPULSE_ASSERT(h1 >= 0 && static_cast<size_t>(h1) < vm_state->query_context->double_vectors.size());
        IMPULSE_ASSERT(h2 >= 0 && static_cast<size_t>(h2) < vm_state->query_context->double_vectors.size());
        auto& vDst = vm_state->query_context->double_vectors[hDst];
        auto& v1 = vm_state->query_context->double_vectors[h1];
        auto& v2 = vm_state->query_context->double_vectors[h2];
        size_t len1 = vm_state->query_context->double_vector_lengths[h1];
        size_t len2 = vm_state->query_context->double_vector_lengths[h2];
        size_t active_len = (len1 > 0 && len2 > 0) ? std::max(len1, len2) : std::max(v1.size(), v2.size());
        active_len = std::min(active_len, vDst.size());
        vm_state->query_context->double_vector_lengths[hDst] = active_len;
        for (size_t i = 0; i < active_len; ++i) {
            double a = (i < v1.size()) ? v1[i] : std::numeric_limits<double>::quiet_NaN();
            double b = (i < v2.size()) ? v2[i] : 0.0;
            vDst[i] = std::isnan(a) ? b : a;
        }
        vm_state->registers[dst] = hDst;
        vm_state->register_types[dst] = TYPE_DOUBLE_VECTOR;
    } else if (vm_state->register_types[src1] == TYPE_DOUBLE_VECTOR &&
               (vm_state->register_types[src2] == TYPE_DOUBLE || vm_state->register_types[src2] == TYPE_FLOAT)) {
        int hDst = acquire_double_vector(vm_state->query_context);
        if (hDst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        int h1 = static_cast<int>(vm_state->registers[src1]);
        IMPULSE_ASSERT(static_cast<size_t>(hDst) < vm_state->query_context->double_vectors.size());
        IMPULSE_ASSERT(h1 >= 0 && static_cast<size_t>(h1) < vm_state->query_context->double_vectors.size());
        auto& vDst = vm_state->query_context->double_vectors[hDst];
        auto& v1 = vm_state->query_context->double_vectors[h1];
        size_t len1 = vm_state->query_context->double_vector_lengths[h1];
        size_t active_len = (len1 > 0) ? len1 : v1.size();
        active_len = std::min(active_len, vDst.size());
        vm_state->query_context->double_vector_lengths[hDst] = active_len;
        double fallback = 0.0;
        if (vm_state->register_types[src2] == TYPE_DOUBLE) {
            std::memcpy(&fallback, &vm_state->registers[src2], sizeof(double));
        } else if (vm_state->register_types[src2] == TYPE_FLOAT) {
            float f32 = 0.0f;
            uint32_t bits = static_cast<uint32_t>(vm_state->registers[src2] & IMP_SENTINEL_U32);
            std::memcpy(&f32, &bits, sizeof(float));
            fallback = static_cast<double>(f32);
        } else {
            fallback = static_cast<double>(static_cast<int64_t>(vm_state->registers[src2]));
        }
        for (size_t i = 0; i < active_len; ++i) {
            double a = (i < v1.size()) ? v1[i] : std::numeric_limits<double>::quiet_NaN();
            vDst[i] = std::isnan(a) ? fallback : a;
        }
        vm_state->registers[dst] = hDst;
        vm_state->register_types[dst] = TYPE_DOUBLE_VECTOR;
    } else {
        return IMPULSE_VM_ERR_INVALID_REGISTER;
    }
    IMP_VM_SET_ZF(vm_state, false);
    vm_state->pc++;
    DISPATCH();
}

