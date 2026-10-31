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
#define OP_66_ALLOWED_SRC_TYPES    (IMP_TYPES_FLOAT_OR_DBL | IMP_TYPES_NODE_VEC)
#define OP_66_OUTPUT_DST_TYPES     (IMP_TYPES_FLOAT_OR_DBL)
#endif

// Opcode 0x66: OP_SPARSE_MATVEC
op_SPARSE_MATVEC: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src = inst.payload & 0xFFFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src);

    if (!vm_state->query_context) return IMPULSE_VM_ERR_NULL_SNAPSHOT;
    IMPULSE_CHECK_REG_TYPE(vm_state, src, OP_66_ALLOWED_SRC_TYPES);

    int h = acquire_float_vector(vm_state->query_context);
    if (h < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    vm_state->registers[dst] = static_cast<uint64_t>(h);
    vm_state->register_types[dst] = TYPE_FLOAT_VECTOR;
    vm_state->flags |= IMPULSE_VM_FLAG_ZF;

    vm_state->pc++;
    DISPATCH();
}
