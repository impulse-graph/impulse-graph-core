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
    IMPULSE_ASSERT(vm_state != nullptr);
    IMPULSE_TESTCASE(vm_state->pc == 0);
    return IMPULSE_VM_ERR_TRAP;
}
