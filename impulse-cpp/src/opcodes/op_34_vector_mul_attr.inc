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
#define OP_34_MODE_SCALAR_IMM      0u
#define OP_34_ALLOWED_SRC_TYPES    (IMP_TYPES_FLOAT_OR_DBL | IMP_TYPES_INT_OR_NODE)
#define OP_34_OUTPUT_DST_TYPES     (IMP_TYPES_FLOAT_OR_DBL)
#endif

// Opcode 0x34: OP_VECTOR_MUL_ATTR
op_VECTOR_MUL_ATTR: {
    const auto& inst = bytecode[vm_state->pc];
    const uint16_t dst = IMP_INST_DST(inst);
    VALIDATE_REG(dst);
    IMPULSE_ASSERT(dst < IMP_VM_GPR_COUNT);
    IMP_REQUIRE_CONTEXT(vm_state);
    IMPULSE_ASSERT(vm_state->query_context != nullptr);

    size_t size = vm_state->query_context->max_nodes;
    bool is_double = (vm_state->register_types[dst] == TYPE_DOUBLE_VECTOR);
    int h_dst = vm_state->registers[dst];

    if (!is_double && vm_state->register_types[dst] != TYPE_FLOAT_VECTOR) {
        h_dst = impulse_vm_context_acquire_float_vector(vm_state->query_context);
        if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        vm_state->registers[dst] = h_dst;
        vm_state->register_types[dst] = TYPE_FLOAT_VECTOR;
    }
    IMPULSE_TESTCASE(is_double);
    IMPULSE_TESTCASE(IMP_INST_FLAGS(inst) == OP_34_MODE_SCALAR_IMM);

    if (IMP_INST_FLAGS(inst) == OP_34_MODE_SCALAR_IMM) {
        uint32_t raw_payload = IMP_INST_PAYLOAD_U32(inst);
        float scalar = 0.0f;
        std::memcpy(&scalar, &raw_payload, sizeof(float));
        if (is_double) {
            IMPULSE_ASSERT(vm_state->registers[dst] < vm_state->query_context->double_vectors.size());
            double* vec = vm_state->query_context->double_vectors[vm_state->registers[dst]].data();
            for (size_t i = 0; i < size; ++i) vec[i] *= scalar;
        } else {
            IMPULSE_ASSERT(vm_state->registers[dst] < vm_state->query_context->float_vectors.size());
            float* vec = vm_state->query_context->float_vectors[vm_state->registers[dst]].data();
            impulse_simd_vector_scale_f32(vec, scalar, size);
        }
    } else {
        const uint8_t src_reg = IMP_INST_U8_0(inst);
        const uint8_t attr_id = IMP_INST_U8_1(inst);
        const uint8_t rel_id = IMP_INST_U8_2(inst);
        VALIDATE_REG(src_reg);
        IMPULSE_ASSERT(src_reg < IMP_VM_GPR_COUNT);
        IMPULSE_CHECK_REG_TYPE(vm_state, src_reg, OP_34_ALLOWED_SRC_TYPES);
        if (vm_state->register_types[src_reg] == TYPE_NULL ||
            ((1ULL << vm_state->register_types[src_reg]) & OP_34_ALLOWED_SRC_TYPES) == 0) {
            return IMPULSE_VM_ERR_INVALID_REGISTER;
        }

        if (rel_id >= vm_state->query_context->attribute_slots.size()) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        if (attr_id >= vm_state->query_context->attribute_slots[rel_id].size()) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        const auto& attr = vm_state->query_context->attribute_slots[rel_id][attr_id];
        if (!attr.data_ptr) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;

        const uint8_t base_type = IMP_ATTR_BASE_TYPE(attr.type_code);
        IMPULSE_TESTCASE(base_type == IMP_LEGACY_ALT_FLOAT32);

        if (is_double) {
            IMPULSE_ASSERT(vm_state->registers[dst] < vm_state->query_context->double_vectors.size());
            double* vec = vm_state->query_context->double_vectors[vm_state->registers[dst]].data();
#if defined(_OPENMP)
            #pragma omp parallel for schedule(static)
#endif
            for (size_t i = 0; i < size; ++i) {
                double val = 1.0;
                if (base_type == IMP_LEGACY_ALT_FLOAT32) val = static_cast<double>(static_cast<const float*>(attr.data_ptr)[i]);
                else if (base_type == IMP_LEGACY_ALT_FLOAT64) val = static_cast<const double*>(attr.data_ptr)[i];
                else if (base_type == IMP_TYPE_CODE_INT32) val = static_cast<double>(static_cast<const int32_t*>(attr.data_ptr)[i]);
                else val = static_cast<double>(static_cast<const int64_t*>(attr.data_ptr)[i]);
                vec[i] *= val;
            }
        } else {
            IMPULSE_ASSERT(vm_state->registers[dst] < vm_state->query_context->float_vectors.size());
            float* vec = vm_state->query_context->float_vectors[vm_state->registers[dst]].data();
            if (base_type == IMP_LEGACY_ALT_FLOAT32) {
                impulse_simd_vector_mul_f32(vec, static_cast<const float*>(attr.data_ptr), size);
            } else {
#if defined(_OPENMP)
                #pragma omp parallel for schedule(static)
#endif
                for (size_t i = 0; i < size; ++i) {
                    float val = 1.0f;
                    if (base_type == IMP_LEGACY_ALT_FLOAT64) val = static_cast<float>(static_cast<const double*>(attr.data_ptr)[i]);
                    else if (base_type == IMP_TYPE_CODE_INT32) val = static_cast<float>(static_cast<const int32_t*>(attr.data_ptr)[i]);
                    else val = static_cast<float>(static_cast<const int64_t*>(attr.data_ptr)[i]);
                    vec[i] *= val;
                }
            }
        }
    }

    vm_state->pc++;
    DISPATCH();
}

