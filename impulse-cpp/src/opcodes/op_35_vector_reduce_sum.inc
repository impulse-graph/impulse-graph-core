/**
 * Formal Opcode Contract: OP_VECTOR_REDUCE_SUM (0x35)
 *
 * Layout:
 *   dst_reg:          inst.dst_reg (DST_REG)
 *   src_reg:          inst.payload & 0xFFFF (PAYLOAD_LOW16)
 *   allowed_src:      IMP_TYPES_FLOAT_OR_DBL | TYPE_FLOAT | TYPE_DOUBLE
 *   output_dst:       TYPE_FLOAT | TYPE_DOUBLE
 *   flags:            ZF set if sum == 0.0
 *   requires_ctx:     true
 */
#ifndef OP_35_CONTRACT_DEFINED
#define OP_35_CONTRACT_DEFINED
#define OP_35_DEFINED_FLAGS        0x00u
#define OP_35_ALLOWED_SRC_TYPES    (IMP_TYPES_FLOAT_OR_DBL | IMP_TYPE_BIT(TYPE_FLOAT) | IMP_TYPE_BIT(TYPE_DOUBLE))
#define OP_35_OUTPUT_DST_TYPES     (IMP_TYPE_BIT(TYPE_FLOAT) | IMP_TYPE_BIT(TYPE_DOUBLE))
#endif

// Opcode 0x35: OP_VECTOR_REDUCE_SUM
op_VECTOR_REDUCE_SUM: {
    const auto& inst = bytecode[vm_state->pc];
    const uint16_t dst = IMP_INST_DST(inst);
    const uint16_t src = IMP_INST_LOW16(inst);
    VALIDATE_REG(dst);
    VALIDATE_REG(src);
    IMPULSE_ASSERT(dst < IMP_VM_GPR_COUNT && src < IMP_VM_GPR_COUNT);
    IMP_REQUIRE_CONTEXT(vm_state);
    IMPULSE_ASSERT(vm_state->query_context != nullptr);
    IMPULSE_CHECK_REG_TYPE(vm_state, src, OP_35_ALLOWED_SRC_TYPES);

    size_t size = vm_state->query_context->max_nodes;
    double sum = 0.0;
    const uint8_t src_type = vm_state->register_types[src];
    IMPULSE_TESTCASE(src_type == TYPE_DOUBLE_VECTOR);
    IMPULSE_TESTCASE(src_type == TYPE_FLOAT || src_type == TYPE_DOUBLE);
    IMPULSE_TESTCASE(dst == src);

    if (src_type == TYPE_DOUBLE_VECTOR) {
        size_t h = vm_state->registers[src];
        IMPULSE_ASSERT(h < vm_state->query_context->double_vectors.size());
        if (h < VM_MAX_VECTOR_HANDLES && vm_state->query_context->double_vector_lengths[h] > 0) {
            size = std::min(size, vm_state->query_context->double_vector_lengths[h]);
        }
        const double* vec = vm_state->query_context->double_vectors[h].data();
        for (size_t i = 0; i < size; ++i) {
            if (!std::isnan(vec[i])) sum += vec[i];
        }
        vm_state->registers[dst] = reinterpret_cast<uint64_t&>(sum);
        vm_state->register_types[dst] = TYPE_DOUBLE;
    } else if (src_type == TYPE_FLOAT_VECTOR) {
        size_t h = vm_state->registers[src];
        IMPULSE_ASSERT(h < vm_state->query_context->float_vectors.size());
        if (h < VM_MAX_VECTOR_HANDLES && vm_state->query_context->float_vector_lengths[h] > 0) {
            size = std::min(size, vm_state->query_context->float_vector_lengths[h]);
        }
        const float* vec = vm_state->query_context->float_vectors[h].data();
        float fsum = 0.0f;
        for (size_t i = 0; i < size; ++i) {
            if (!std::isnan(vec[i])) fsum += vec[i];
        }
        vm_state->registers[dst] = 0;
        reinterpret_cast<float&>(vm_state->registers[dst]) = fsum;
        vm_state->register_types[dst] = TYPE_FLOAT;
        sum = fsum;
    } else if (src_type == TYPE_FLOAT) {
        uint32_t bits = static_cast<uint32_t>(vm_state->registers[src] & IMP_SENTINEL_U32);
        float fval;
        std::memcpy(&fval, &bits, sizeof(float));
        vm_state->registers[dst] = 0;
        reinterpret_cast<float&>(vm_state->registers[dst]) = fval;
        vm_state->register_types[dst] = TYPE_FLOAT;
        sum = fval;
    } else if (src_type == TYPE_DOUBLE) {
        double dval;
        uint64_t bits = vm_state->registers[src];
        std::memcpy(&dval, &bits, sizeof(double));
        vm_state->registers[dst] = reinterpret_cast<uint64_t&>(dval);
        vm_state->register_types[dst] = TYPE_DOUBLE;
        sum = dval;
    } else {
        vm_state->registers[dst] = 0;
        vm_state->register_types[dst] = TYPE_NULL;
    }

    IMP_VM_SET_ZF(vm_state, sum == 0.0);

    vm_state->pc++;
    DISPATCH();
}

