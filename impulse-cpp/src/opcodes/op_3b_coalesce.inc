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
    uint16_t dst = inst.dst_reg;
    uint16_t src1 = inst.payload & 0xFFFF;
    uint16_t src2 = (inst.payload >> 16) & 0xFFFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src1);
    VALIDATE_REG(src2);
    if (!vm_state->query_context) return IMPULSE_VM_ERR_NULL_SNAPSHOT;

    if (vm_state->register_types[src1] == TYPE_FLOAT_VECTOR && vm_state->register_types[src2] == TYPE_FLOAT_VECTOR) {
        int hDst = acquire_float_vector(vm_state->query_context);
        if (hDst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        int h1 = static_cast<int>(vm_state->registers[src1]);
        int h2 = static_cast<int>(vm_state->registers[src2]);
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
               (vm_state->register_types[src2] == TYPE_FLOAT || vm_state->register_types[src2] == TYPE_INT64)) {
        int hDst = acquire_float_vector(vm_state->query_context);
        if (hDst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        int h1 = static_cast<int>(vm_state->registers[src1]);
        auto& vDst = vm_state->query_context->float_vectors[hDst];
        auto& v1 = vm_state->query_context->float_vectors[h1];
        size_t len1 = vm_state->query_context->float_vector_lengths[h1];
        size_t active_len = (len1 > 0) ? len1 : v1.size();
        active_len = std::min(active_len, vDst.size());
        vm_state->query_context->float_vector_lengths[hDst] = active_len;
        float fallback = 0.0f;
        if (vm_state->register_types[src2] == TYPE_FLOAT) {
            uint32_t bits = static_cast<uint32_t>(vm_state->registers[src2] & 0xFFFFFFFFULL);
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
    } else if (vm_state->register_types[src1] != TYPE_NULL) {
        vm_state->registers[dst] = vm_state->registers[src1];
        vm_state->register_types[dst] = vm_state->register_types[src1];
    } else {
        vm_state->registers[dst] = vm_state->registers[src2];
        vm_state->register_types[dst] = vm_state->register_types[src2];
    }
    vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;
    vm_state->pc++;
    DISPATCH();
}

