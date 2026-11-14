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
#define OP_37_ALLOWED_SRC_TYPES    (IMP_TYPE_BIT(TYPE_STRING_VECTOR) | IMP_TYPE_BIT(TYPE_NULL) | IMP_TYPES_ANY)
#define OP_37_ALLOWED_AUX1_TYPES   (IMP_TYPE_BIT(TYPE_STRING_VECTOR) | IMP_TYPE_BIT(TYPE_NULL) | IMP_TYPES_ANY)
#define OP_37_OUTPUT_DST_TYPES     (IMP_TYPE_BIT(TYPE_STRING_VECTOR))
#endif

// Opcode 0x37: OP_VECTOR_STR_CONCAT
op_VECTOR_STR_CONCAT: {
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
    IMPULSE_CHECK_REG_TYPE(vm_state, src1, OP_37_ALLOWED_SRC_TYPES);
    IMPULSE_CHECK_REG_TYPE(vm_state, src2, OP_37_ALLOWED_AUX1_TYPES);
    if (vm_state->register_types[src1] != TYPE_STRING_VECTOR ||
        vm_state->register_types[src2] != TYPE_STRING_VECTOR) {
        return IMPULSE_VM_ERR_INVALID_REGISTER;
    }

    int h_dst = acquire_string_vector(vm_state->query_context);
    if (h_dst < 0) {
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
    IMPULSE_ASSERT(static_cast<size_t>(h_dst) < vm_state->query_context->string_vectors.size());
    auto& svec_dst = vm_state->query_context->string_vectors[h_dst];
    svec_dst.clear();

    IMPULSE_TESTCASE(src1 == src2);
    if (vm_state->register_types[src1] == TYPE_STRING_VECTOR) {
        int h_src1 = static_cast<int>(vm_state->registers[src1]);
        if (h_src1 < 0 || static_cast<size_t>(h_src1) >= vm_state->query_context->string_vectors.size()) {
            return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        }
        const auto& svec1 = vm_state->query_context->string_vectors[h_src1];
        for (const char* str : svec1) svec_dst.push_back(str);
    }
    if (vm_state->register_types[src2] == TYPE_STRING_VECTOR) {
        int h_src2 = static_cast<int>(vm_state->registers[src2]);
        if (h_src2 < 0 || static_cast<size_t>(h_src2) >= vm_state->query_context->string_vectors.size()) {
            return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        }
        const auto& svec2 = vm_state->query_context->string_vectors[h_src2];
        for (const char* str : svec2) svec_dst.push_back(str);
    }
    IMPULSE_TESTCASE(svec_dst.empty());

    vm_state->registers[dst] = h_dst;
    vm_state->register_types[dst] = TYPE_STRING_VECTOR;

    vm_state->pc++;
    DISPATCH();
}

