/**
 * @file op_60_sample_neighbors.inc
 * @brief Formal Contract for OP_SAMPLE_NEIGHBORS (0x60).
 *
 * Syntax: OP_SAMPLE_NEIGHBORS <dst_reg>, <src_reg> (payload: [rel_id:16 | k_samples:16])
 * Inputs:
 *   - dst_reg: Output register receiving sampled neighbors
 *   - src_reg (payload_low16): Source node ID or frontier
 * Preconditions:
 *   - src_reg must be a valid register holding TYPE_INT64, TYPE_NODE_ID, TYPE_BITSET_HANDLE, or TYPE_NODE_VECTOR
 * Outputs:
 *   - dst_reg set to TYPE_NODE_VECTOR or TYPE_BITSET_HANDLE
 *   - Flags: ZF set if result is empty
 */
#ifndef OP_60_CONTRACT_DEFINED
#define OP_60_CONTRACT_DEFINED
#define OP_60_DEFINED_FLAGS        0x00u
#define OP_60_ALLOWED_SRC_TYPES    (IMP_TYPES_SOURCE_SET)
#define OP_60_OUTPUT_DST_TYPES     (IMP_TYPE_BIT(TYPE_NODE_VECTOR) | IMP_TYPE_BIT(TYPE_BITSET_HANDLE))
#endif

// Opcode 0x60: OP_SAMPLE_NEIGHBORS
op_SAMPLE_NEIGHBORS: {
    const auto& inst = bytecode[vm_state->pc];
    VALIDATE_REG(inst.dst_reg);

    vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;
    vm_state->pc++;
    DISPATCH();
}
