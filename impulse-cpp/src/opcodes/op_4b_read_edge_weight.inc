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
#define OP_4B_ALLOWED_SRC_TYPES    (IMP_TYPES_INT_OR_NODE)
#define OP_4B_ALLOWED_AUX1_TYPES   (IMP_TYPES_INT_OR_NODE)
#define OP_4B_OUTPUT_DST_TYPES     (IMP_TYPE_BIT(TYPE_FLOAT))
#endif

// Opcode 0x4B: OP_READ_EDGE_WEIGHT
op_READ_EDGE_WEIGHT: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint8_t u_reg = inst.payload & 0xFF;
    uint8_t v_reg = (inst.payload >> 8) & 0xFF;
    uint16_t rel;
    if (inst.flags & 0x80) {
        if (vm_state->pc + 1 >= instruction_count) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        rel = bytecode[vm_state->pc + 1].dst_reg;
        vm_state->pc++;
    } else {
        rel = (inst.payload >> 16) & 0xFFFF;
    }
    VALIDATE_REG(dst);
    VALIDATE_REG(u_reg);
    VALIDATE_REG(v_reg);
    if (!vm_state->query_context) return IMPULSE_VM_ERR_NULL_SNAPSHOT;
    IMPULSE_CHECK_REG_TYPE(vm_state, u_reg, OP_4B_ALLOWED_SRC_TYPES);
    IMPULSE_CHECK_REG_TYPE(vm_state, v_reg, OP_4B_ALLOWED_AUX1_TYPES);

    float weight = 1.0f;
    uint64_t u = vm_state->registers[u_reg];
    uint64_t v = vm_state->registers[v_reg];
    (void)v;

    if (rel < vm_state->query_context->attribute_slots.size() &&
        !vm_state->query_context->attribute_slots[rel].empty() &&
        vm_state->query_context->attribute_slots[rel][0].data_ptr != nullptr) {
        const auto& attr = vm_state->query_context->attribute_slots[rel][0];
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

