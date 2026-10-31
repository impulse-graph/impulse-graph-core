/**
 * Formal Opcode Contract: OP_THROW (0x5A)
 *
 * Layout:
 *   payload:          user error code payload (written to R0)
 *   allowed_src:      none
 *   requires_ctx:     false
 */
#ifndef OP_5A_CONTRACT_DEFINED
#define OP_5A_CONTRACT_DEFINED
#define OP_5A_DEFINED_FLAGS        0x00u
#define OP_5A_OUTPUT_DST_TYPES     IMP_TYPES_NONE
#endif

// Opcode 0x5A: OP_THROW
op_THROW: {
    const impulse_instruction_t& inst = bytecode[vm_state->pc];
    vm_state->registers[0] = inst.payload;
    vm_state->register_types[0] = TYPE_INT64;
    return IMPULSE_VM_ERR_USER_THROW;
}

