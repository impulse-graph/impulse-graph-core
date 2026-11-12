/**
 * @file op_8e_dense_walk.inc
 * @brief Formal Contract for OP_DENSE_WALK (0x8E).
 *
 * Syntax: OP_DENSE_WALK <dst_reg>, <src_reg> (payload: [rel_id:16 | ... | src:8])
 * Inputs:
 *   - dst_reg: Target bitset register receiving dense traversal frontier
 *   - src_reg (payload_low8): Source frontier bitset or scalar node ID
 * Preconditions:
 *   - src_reg must be TYPE_INT64, TYPE_NODE_ID, or TYPE_BITSET_HANDLE
 * Outputs:
 *   - dst_reg set to TYPE_BITSET_HANDLE
 *   - Flags: ZF set if output frontier is empty
 */
#ifndef OP_8E_CONTRACT_DEFINED
#define OP_8E_CONTRACT_DEFINED
#define OP_8E_DEFINED_FLAGS        0x00u
#define OP_8E_ALLOWED_SRC_TYPES    (IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_BITSET_HANDLE))
#define OP_8E_OUTPUT_DST_TYPES     (IMP_TYPE_BIT(TYPE_BITSET_HANDLE))
#endif

// Opcode 0x8E: OP_DENSE_WALK
op_DENSE_WALK: {
    IMPULSE_ASSERT(vm_state != nullptr);
    IMPULSE_VVA_ONLY(const uint16_t dst = IMP_INST_DST(bytecode[vm_state->pc]);)
    const impulse_vm_status_t st = execute_dense_walk_common(bytecode, vm_state, OP_8E_ALLOWED_SRC_TYPES);
    IMPULSE_TESTCASE(st != IMPULSE_VM_OK);
    if (st != IMPULSE_VM_OK) return st;
    IMPULSE_ASSERT(dst < IMP_VM_GPR_COUNT);
    IMPULSE_ASSERT((IMP_TYPE_BIT(vm_state->register_types[dst]) & OP_8E_OUTPUT_DST_TYPES) != 0u);
    IMPULSE_TESTCASE(IMP_VM_IS_ZF(vm_state));
    vm_state->pc++;
    DISPATCH();
}
