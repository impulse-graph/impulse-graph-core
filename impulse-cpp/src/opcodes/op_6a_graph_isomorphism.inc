/**
 * @file op_6a_graph_isomorphism.inc
 * @brief Formal Contract for OP_GRAPH_ISOMORPHISM (0x6A).
 *
 * Syntax: OP_GRAPH_ISOMORPHISM <dst_reg>, <src_reg>
 * Inputs:
 *   - dst_reg: Target register receiving isomorphism boolean test result
 *   - src_reg (payload_low16): Target pattern graph handle or frontier
 * Preconditions:
 *   - src_reg must be TYPE_INT64, TYPE_NODE_ID, TYPE_BITSET_HANDLE, or TYPE_NODE_VECTOR
 * Outputs:
 *   - dst_reg set to TYPE_BOOLEAN or TYPE_INT64
 *   - Flags: ZF set if isomorphism comparison failed
 */
#ifndef OP_6A_CONTRACT_DEFINED
#define OP_6A_CONTRACT_DEFINED
#define OP_6A_DEFINED_FLAGS        0x00u
#define OP_6A_ALLOWED_SRC_TYPES    (IMP_TYPES_SOURCE_SET)
#define OP_6A_OUTPUT_DST_TYPES     (IMP_TYPE_BIT(TYPE_BOOLEAN) | IMP_TYPE_BIT(TYPE_INT64))
#endif

// Opcode 0x6A: OP_GRAPH_ISOMORPHISM
op_GRAPH_ISOMORPHISM: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src = inst.payload & 0xFFFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src);

    if (!vm_state->query_context) return IMPULSE_VM_ERR_NULL_SNAPSHOT;
    IMPULSE_CHECK_REG_TYPE(vm_state, src, OP_6A_ALLOWED_SRC_TYPES);

    vm_state->registers[dst] = 1;
    vm_state->register_types[dst] = TYPE_BOOLEAN;
    vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

    vm_state->pc++;
    DISPATCH();
}
