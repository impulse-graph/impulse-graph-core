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
#define OP_38_MODE_IMM_FACTOR      0u
#define OP_38_ALLOWED_SRC_TYPES    (IMP_TYPE_BIT(TYPE_FLOAT) | IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_DOUBLE))
#define OP_38_OUTPUT_DST_TYPES     (IMP_TYPE_BIT(TYPE_FLOAT_VECTOR))
#endif

// Opcode 0x38: OP_FLOAT_VECTOR_SCALE
op_FLOAT_VECTOR_SCALE: {
    const auto& inst = bytecode[vm_state->pc];
    const uint16_t dst = IMP_INST_DST(inst);
    VALIDATE_REG(dst);
    IMPULSE_ASSERT(dst < IMP_VM_GPR_COUNT);
    IMP_REQUIRE_CONTEXT(vm_state);
    IMPULSE_ASSERT(vm_state->query_context != nullptr);

    float factor = 1.0f;
    IMPULSE_TESTCASE(IMP_INST_FLAGS(inst) == OP_38_MODE_IMM_FACTOR);
    if (IMP_INST_FLAGS(inst) == OP_38_MODE_IMM_FACTOR) {
        uint32_t raw_payload = IMP_INST_PAYLOAD_U32(inst);
        std::memcpy(&factor, &raw_payload, sizeof(float));
    } else {
        const uint16_t src_reg = IMP_INST_LOW16(inst);
        VALIDATE_REG(src_reg);
        IMPULSE_ASSERT(src_reg < IMP_VM_GPR_COUNT);
        IMPULSE_CHECK_REG_TYPE(vm_state, src_reg, OP_38_ALLOWED_SRC_TYPES);
        if (vm_state->register_types[src_reg] == TYPE_FLOAT) {
            factor = reinterpret_cast<const float&>(vm_state->registers[src_reg]);
        } else if (vm_state->register_types[src_reg] == TYPE_DOUBLE) {
            factor = static_cast<float>(reinterpret_cast<const double&>(vm_state->registers[src_reg]));
        } else {
            factor = static_cast<float>(static_cast<int64_t>(vm_state->registers[src_reg]));
        }
    }
    IMPULSE_TESTCASE(factor == 0.0f);

    int handle = static_cast<int>(vm_state->registers[dst]);
    if (vm_state->register_types[dst] == TYPE_FLOAT_VECTOR && handle >= 0 &&
        static_cast<size_t>(handle) < VM_MAX_VECTOR_HANDLES &&
        vm_state->query_context->float_vectors_allocated[handle]) {
        float* vec = vm_state->query_context->float_vectors[handle].data();
        size_t cap = vm_state->query_context->float_vectors[handle].size();
        size_t size = std::min(vm_state->query_context->max_nodes, cap);
        impulse_simd_vector_scale_f32(vec, factor, size);
    }

    vm_state->pc++;
    DISPATCH();
}

