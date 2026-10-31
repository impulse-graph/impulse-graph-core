/**
 * @file op_67_louvain_modularity.inc
 * @brief Formal Contract for OP_LOUVAIN_MODULARITY (0x67).
 *
 * Syntax: OP_LOUVAIN_MODULARITY <dst_reg>, <src_reg>
 * Inputs:
 *   - dst_reg: Destination register receiving partition community vector
 *   - src_reg (payload_low16): Seed node frontier or relation handle
 * Preconditions:
 *   - src_reg must be TYPE_INT64, TYPE_NODE_ID, TYPE_BITSET_HANDLE, or TYPE_NODE_VECTOR
 * Outputs:
 *   - dst_reg set to TYPE_UINT64_VECTOR or TYPE_NODE_VECTOR
 *   - Flags: ZF set if zero communities detected
 */
#ifndef OP_67_CONTRACT_DEFINED
#define OP_67_CONTRACT_DEFINED
#define OP_67_DEFINED_FLAGS        0x00u
#define OP_67_ALLOWED_SRC_TYPES    (IMP_TYPES_SOURCE_SET)
#define OP_67_OUTPUT_DST_TYPES     (IMP_TYPE_BIT(TYPE_UINT64_VECTOR) | IMP_TYPE_BIT(TYPE_NODE_VECTOR))
#endif

// Opcode 0x67: OP_LOUVAIN_MODULARITY
op_LOUVAIN_MODULARITY: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src = inst.payload & 0xFFFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src);

    if (!vm_state->query_context) return IMPULSE_VM_ERR_NULL_SNAPSHOT;
    IMPULSE_CHECK_REG_TYPE(vm_state, src, OP_67_ALLOWED_SRC_TYPES);

    int h = acquire_node_vector(vm_state->query_context);
    if (h < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    vm_state->registers[dst] = static_cast<uint64_t>(h);
    vm_state->register_types[dst] = TYPE_NODE_VECTOR;
    vm_state->flags |= IMPULSE_VM_FLAG_ZF;

    vm_state->pc++;
    DISPATCH();
}
