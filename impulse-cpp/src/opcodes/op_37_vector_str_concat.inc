/**
 * Formal Opcode Contract: OP_VECTOR_STR_CONCAT (0x37)
 *
 * Layout:
 *   dst_reg:          inst.dst_reg (DST_REG)
 *   src_reg:          inst.payload & 0xFFFF (PAYLOAD_LOW16)
 *   aux1_reg:         (inst.payload >> 16) & 0xFFFF (PAYLOAD_HIGH16)
 *   allowed_src:      TYPE_STRING_VECTOR
 *   allowed_aux1:     TYPE_STRING_VECTOR
 *   output_dst:       TYPE_STRING_VECTOR
 *   requires_ctx:     true
 */
#ifndef OP_37_CONTRACT_DEFINED
#define OP_37_CONTRACT_DEFINED
#define OP_37_DEFINED_FLAGS        0x00u
#define OP_37_ALLOWED_SRC_TYPES    (IMP_TYPE_BIT(TYPE_STRING_VECTOR))
#define OP_37_ALLOWED_AUX1_TYPES   (IMP_TYPE_BIT(TYPE_STRING_VECTOR))
#define OP_37_OUTPUT_DST_TYPES     (IMP_TYPE_BIT(TYPE_STRING_VECTOR))
#endif

// Opcode 0x37: OP_VECTOR_STR_CONCAT
op_VECTOR_STR_CONCAT: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src1 = inst.payload & 0xFFFF;
    uint16_t src2 = (inst.payload >> 16) & 0xFFFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src1);
    VALIDATE_REG(src2);
    if (!vm_state->query_context) return IMPULSE_VM_ERR_NULL_SNAPSHOT;
    IMPULSE_CHECK_REG_TYPE(vm_state, src1, OP_37_ALLOWED_SRC_TYPES);
    IMPULSE_CHECK_REG_TYPE(vm_state, src2, OP_37_ALLOWED_AUX1_TYPES);

    int h_dst = acquire_string_vector(vm_state->query_context);
    if (h_dst < 0) {
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
    auto& svec_dst = vm_state->query_context->string_vectors[h_dst];
    svec_dst.clear();

    if (vm_state->register_types[src1] == TYPE_STRING_VECTOR) {
        int h_src1 = static_cast<int>(vm_state->registers[src1]);
        const auto& svec1 = vm_state->query_context->string_vectors[h_src1];
        for (const char* str : svec1) svec_dst.push_back(str);
    }
    if (vm_state->register_types[src2] == TYPE_STRING_VECTOR) {
        int h_src2 = static_cast<int>(vm_state->registers[src2]);
        const auto& svec2 = vm_state->query_context->string_vectors[h_src2];
        for (const char* str : svec2) svec_dst.push_back(str);
    }

    vm_state->registers[dst] = h_dst;
    vm_state->register_types[dst] = TYPE_STRING_VECTOR;

    vm_state->pc++;
    DISPATCH();
}

