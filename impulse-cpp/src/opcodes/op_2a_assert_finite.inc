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
#endif

// Opcode 0x2A: OP_ASSERT_FINITE
op_ASSERT_FINITE: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t target_reg = inst.dst_reg;
    VALIDATE_REG(target_reg);

    if (vm_state->register_types[target_reg] == TYPE_FLOAT) {
        float v = *reinterpret_cast<const float*>(&vm_state->registers[target_reg]);
        if (std::isnan(v) || std::isinf(v)) {
            vm_state->registers[0] = 0;
            return IMPULSE_VM_ERR_FLOATING_POINT;
        }
    } else if (vm_state->register_types[target_reg] == TYPE_DOUBLE) {
        double v = *reinterpret_cast<const double*>(&vm_state->registers[target_reg]);
        if (std::isnan(v) || std::isinf(v)) {
            vm_state->registers[0] = 0;
            return IMPULSE_VM_ERR_FLOATING_POINT;
        }
    } else if (vm_state->register_types[target_reg] == TYPE_FLOAT_VECTOR) {
        int h = static_cast<int>(vm_state->registers[target_reg]);
        if (!vm_state->query_context || h < 0 || static_cast<size_t>(h) >= VM_MAX_VECTOR_HANDLES || !vm_state->query_context->float_vectors_allocated[h]) {
            return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        }
        const float* vec = vm_state->query_context->float_vectors[h].data();
        size_t N = vm_state->query_context->float_vectors[h].size();
        for (size_t i = 0; i < N; ++i) {
            float v = vec[i];
            if (std::isnan(v) || std::isinf(v)) {
                vm_state->registers[0] = i;
                return IMPULSE_VM_ERR_FLOATING_POINT;
            }
        }
    } else if (vm_state->register_types[target_reg] == TYPE_DOUBLE_VECTOR) {
        int h = static_cast<int>(vm_state->registers[target_reg]);
        if (!vm_state->query_context || h < 0 || static_cast<size_t>(h) >= VM_MAX_VECTOR_HANDLES || !vm_state->query_context->double_vectors_allocated[h]) {
            return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        }
        const double* vec = vm_state->query_context->double_vectors[h].data();
        size_t N = vm_state->query_context->double_vectors[h].size();
        for (size_t i = 0; i < N; ++i) {
            double v = vec[i];
            if (std::isnan(v) || std::isinf(v)) {
                vm_state->registers[0] = i;
                return IMPULSE_VM_ERR_FLOATING_POINT;
            }
        }
    }
    vm_state->pc++;
    DISPATCH();
}

