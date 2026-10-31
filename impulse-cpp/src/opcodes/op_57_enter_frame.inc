/**
 * Formal Opcode Contract: OP_ENTER_FRAME (0x57)
 *
 * Layout:
 *   allowed_src:      none
 *   requires_ctx:     false
 */
#ifndef OP_57_CONTRACT_DEFINED
#define OP_57_CONTRACT_DEFINED
#define OP_57_DEFINED_FLAGS        0x00u
#define OP_57_OUTPUT_DST_TYPES     IMP_TYPES_NONE
#endif

// Opcode 0x57: OP_ENTER_FRAME
op_ENTER_FRAME:
    vm_state->pc++;
    DISPATCH();

