/**
 * @file op_66_sparse_matvec.inc
 * @brief Formal Contract for OP_SPARSE_MATVEC (0x66).
 *
 * Syntax: OP_SPARSE_MATVEC <dst_reg>, <src_reg> (payload: [rel_id:16 | ...])
 * Inputs:
 *   - dst_reg: Destination vector receiving matrix-vector product
 *   - src_reg (payload_low16): Input multiplication vector
 * Preconditions:
 *   - src_reg must be TYPE_FLOAT_VECTOR, TYPE_DOUBLE_VECTOR, or TYPE_NODE_VECTOR
 * Outputs:
 *   - dst_reg set to TYPE_FLOAT_VECTOR or TYPE_DOUBLE_VECTOR
 *   - Flags: ZF set if resulting vector is all zeros
 */
#ifndef OP_66_CONTRACT_DEFINED
#define OP_66_CONTRACT_DEFINED
#define OP_66_DEFINED_FLAGS        0x00u
#define OP_66_ALLOWED_SRC_TYPES    (IMP_TYPES_FLOAT_OR_DBL | IMP_TYPES_NODE_VEC | IMP_TYPE_BIT(TYPE_NULL))
#define OP_66_OUTPUT_DST_TYPES     (IMP_TYPES_FLOAT_OR_DBL)
#endif

// Opcode 0x66: OP_SPARSE_MATVEC
op_SPARSE_MATVEC: {
    IMPULSE_ASSERT(vm_state != nullptr);
    const auto& inst = bytecode[vm_state->pc];
    const uint16_t dst = IMP_INST_DST(inst);
    const uint16_t src = IMP_INST_LOW16(inst);
    IMP_VALIDATE_GPR(dst);
    IMP_VALIDATE_GPR(src);

    IMP_REQUIRE_CONTEXT(vm_state);
    IMPULSE_ASSERT(vm_state->query_context != nullptr);
    IMPULSE_ASSERT(dst < IMP_VM_GPR_COUNT && src < IMP_VM_GPR_COUNT);
    IMPULSE_CHECK_REG_TYPE(vm_state, src, OP_66_ALLOWED_SRC_TYPES);
    if (((1ULL << vm_state->register_types[src]) & OP_66_ALLOWED_SRC_TYPES) == 0) {
        return IMPULSE_VM_ERR_INVALID_REGISTER;
    }
    IMPULSE_TESTCASE(dst == src);

    const int h = acquire_float_vector(vm_state->query_context);
    if (h < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    IMPULSE_ASSERT(h >= 0 && static_cast<size_t>(h) < vm_state->query_context->float_vectors.size());
    vm_state->registers[dst] = static_cast<uint64_t>(h);
    vm_state->register_types[dst] = TYPE_FLOAT_VECTOR;
    IMP_VM_SET_ZF(vm_state, true);

    vm_state->pc++;
    DISPATCH();
}
