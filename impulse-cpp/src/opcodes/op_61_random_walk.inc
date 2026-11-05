/**
 * @file op_61_random_walk.inc
 * @brief Formal Contract for OP_RANDOM_WALK (0x61).
 *
 * Syntax: OP_RANDOM_WALK <dst_reg>, <src_reg> (payload: [rel_id:16 | steps:16])
 * Inputs:
 *   - dst_reg: Target register receiving walk path node sequence
 *   - src_reg (payload_low16): Starting node ID or seed frontier
 * Preconditions:
 *   - src_reg must be TYPE_INT64, TYPE_NODE_ID, TYPE_BITSET_HANDLE, or TYPE_NODE_VECTOR
 * Outputs:
 *   - dst_reg set to TYPE_NODE_VECTOR
 *   - Flags: ZF set if walk terminated early / empty
 */
#ifndef OP_61_CONTRACT_DEFINED
#define OP_61_CONTRACT_DEFINED
#define OP_61_DEFINED_FLAGS        0x00u
#define OP_61_ALLOWED_SRC_TYPES    (IMP_TYPES_SOURCE_SET)
#define OP_61_OUTPUT_DST_TYPES     (IMP_TYPE_BIT(TYPE_NODE_VECTOR))
#endif

// Opcode 0x61: OP_RANDOM_WALK
op_RANDOM_WALK: {
    const auto& inst = bytecode[vm_state->pc];
    VALIDATE_REG(inst.dst_reg);

    vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;
    vm_state->pc++;
    DISPATCH();
}
