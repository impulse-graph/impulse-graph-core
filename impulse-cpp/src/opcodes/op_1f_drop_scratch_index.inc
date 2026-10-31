/**
 * @file op_1f_drop_scratch_index.inc
 * @brief Formal Contract for OP_DROP_SCRATCH_INDEX (0x1F).
 *
 * Mnemonic: OP_DROP_SCRATCH_INDEX
 * Opcode: 0x1F
 * Description: Releases transient thread-local scratch index structure.
 *
 * Operands:
 *  - None
 *
 * Inputs:
 *  - None
 *
 * Outputs:
 *  - None
 *
 * Flags:
 *  - Unmodified
 *
 * Context Requirement:
 *  - Independent
 */
#ifndef OP_1F_CONTRACT_DEFINED
#define OP_1F_CONTRACT_DEFINED 1
#endif

// Opcode 0x1F: OP_DROP_SCRATCH_INDEX
op_DROP_SCRATCH_INDEX: {
    vm_state->pc++;
    DISPATCH();
}

