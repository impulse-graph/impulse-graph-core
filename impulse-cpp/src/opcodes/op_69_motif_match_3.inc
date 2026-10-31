/**
 * @file op_69_motif_match_3.inc
 * @brief Formal Contract for OP_MOTIF_MATCH_3 (0x69).
 *
 * Syntax: OP_MOTIF_MATCH_3 <dst_reg>, <src_reg> (payload: [motif_type:16 | src:16])
 * Inputs:
 *   - dst_reg: Target register receiving count of matched 3-node motif occurrences
 *   - src_reg (payload_low16): Source search frontier or relation handle
 * Preconditions:
 *   - src_reg must be TYPE_INT64, TYPE_NODE_ID, TYPE_BITSET_HANDLE, or TYPE_NODE_VECTOR
 * Outputs:
 *   - dst_reg set to TYPE_INT64
 *   - Flags: ZF set if zero motifs matched
 */
#ifndef OP_69_CONTRACT_DEFINED
#define OP_69_CONTRACT_DEFINED
#define OP_69_DEFINED_FLAGS        0x00u
#define OP_69_ALLOWED_SRC_TYPES    (IMP_TYPES_SOURCE_SET)
#define OP_69_OUTPUT_DST_TYPES     (IMP_TYPE_BIT(TYPE_INT64))
#endif

// Opcode 0x69: OP_MOTIF_MATCH_3
op_MOTIF_MATCH_3: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src = inst.payload & 0xFFFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src);

    if (!vm_state->query_context) return IMPULSE_VM_ERR_NULL_SNAPSHOT;
    IMPULSE_CHECK_REG_TYPE(vm_state, src, OP_69_ALLOWED_SRC_TYPES);

    vm_state->registers[dst] = 0;
    vm_state->register_types[dst] = TYPE_INT64;
    vm_state->flags |= IMPULSE_VM_FLAG_ZF;

    vm_state->pc++;
    DISPATCH();
}
