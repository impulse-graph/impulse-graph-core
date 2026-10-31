/**
 * @file op_8f_dense_walk_bitmatrix.inc
 * @brief Formal Contract for OP_DENSE_WALK_BITMATRIX (0x8F).
 *
 * Syntax: OP_DENSE_WALK_BITMATRIX <dst_reg>, <src_reg> (payload: [rel_id:16 | ... | src:8])
 * Inputs:
 *   - dst_reg: Target bitset register receiving bitmatrix traversal frontier
 *   - src_reg (payload_low8): Source frontier bitset or scalar node ID
 * Preconditions:
 *   - src_reg must be TYPE_INT64, TYPE_NODE_ID, or TYPE_BITSET_HANDLE
 * Outputs:
 *   - dst_reg set to TYPE_BITSET_HANDLE
 *   - Flags: ZF set if output frontier is empty
 */
#ifndef OP_8F_CONTRACT_DEFINED
#define OP_8F_CONTRACT_DEFINED
#define OP_8F_DEFINED_FLAGS        0x00u
#define OP_8F_ALLOWED_SRC_TYPES    (IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_BITSET_HANDLE))
#define OP_8F_OUTPUT_DST_TYPES     (IMP_TYPE_BIT(TYPE_BITSET_HANDLE))
#endif

// Opcode 0x8F: OP_DENSE_WALK_BITMATRIX
op_DENSE_WALK_BITMATRIX: {
    impulse_vm_status_t st = execute_dense_walk_common(bytecode, vm_state, OP_8F_ALLOWED_SRC_TYPES);
    if (st != IMPULSE_VM_OK) return st;
    vm_state->pc++;
    DISPATCH();
}
