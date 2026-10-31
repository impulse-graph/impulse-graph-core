/**
 * Formal Opcode Contract: OP_EWISE_MULT (0x44)
 *
 * Layout:
 *   dst_reg:          inst.dst_reg (DST_REG)
 *   src_reg:          inst.payload & 0xFF (PAYLOAD_LOW8)
 *   aux1_reg:         (inst.payload >> 8) & 0xFF (PAYLOAD_BYTE1)
 *   op_id:            (inst.payload >> 16) & 0xFFFF
 *   allowed_src:      IMP_TYPES_FLOAT_OR_DBL
 *   allowed_aux1:     IMP_TYPES_FLOAT_OR_DBL
 *   output_dst:       IMP_TYPES_FLOAT_OR_DBL
 *   requires_ctx:     true
 */
#ifndef OP_44_CONTRACT_DEFINED
#define OP_44_CONTRACT_DEFINED
#define OP_44_DEFINED_FLAGS        0x00u
#define OP_44_ALLOWED_SRC_TYPES    (IMP_TYPES_FLOAT_OR_DBL)
#define OP_44_ALLOWED_AUX1_TYPES   (IMP_TYPES_FLOAT_OR_DBL)
#define OP_44_OUTPUT_DST_TYPES     (IMP_TYPES_FLOAT_OR_DBL)
#endif

// Opcode 0x44: OP_EWISE_MULT
op_EWISE_MULT: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src1 = inst.payload & 0xFF;
    uint16_t src2 = (inst.payload >> 8) & 0xFF;
    uint16_t op_id = (inst.payload >> 16) & 0xFFFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src1);
    VALIDATE_REG(src2);
    if (!vm_state->query_context) return IMPULSE_VM_ERR_NULL_SNAPSHOT;
    IMPULSE_CHECK_REG_TYPE(vm_state, src1, OP_44_ALLOWED_SRC_TYPES);
    IMPULSE_CHECK_REG_TYPE(vm_state, src2, OP_44_ALLOWED_AUX1_TYPES);

    size_t N = vm_state->query_context->max_nodes;
    bool is_double = (vm_state->register_types[src1] == TYPE_DOUBLE_VECTOR);

    if (is_double) {
        size_t h_s1 = vm_state->registers[src1];
        size_t h_s2 = vm_state->registers[src2];
        size_t len1 = (h_s1 < VM_MAX_VECTOR_HANDLES) ? vm_state->query_context->double_vector_lengths[h_s1] : N;
        size_t len2 = (h_s2 < VM_MAX_VECTOR_HANDLES) ? vm_state->query_context->double_vector_lengths[h_s2] : N;
        size_t active_len = (len1 > 0 && len2 > 0) ? std::min(len1, len2) : N;
        if (vm_state->register_types[dst] != TYPE_DOUBLE_VECTOR) {
            int h_dst = acquire_double_vector(vm_state->query_context);
            if (h_dst < 0) {
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
            vm_state->registers[dst] = h_dst;
            vm_state->register_types[dst] = TYPE_DOUBLE_VECTOR;
        }
        vm_state->query_context->double_vector_lengths[vm_state->registers[dst]] = active_len;
        double* dst_data = vm_state->query_context->double_vectors[vm_state->registers[dst]].data();
        const double* s1_data = vm_state->query_context->double_vectors[h_s1].data();
        const double* s2_data = vm_state->query_context->double_vectors[h_s2].data();

        #pragma omp parallel for schedule(static)
        for (size_t i = 0; i < N; ++i) {
            if (op_id == BINARY_OP_MUL) dst_data[i] = s1_data[i] * s2_data[i];
            else if (op_id == BINARY_OP_MIN) dst_data[i] = std::min(s1_data[i], s2_data[i]);
            else if (op_id == BINARY_OP_MAX) dst_data[i] = std::max(s1_data[i], s2_data[i]);
            else dst_data[i] = s1_data[i] * s2_data[i];
        }
    } else {
        size_t h_s1 = vm_state->registers[src1];
        size_t h_s2 = vm_state->registers[src2];
        size_t len1 = (h_s1 < VM_MAX_VECTOR_HANDLES) ? vm_state->query_context->float_vector_lengths[h_s1] : N;
        size_t len2 = (h_s2 < VM_MAX_VECTOR_HANDLES) ? vm_state->query_context->float_vector_lengths[h_s2] : N;
        size_t active_len = (len1 > 0 && len2 > 0) ? std::min(len1, len2) : N;
        if (vm_state->register_types[dst] != TYPE_FLOAT_VECTOR) {
            int h_dst = acquire_float_vector(vm_state->query_context);
            if (h_dst < 0) {
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
            vm_state->registers[dst] = h_dst;
            vm_state->register_types[dst] = TYPE_FLOAT_VECTOR;
        }
        vm_state->query_context->float_vector_lengths[vm_state->registers[dst]] = active_len;
        float* dst_data = vm_state->query_context->float_vectors[vm_state->registers[dst]].data();
        const float* s1_data = vm_state->query_context->float_vectors[h_s1].data();
        const float* s2_data = vm_state->query_context->float_vectors[h_s2].data();

        #pragma omp parallel for schedule(static)
        for (size_t i = 0; i < N; ++i) {
            if (op_id == BINARY_OP_MUL) dst_data[i] = s1_data[i] * s2_data[i];
            else if (op_id == BINARY_OP_MIN) dst_data[i] = std::min(s1_data[i], s2_data[i]);
            else if (op_id == BINARY_OP_MAX) dst_data[i] = std::max(s1_data[i], s2_data[i]);
            else dst_data[i] = s1_data[i] * s2_data[i];
        }
    }

    vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;
    vm_state->pc++;
    DISPATCH();
}

