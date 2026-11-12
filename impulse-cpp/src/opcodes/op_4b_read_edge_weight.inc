/**
 * Formal Opcode Contract: OP_READ_EDGE_WEIGHT (0x4B)
 *
 * Layout:
 *   dst_reg:          inst.dst_reg (DST_REG)
 *   src_reg:          inst.payload & 0xFF (PAYLOAD_LOW8, u_reg)
 *   aux1_reg:         (inst.payload >> 8) & 0xFF (PAYLOAD_BYTE1, v_reg)
 *   rel_id:           (inst.payload >> 16) & 0xFFFF
 *   flags:            0x80: read rel_id from subsequent instruction
 *   allowed_src:      IMP_TYPES_INT_OR_NODE
 *   allowed_aux1:     IMP_TYPES_INT_OR_NODE
 *   output_dst:       TYPE_FLOAT
 *   requires_ctx:     true
 */
#ifndef OP_4B_CONTRACT_DEFINED
#define OP_4B_CONTRACT_DEFINED
#define OP_4B_DEFINED_FLAGS        0x80u
#define OP_4B_DEFAULT_EDGE_WEIGHT  1.0f
#define OP_4B_WEIGHT_ATTR_SLOT     0u
#define OP_4B_ALLOWED_SRC_TYPES    (IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_NULL))
#define OP_4B_ALLOWED_AUX1_TYPES   (IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_NULL))
#define OP_4B_OUTPUT_DST_TYPES     (IMP_TYPE_BIT(TYPE_FLOAT))
#endif

// Opcode 0x4B: OP_READ_EDGE_WEIGHT
op_READ_EDGE_WEIGHT: {
    const auto& inst = bytecode[vm_state->pc];
    const uint16_t dst = IMP_INST_DST(inst);
    const uint8_t u_reg = IMP_INST_U8_0(inst);
    const uint8_t v_reg = IMP_INST_U8_1(inst);
    uint16_t rel;
    IMPULSE_TESTCASE(IMP_INST_IS_EXTENDED(inst));
    if (IMP_INST_IS_EXTENDED(inst)) {
        if (vm_state->pc + 1 >= instruction_count) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        rel = IMP_INST_DST(bytecode[vm_state->pc + 1]);
        vm_state->pc++;
    } else {
        rel = IMP_INST_HIGH16(inst);
    }
    VALIDATE_REG(dst);
    VALIDATE_REG(u_reg);
    VALIDATE_REG(v_reg);
    IMPULSE_ASSERT(dst < IMP_VM_GPR_COUNT && u_reg < IMP_VM_GPR_COUNT && v_reg < IMP_VM_GPR_COUNT);
    IMP_REQUIRE_CONTEXT(vm_state);
    IMPULSE_ASSERT(vm_state->query_context != nullptr);
    IMPULSE_CHECK_REG_TYPE(vm_state, u_reg, OP_4B_ALLOWED_SRC_TYPES);
    IMPULSE_CHECK_REG_TYPE(vm_state, v_reg, OP_4B_ALLOWED_AUX1_TYPES);

    IMPULSE_TESTCASE(u_reg == v_reg);
    float weight = OP_4B_DEFAULT_EDGE_WEIGHT;
    uint64_t u = vm_state->registers[u_reg];
    uint64_t v = vm_state->registers[v_reg];
    (void)v;

    if (rel < vm_state->query_context->attribute_slots.size() &&
        !vm_state->query_context->attribute_slots[rel].empty() &&
        vm_state->query_context->attribute_slots[rel][OP_4B_WEIGHT_ATTR_SLOT].data_ptr != nullptr) {
        const auto& attr = vm_state->query_context->attribute_slots[rel][OP_4B_WEIGHT_ATTR_SLOT];
        const float* weights = static_cast<const float*>(attr.data_ptr);
        if (weights && u < vm_state->query_context->max_nodes) {
            weight = weights[u];
        }
    }

    vm_state->registers[dst] = 0;
    reinterpret_cast<float&>(vm_state->registers[dst]) = weight;
    vm_state->register_types[dst] = TYPE_FLOAT;

    vm_state->pc++;
    DISPATCH();
}

