/**
 * Formal Opcode Contract: OP_VECTOR_MUL_ATTR (0x34)
 *
 * Layout:
 *   Variant 0 (flags == 0): Scale vector by scalar float immediate
 *     dst_reg:        inst.dst_reg (DST_REG)
 *     payload:        float scalar immediate
 *     allowed_src:    none
 *     output_dst:     TYPE_FLOAT_VECTOR | TYPE_DOUBLE_VECTOR
 *     requires_ctx:   true
 *
 *   Variant 1 (flags == 1): Multiply vector by attribute slot
 *     dst_reg:        inst.dst_reg (DST_REG)
 *     src_reg:        inst.payload & 0xFF (PAYLOAD_LOW8)
 *     attr_id:        (inst.payload >> 8) & 0xFF
 *     rel_id:         (inst.payload >> 16) & 0xFF
 *     allowed_src:    TYPE_FLOAT_VECTOR | TYPE_DOUBLE_VECTOR | IMP_TYPES_INT_OR_NODE
 *     output_dst:     TYPE_FLOAT_VECTOR | TYPE_DOUBLE_VECTOR
 *     requires_ctx:   true
 */
#ifndef OP_34_CONTRACT_DEFINED
#define OP_34_CONTRACT_DEFINED
#define OP_34_DEFINED_FLAGS        0x01u
#define OP_34_ALLOWED_SRC_TYPES    (IMP_TYPES_FLOAT_OR_DBL | IMP_TYPES_INT_OR_NODE)
#define OP_34_OUTPUT_DST_TYPES     (IMP_TYPES_FLOAT_OR_DBL)
#endif

// Opcode 0x34: OP_VECTOR_MUL_ATTR
op_VECTOR_MUL_ATTR: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    VALIDATE_REG(dst);
    if (!vm_state->query_context) return IMPULSE_VM_ERR_NULL_SNAPSHOT;

    size_t size = vm_state->query_context->max_nodes;
    bool is_double = (vm_state->register_types[dst] == TYPE_DOUBLE_VECTOR);
    int h_dst = vm_state->registers[dst];

    if (!is_double && vm_state->register_types[dst] != TYPE_FLOAT_VECTOR) {
        h_dst = impulse_vm_context_acquire_float_vector(vm_state->query_context);
        if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        vm_state->registers[dst] = h_dst;
        vm_state->register_types[dst] = TYPE_FLOAT_VECTOR;
    }

    if (inst.flags == 0) {
        float scalar = reinterpret_cast<const float&>(inst.payload);
        if (is_double) {
            double* vec = vm_state->query_context->double_vectors[vm_state->registers[dst]].data();
            for (size_t i = 0; i < size; ++i) vec[i] *= scalar;
        } else {
            float* vec = vm_state->query_context->float_vectors[vm_state->registers[dst]].data();
            impulse_simd_vector_scale_f32(vec, scalar, size);
        }
    } else {
        uint8_t src_reg = inst.payload & 0xFF;
        uint8_t attr_id = (inst.payload >> 8) & 0xFF;
        uint8_t rel_id = (inst.payload >> 16) & 0xFF;
        VALIDATE_REG(src_reg);
        IMPULSE_CHECK_REG_TYPE(vm_state, src_reg, OP_34_ALLOWED_SRC_TYPES);

        if (rel_id >= vm_state->query_context->attribute_slots.size()) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        if (attr_id >= vm_state->query_context->attribute_slots[rel_id].size()) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        const auto& attr = vm_state->query_context->attribute_slots[rel_id][attr_id];
        if (!attr.data_ptr) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;

        uint8_t base_type = attr.type_code & 0x7F;

        if (is_double) {
            double* vec = vm_state->query_context->double_vectors[vm_state->registers[dst]].data();
#if defined(_OPENMP)
            #pragma omp parallel for schedule(static)
#endif
            for (size_t i = 0; i < size; ++i) {
                double val = 1.0;
                if (base_type == 0x08) val = static_cast<double>(static_cast<const float*>(attr.data_ptr)[i]);
                else if (base_type == 0x09) val = static_cast<const double*>(attr.data_ptr)[i];
                else if (base_type == 0x03) val = static_cast<double>(static_cast<const int32_t*>(attr.data_ptr)[i]);
                else val = static_cast<double>(static_cast<const int64_t*>(attr.data_ptr)[i]);
                vec[i] *= val;
            }
        } else {
            float* vec = vm_state->query_context->float_vectors[vm_state->registers[dst]].data();
            if (base_type == 0x08) {
                impulse_simd_vector_mul_f32(vec, static_cast<const float*>(attr.data_ptr), size);
            } else {
#if defined(_OPENMP)
                #pragma omp parallel for schedule(static)
#endif
                for (size_t i = 0; i < size; ++i) {
                    float val = 1.0f;
                    if (base_type == 0x09) val = static_cast<float>(static_cast<const double*>(attr.data_ptr)[i]);
                    else if (base_type == 0x03) val = static_cast<float>(static_cast<const int32_t*>(attr.data_ptr)[i]);
                    else val = static_cast<float>(static_cast<const int64_t*>(attr.data_ptr)[i]);
                    vec[i] *= val;
                }
            }
        }
    }

    vm_state->pc++;
    DISPATCH();
}

