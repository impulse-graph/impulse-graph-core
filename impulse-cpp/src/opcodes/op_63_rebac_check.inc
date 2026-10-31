/**
 * @file op_63_rebac_check.inc
 * @brief Formal Contract for OP_REBAC_CHECK (0x63).
 *
 * Syntax: OP_REBAC_CHECK <dst_reg>, <src_reg> (payload: [permission_id:32])
 * Inputs:
 *   - dst_reg: Target register receiving boolean check result (1 = allowed, 0 = denied)
 *   - src_reg (payload_low16): Subject entity node ID or frontier
 * Preconditions:
 *   - src_reg must be TYPE_NODE_ID, TYPE_INT64, or TYPE_BITSET_HANDLE
 * Outputs:
 *   - dst_reg set to TYPE_BOOLEAN or TYPE_INT64
 *   - Flags: ZF set if permission check evaluated to false / denied
 */
#ifndef OP_63_CONTRACT_DEFINED
#define OP_63_CONTRACT_DEFINED
#define OP_63_DEFINED_FLAGS        0x00u
#define OP_63_ALLOWED_SRC_TYPES    (IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_BITSET_HANDLE))
#define OP_63_OUTPUT_DST_TYPES     (IMP_TYPE_BIT(TYPE_BOOLEAN) | IMP_TYPE_BIT(TYPE_INT64))
#endif

// Opcode 0x63: OP_REBAC_CHECK
op_REBAC_CHECK: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src = inst.payload & 0xFFFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src);

    if (!vm_state->query_context) return IMPULSE_VM_ERR_NULL_SNAPSHOT;
    IMPULSE_CHECK_REG_TYPE(vm_state, src, OP_63_ALLOWED_SRC_TYPES);

    vm_state->registers[dst] = 0;
    vm_state->register_types[dst] = TYPE_BOOLEAN;
    vm_state->flags |= IMPULSE_VM_FLAG_ZF;

    vm_state->pc++;
    DISPATCH();
}
