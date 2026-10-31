/**
 * @file op_86_coo_walk.inc
 * @brief Formal Contract for OP_COO_WALK (0x86).
 *
 * Syntax: OP_COO_WALK <dst_reg>, <src_reg> (payload: [rel_id:16 | ... | src:8])
 * Inputs:
 *   - dst_reg: Target register receiving visited target bitset
 *   - src_reg (payload_low8): Source frontier bitset or scalar node ID
 * Preconditions:
 *   - src_reg must be TYPE_INT64, TYPE_NODE_ID, or TYPE_BITSET_HANDLE
 * Outputs:
 *   - dst_reg set to TYPE_BITSET_HANDLE
 *   - Flags: ZF set if traversal output is empty
 */
#ifndef OP_86_CONTRACT_DEFINED
#define OP_86_CONTRACT_DEFINED
#define OP_86_DEFINED_FLAGS        0x00u
#define OP_86_ALLOWED_SRC_TYPES    (IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_BITSET_HANDLE))
#define OP_86_OUTPUT_DST_TYPES     (IMP_TYPE_BIT(TYPE_BITSET_HANDLE))
#endif

// Opcode 0x86: OP_COO_WALK
op_COO_WALK: {
    impulse_vm_status_t st = execute_coo_walk_common(bytecode, vm_state);
    if (st != IMPULSE_VM_OK) return st;
    vm_state->pc++;
    DISPATCH();
}
