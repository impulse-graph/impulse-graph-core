/**
 * @file op_62_scatter_gather.inc
 * @brief Formal Contract for OP_SCATTER_GATHER (0x62).
 *
 * Syntax: OP_SCATTER_GATHER <dst_reg>, <src_reg>
 * Inputs:
 *   - dst_reg: Target register receiving aggregated neighbor feature vector
 *   - src_reg (payload_low16): Source state vector or node frontier
 * Preconditions:
 *   - src_reg must be TYPE_FLOAT_VECTOR, TYPE_DOUBLE_VECTOR, TYPE_NODE_VECTOR, or TYPE_BITSET_HANDLE
 * Outputs:
 *   - dst_reg set to TYPE_FLOAT_VECTOR or TYPE_DOUBLE_VECTOR
 *   - Flags: ZF set if output vector has zero magnitude
 */
#ifndef OP_62_CONTRACT_DEFINED
#define OP_62_CONTRACT_DEFINED
#define OP_62_DEFINED_FLAGS        0x00u
#define OP_62_ALLOWED_SRC_TYPES    (IMP_TYPES_SOURCE_SET | IMP_TYPES_NUMERIC_VEC)
#define OP_62_OUTPUT_DST_TYPES     (IMP_TYPES_NUMERIC_VEC)
#endif

// Opcode 0x62: OP_SCATTER_GATHER
op_SCATTER_GATHER: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src = inst.payload & 0xFFFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src);

    if (!vm_state->query_context) return IMPULSE_VM_ERR_NULL_SNAPSHOT;
    IMPULSE_CHECK_REG_TYPE(vm_state, src, OP_62_ALLOWED_SRC_TYPES);

    int h = acquire_float_vector(vm_state->query_context);
    if (h < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    vm_state->registers[dst] = static_cast<uint64_t>(h);
    vm_state->register_types[dst] = TYPE_FLOAT_VECTOR;
    vm_state->flags |= IMPULSE_VM_FLAG_ZF;

    vm_state->pc++;
    DISPATCH();
}
