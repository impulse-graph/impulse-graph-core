/**
 * Formal Opcode Contract: OP_LEAVE_FRAME (0x58)
 *
 * Layout:
 *   allowed_src:      none
 *   requires_ctx:     false
 */
#ifndef OP_58_CONTRACT_DEFINED
#define OP_58_CONTRACT_DEFINED
#define OP_58_DEFINED_FLAGS        0x00u
#define OP_58_OUTPUT_DST_TYPES     IMP_TYPES_NONE
#endif

// Opcode 0x58: OP_LEAVE_FRAME
op_LEAVE_FRAME:
    vm_state->pc++;
    DISPATCH();

