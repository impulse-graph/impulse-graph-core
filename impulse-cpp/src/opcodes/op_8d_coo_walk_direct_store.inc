/**
 * @file op_8d_coo_walk_direct_store.inc
 * @brief Formal Contract for OP_COO_WALK_DIRECT_STORE (0x8D).
 *
 * Syntax: OP_COO_WALK_DIRECT_STORE <dst_reg>, <src_reg> (payload: [rel_id:16 | ... | src:8])
 * Inputs:
 *   - dst_reg: Target bitset register directly storing visited target bits
 *   - src_reg (payload_low8): Source frontier bitset or scalar node ID
 * Preconditions:
 *   - src_reg must be TYPE_INT64, TYPE_NODE_ID, or TYPE_BITSET_HANDLE
 * Outputs:
 *   - dst_reg set to TYPE_BITSET_HANDLE
 *   - Flags: ZF set if traversal output is empty
 */
#ifndef OP_8D_CONTRACT_DEFINED
#define OP_8D_CONTRACT_DEFINED
#define OP_8D_DEFINED_FLAGS        0x00u
#define OP_8D_ALLOWED_SRC_TYPES    (IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_BITSET_HANDLE))
#define OP_8D_OUTPUT_DST_TYPES     (IMP_TYPE_BIT(TYPE_BITSET_HANDLE))
#endif

// Opcode 0x8D: OP_COO_WALK_DIRECT_STORE
op_COO_WALK_DIRECT_STORE: {
    impulse_vm_status_t st = execute_coo_walk_common(bytecode, vm_state);
    if (st != IMPULSE_VM_OK) return st;
    vm_state->pc++;
    DISPATCH();
}
