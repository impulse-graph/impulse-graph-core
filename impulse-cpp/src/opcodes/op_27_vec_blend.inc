/**
 * @file op_27_vec_blend.inc
 * @brief Formal Contract for OP_VEC_BLEND (0x27).
 *
 * Mnemonic: OP_VEC_BLEND
 * Opcode: 0x27
 * Description: Vector SIMD elementwise conditional multiplexer based on BitSet selection mask.
 *
 * Operands:
 *  - dst = inst.dst_reg, mask = payload & 0xFF, src_true = (payload >> 8) & 0xFF, src_false = (payload >> 16) & 0xFF
 *
 * Inputs:
 *  - mask: TYPE_BITSET_HANDLE, src_true, src_false: NUMERIC_VEC
 *
 * Outputs:
 *  - dst: NUMERIC_VEC
 *
 * Flags:
 *  - Unmodified
 *
 * Context Requirement:
 *  - Requires context (query_context bitset and vector pool)
 */
#ifndef OP_27_CONTRACT_DEFINED
#define OP_27_CONTRACT_DEFINED 1
#define OP_27_ALLOWED_SRC_TYPES  (IMP_TYPE_BIT(TYPE_BITSET_HANDLE) | IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_NULL))
#define OP_27_ALLOWED_AUX1_TYPES (IMP_TYPES_NUMERIC_VEC | IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_NULL))
#define OP_27_ALLOWED_AUX2_TYPES (IMP_TYPES_NUMERIC_VEC | IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_NULL))
#endif

