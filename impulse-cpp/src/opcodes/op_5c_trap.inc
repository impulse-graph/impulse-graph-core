/**
 * Formal Opcode Contract: OP_TRAP (0x5C)
 *
 * Layout:
 *   allowed_src:      none
 *   requires_ctx:     false
 */
#ifndef OP_5C_CONTRACT_DEFINED
#define OP_5C_CONTRACT_DEFINED
#define OP_5C_DEFINED_FLAGS        0x00u
#define OP_5C_OUTPUT_DST_TYPES     IMP_TYPES_NONE
#endif

// Opcode 0x5C: OP_TRAP
op_TRAP: {
    return IMPULSE_VM_ERR_TRAP;
}

