/**
 * @file op_2a_assert_finite.inc
 * @brief Formal Contract for OP_ASSERT_FINITE (0x2A).
 *
 * Mnemonic: OP_ASSERT_FINITE
 * Opcode: 0x2A
 * Description: Asserts that target numerical register or vector elements are finite (non-NaN, non-Inf).
 *
 * Operands:
 *  - target = inst.dst_reg
 *
 * Inputs:
 *  - target: TYPE_FLOAT | TYPE_DOUBLE | NUMERIC_VEC
 *
 * Outputs:
 *  - None (throws IMPULSE_VM_ERR_FLOATING_POINT on non-finite)
 *
 * Flags:
 *  - Unmodified
 *
 * Context Requirement:
 *  - Independent
 */
#ifndef OP_2A_CONTRACT_DEFINED
#define OP_2A_CONTRACT_DEFINED 1
#define OP_2A_ERR_DIAG_REG 0u
#endif

// Opcode 0x2A: OP_ASSERT_FINITE
op_ASSERT_FINITE: {
    const auto& inst = bytecode[vm_state->pc];
    const uint16_t target_reg = IMP_INST_DST(inst);
    VALIDATE_REG(target_reg);
    IMPULSE_ASSERT(target_reg < IMP_VM_GPR_COUNT);

    const uint8_t rtype = vm_state->register_types[target_reg];
    IMPULSE_TESTCASE(rtype == TYPE_FLOAT || rtype == TYPE_DOUBLE);
    IMPULSE_TESTCASE(rtype == TYPE_FLOAT_VECTOR || rtype == TYPE_DOUBLE_VECTOR);

    if (rtype == TYPE_FLOAT) {
        float v = *reinterpret_cast<const float*>(&vm_state->registers[target_reg]);
        const bool non_finite = std::isnan(v) || std::isinf(v);
        IMPULSE_TESTCASE(non_finite);
        if (non_finite) {
            vm_state->registers[OP_2A_ERR_DIAG_REG] = 0;
            vm_state->register_types[OP_2A_ERR_DIAG_REG] = TYPE_INT64;
            return IMPULSE_VM_ERR_FLOATING_POINT;
        }
    } else if (rtype == TYPE_DOUBLE) {
        double v = *reinterpret_cast<const double*>(&vm_state->registers[target_reg]);
        const bool non_finite = std::isnan(v) || std::isinf(v);
        IMPULSE_TESTCASE(non_finite);
        if (non_finite) {
            vm_state->registers[OP_2A_ERR_DIAG_REG] = 0;
            vm_state->register_types[OP_2A_ERR_DIAG_REG] = TYPE_INT64;
            return IMPULSE_VM_ERR_FLOATING_POINT;
        }
    } else if (rtype == TYPE_FLOAT_VECTOR) {
        int h = static_cast<int>(vm_state->registers[target_reg]);
        if (!vm_state->query_context || h < 0 || static_cast<size_t>(h) >= VM_MAX_VECTOR_HANDLES || !vm_state->query_context->float_vectors_allocated[h]) {
            return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        }
        IMPULSE_ASSERT(vm_state->query_context != nullptr);
        const float* vec = vm_state->query_context->float_vectors[h].data();
        size_t N = vm_state->query_context->float_vectors[h].size();
        for (size_t i = 0; i < N; ++i) {
            float v = vec[i];
            if (std::isnan(v) || std::isinf(v)) {
                IMPULSE_TESTCASE(i == 0u);
                vm_state->registers[OP_2A_ERR_DIAG_REG] = i;
                vm_state->register_types[OP_2A_ERR_DIAG_REG] = TYPE_INT64;
                return IMPULSE_VM_ERR_FLOATING_POINT;
            }
        }
    } else if (rtype == TYPE_DOUBLE_VECTOR) {
        int h = static_cast<int>(vm_state->registers[target_reg]);
        if (!vm_state->query_context || h < 0 || static_cast<size_t>(h) >= VM_MAX_VECTOR_HANDLES || !vm_state->query_context->double_vectors_allocated[h]) {
            return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        }
        IMPULSE_ASSERT(vm_state->query_context != nullptr);
        const double* vec = vm_state->query_context->double_vectors[h].data();
        size_t N = vm_state->query_context->double_vectors[h].size();
        for (size_t i = 0; i < N; ++i) {
            double v = vec[i];
            if (std::isnan(v) || std::isinf(v)) {
                IMPULSE_TESTCASE(i == 0u);
                vm_state->registers[OP_2A_ERR_DIAG_REG] = i;
                vm_state->register_types[OP_2A_ERR_DIAG_REG] = TYPE_INT64;
                return IMPULSE_VM_ERR_FLOATING_POINT;
            }
        }
    }
    vm_state->pc++;
    DISPATCH();
}