// Opcode 0x27: OP_VEC_BLEND
op_VEC_BLEND: {
    const auto& inst = bytecode[vm_state->pc];
    const uint16_t dst = IMP_INST_DST(inst);
    const uint16_t mask_reg = IMP_INST_U8_0(inst);
    const uint16_t src_true = IMP_INST_U8_1(inst);
    const uint16_t src_false = IMP_INST_U8_2(inst);
    VALIDATE_REG(dst);
    VALIDATE_REG(mask_reg);
    VALIDATE_REG(src_true);
    VALIDATE_REG(src_false);
    IMPULSE_ASSERT(dst < IMP_VM_GPR_COUNT && mask_reg < IMP_VM_GPR_COUNT && src_true < IMP_VM_GPR_COUNT && src_false < IMP_VM_GPR_COUNT);
    IMP_REQUIRE_CONTEXT(vm_state);
    IMPULSE_ASSERT(vm_state->query_context != nullptr);
    IMPULSE_CHECK_REG_TYPE(vm_state, mask_reg, OP_27_ALLOWED_SRC_TYPES);
    IMPULSE_CHECK_REG_TYPE(vm_state, src_true, OP_27_ALLOWED_AUX1_TYPES);
    IMPULSE_CHECK_REG_TYPE(vm_state, src_false, OP_27_ALLOWED_AUX2_TYPES);

    uint8_t type_true = vm_state->register_types[src_true];
    IMPULSE_TESTCASE(type_true != TYPE_DOUBLE_VECTOR && type_true != TYPE_FLOAT_VECTOR);
    if (type_true != TYPE_DOUBLE_VECTOR && type_true != TYPE_FLOAT_VECTOR) {
        return IMPULSE_VM_ERR_INVALID_REGISTER;
    }

    int h_mask = static_cast<int>(vm_state->registers[mask_reg]);
    int h_true = static_cast<int>(vm_state->registers[src_true]);
    int h_false = static_cast<int>(vm_state->registers[src_false]);
    bool has_mask = (vm_state->register_types[mask_reg] == TYPE_BITSET_HANDLE &&
                     h_mask >= 0 && h_mask < static_cast<int>(vm_state->query_context->bitsets.size()));
    IMPULSE_TESTCASE(!has_mask);
    IMPULSE_TESTCASE(type_true == TYPE_DOUBLE_VECTOR);
    IMPULSE_TESTCASE(dst == src_true || dst == src_false);
    size_t N = vm_state->query_context->max_nodes;

    if (type_true == TYPE_DOUBLE_VECTOR) {
        size_t len1 = (h_true >= 0 && static_cast<size_t>(h_true) < VM_MAX_VECTOR_HANDLES) ? vm_state->query_context->double_vector_lengths[h_true] : N;
        size_t len2 = (h_false >= 0 && static_cast<size_t>(h_false) < VM_MAX_VECTOR_HANDLES) ? vm_state->query_context->double_vector_lengths[h_false] : N;
        size_t active_len = (len1 > 0 && len2 > 0) ? std::max(len1, len2) : (len1 > 0 ? len1 : (len2 > 0 ? len2 : N));

        if (vm_state->register_types[dst] != TYPE_DOUBLE_VECTOR) {
            int h = acquire_double_vector(vm_state->query_context);
            if (h < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
            IMPULSE_ASSERT(h >= 0 && static_cast<size_t>(h) < vm_state->query_context->double_vectors.size());
            vm_state->registers[dst] = static_cast<uint64_t>(h);
            vm_state->register_types[dst] = TYPE_DOUBLE_VECTOR;
        }
        int h_dst = static_cast<int>(vm_state->registers[dst]);
        vm_state->query_context->double_vector_lengths[h_dst] = active_len;
        double* d = vm_state->query_context->double_vectors[h_dst].data();
        const double* vt = (h_true >= 0 && static_cast<size_t>(h_true) < VM_MAX_VECTOR_HANDLES) ? vm_state->query_context->double_vectors[h_true].data() : nullptr;
        const double* vf = (h_false >= 0 && static_cast<size_t>(h_false) < VM_MAX_VECTOR_HANDLES) ? vm_state->query_context->double_vectors[h_false].data() : nullptr;
        for (size_t i = 0; i < active_len; ++i) {
            bool bit = has_mask && vm_state->query_context->bitsets[h_mask].test(i);
            double val1 = (vt && i < len1) ? vt[i] : 0.0;
            double val2 = (vf && i < len2) ? vf[i] : 0.0;
            d[i] = bit ? val1 : val2;
        }
    } else {
        size_t len1 = (h_true >= 0 && static_cast<size_t>(h_true) < VM_MAX_VECTOR_HANDLES) ? vm_state->query_context->float_vector_lengths[h_true] : N;
        size_t len2 = (h_false >= 0 && static_cast<size_t>(h_false) < VM_MAX_VECTOR_HANDLES) ? vm_state->query_context->float_vector_lengths[h_false] : N;
        size_t active_len = (len1 > 0 && len2 > 0) ? std::max(len1, len2) : (len1 > 0 ? len1 : (len2 > 0 ? len2 : N));

        if (vm_state->register_types[dst] != TYPE_FLOAT_VECTOR) {
            int h = acquire_float_vector(vm_state->query_context);
            if (h < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
            IMPULSE_ASSERT(h >= 0 && static_cast<size_t>(h) < vm_state->query_context->float_vectors.size());
            vm_state->registers[dst] = static_cast<uint64_t>(h);
            vm_state->register_types[dst] = TYPE_FLOAT_VECTOR;
        }
        int h_dst = static_cast<int>(vm_state->registers[dst]);
        vm_state->query_context->float_vector_lengths[h_dst] = active_len;
        float* d = vm_state->query_context->float_vectors[h_dst].data();
        const float* vt = (h_true >= 0 && static_cast<size_t>(h_true) < VM_MAX_VECTOR_HANDLES) ? vm_state->query_context->float_vectors[h_true].data() : nullptr;
        const float* vf = (h_false >= 0 && static_cast<size_t>(h_false) < VM_MAX_VECTOR_HANDLES) ? vm_state->query_context->float_vectors[h_false].data() : nullptr;
        for (size_t i = 0; i < active_len; ++i) {
            bool bit = has_mask && vm_state->query_context->bitsets[h_mask].test(i);
            float val1 = (vt && i < len1) ? vt[i] : 0.0f;
            float val2 = (vf && i < len2) ? vf[i] : 0.0f;
            d[i] = bit ? val1 : val2;
        }
    }
    vm_state->pc++;
    DISPATCH();
}

