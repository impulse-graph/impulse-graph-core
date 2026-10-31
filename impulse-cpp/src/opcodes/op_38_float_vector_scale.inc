/**
 * Formal Opcode Contract: OP_FLOAT_VECTOR_SCALE (0x38)
 *
 * Layout:
 *   Variant 0 (flags == 0): In-place scale by immediate float factor
 *     dst_reg:        inst.dst_reg (DST_REG, TYPE_FLOAT_VECTOR)
 *     payload:        float immediate factor
 *     allowed_src:    none
 *     output_dst:     TYPE_FLOAT_VECTOR
 *     requires_ctx:   true
 *
 *   Variant 1 (flags == 1): In-place scale by register float factor
 *     dst_reg:        inst.dst_reg (DST_REG, TYPE_FLOAT_VECTOR)
 *     src_reg:        inst.payload & 0xFFFF (PAYLOAD_LOW16)
 *     allowed_src:    TYPE_FLOAT | TYPE_INT64 | TYPE_DOUBLE
 *     output_dst:     TYPE_FLOAT_VECTOR
 *     requires_ctx:   true
 */
#ifndef OP_38_CONTRACT_DEFINED
#define OP_38_CONTRACT_DEFINED
#define OP_38_DEFINED_FLAGS        0x01u
#define OP_38_ALLOWED_SRC_TYPES    (IMP_TYPE_BIT(TYPE_FLOAT) | IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_DOUBLE))
#define OP_38_OUTPUT_DST_TYPES     (IMP_TYPE_BIT(TYPE_FLOAT_VECTOR))
#endif

// Opcode 0x38: OP_FLOAT_VECTOR_SCALE
op_FLOAT_VECTOR_SCALE: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    VALIDATE_REG(dst);
    if (!vm_state->query_context) return IMPULSE_VM_ERR_NULL_SNAPSHOT;

    float factor = 1.0f;
    if (inst.flags == 0) {
        factor = reinterpret_cast<const float&>(inst.payload);
    } else {
        uint16_t src_reg = inst.payload & 0xFFFF;
        VALIDATE_REG(src_reg);
        IMPULSE_CHECK_REG_TYPE(vm_state, src_reg, OP_38_ALLOWED_SRC_TYPES);
        if (vm_state->register_types[src_reg] == TYPE_FLOAT) {
            factor = reinterpret_cast<const float&>(vm_state->registers[src_reg]);
        } else if (vm_state->register_types[src_reg] == TYPE_DOUBLE) {
            factor = static_cast<float>(reinterpret_cast<const double&>(vm_state->registers[src_reg]));
        } else {
            factor = static_cast<float>(static_cast<int64_t>(vm_state->registers[src_reg]));
        }
    }

    int handle = static_cast<int>(vm_state->registers[dst]);
    if (vm_state->register_types[dst] == TYPE_FLOAT_VECTOR && handle >= 0 && handle < static_cast<int>(vm_state->query_context->float_vectors.size())) {
        float* vec = vm_state->query_context->float_vectors[handle].data();
        size_t size = vm_state->query_context->max_nodes;
        impulse_simd_vector_scale_f32(vec, factor, size);
    }

    vm_state->pc++;
    DISPATCH();
}

