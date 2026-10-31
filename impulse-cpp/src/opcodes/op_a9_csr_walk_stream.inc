#ifndef OP_A9_CONTRACT_DEFINED
#define OP_A9_CONTRACT_DEFINED
/**
 * @file op_a9_csr_walk_stream.inc
 * @brief Opcode 0xA9: OP_CSR_WALK_STREAM — Executes fused edge stream shader over forward CSR edges.
 *
 * Formal Opcode Contract:
 *  - Opcode: 0xA9 (OP_CSR_WALK_STREAM)
 *  - Destination: dst_reg (RegFieldLayout::DST_REG)
 *  - Source: payload & 0xFFFF (RegFieldLayout::PAYLOAD_LOW16)
 *  - Relation: (payload >> 16) & 0xFFFF
 *  - Flags: Shader start PC offset
 *  - Allowed Source Types: TYPE_SOURCE_SET (Node ID, Int64, Node Vector, BitSet)
 *  - Output Destination Types: TYPE_BITSET_HANDLE
 *  - Flags: Sets IMPULSE_VM_FLAG_ZF if destination bitset is empty
 *  - Requirements: Requires valid query_context
 *  - Modifies Destination: True
 */
#define OP_A9_DEFINED_FLAGS        0x00u
#define OP_A9_ALLOWED_SRC_TYPES    (IMP_TYPES_SOURCE_SET)
#define OP_A9_OUTPUT_DST_TYPES     (IMP_TYPE_BIT(TYPE_BITSET_HANDLE))
#endif

// Opcode 0xA9: OP_CSR_WALK_STREAM
op_CSR_WALK_STREAM: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src = inst.payload & 0xFFFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src);
    if (!vm_state->query_context) return IMPULSE_VM_ERR_NULL_SNAPSHOT;
    IMPULSE_CHECK_REG_TYPE(vm_state, src, OP_A9_ALLOWED_SRC_TYPES);

    impulse_vm_status_t st = execute_walk_stream_common<OP_CSR_WALK_STREAM>(bytecode, instruction_count, vm_state);
    if (st != IMPULSE_VM_OK) return st;
    DISPATCH();
}
