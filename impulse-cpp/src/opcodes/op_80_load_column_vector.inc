/**
 * @file op_80_load_column_vector.inc
 * @brief Formal Contract for OP_LOAD_COLUMN_VECTOR (0x80).
 *
 * Syntax: OP_LOAD_COLUMN_VECTOR <dst_reg> (payload: [attr_idx:16 | domain_or_rel:16])
 * Inputs:
 *   - dst_reg: Target register receiving off-heap float column vector
 *   - payload: High 16 bits = attribute index, low 16 bits = domain / relation ID
 * Preconditions:
 *   - Snapshot attribute slot must exist and contain float data
 * Outputs:
 *   - dst_reg set to TYPE_FLOAT_VECTOR
 *   - Flags: None
 */
#ifndef OP_80_CONTRACT_DEFINED
#define OP_80_CONTRACT_DEFINED
#define OP_80_DEFINED_FLAGS        0x00u
#define OP_80_ALLOWED_SRC_TYPES    (IMP_TYPES_NONE)
#define OP_80_OUTPUT_DST_TYPES     (IMP_TYPE_BIT(TYPE_FLOAT_VECTOR))
#endif

// Opcode 0x80: OP_LOAD_COLUMN_VECTOR
op_LOAD_COLUMN_VECTOR: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t domain_or_rel = inst.payload & 0xFFFF;
    uint16_t attr_idx = (inst.payload >> 16) & 0xFFFF;
    VALIDATE_REG(dst);

    if (!vm_state->query_context) return IMPULSE_VM_ERR_NULL_SNAPSHOT;

    size_t N = vm_state->query_context->max_nodes;
    if (vm_state->register_types[dst] != TYPE_FLOAT_VECTOR) {
        int h = acquire_float_vector(vm_state->query_context);
        if (h < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        vm_state->registers[dst] = h;
        vm_state->register_types[dst] = TYPE_FLOAT_VECTOR;
    }
    int h_dst = static_cast<int>(vm_state->registers[dst]);
    float* dst_vec = vm_state->query_context->float_vectors[h_dst].data();

    if (domain_or_rel < vm_state->query_context->attribute_slots.size() &&
        attr_idx < vm_state->query_context->attribute_slots[domain_or_rel].size()) {
        const auto& slot = vm_state->query_context->attribute_slots[domain_or_rel][attr_idx];
        if (slot.data_ptr) {
            const float* src_attr = static_cast<const float*>(slot.data_ptr);
            size_t copy_n = std::min(N, (size_t)(slot.data_bytes / sizeof(float)));
            std::memcpy(dst_vec, src_attr, copy_n * sizeof(float));
        }
    }
    vm_state->pc++;
    DISPATCH();
}

