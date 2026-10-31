/**
 * Formal Opcode Contract: OP_RET (0x56)
 *
 * Layout:
 *   allowed_src:      none
 *   requires_ctx:     false
 */
#ifndef OP_56_CONTRACT_DEFINED
#define OP_56_CONTRACT_DEFINED
#define OP_56_DEFINED_FLAGS        0x00u
#define OP_56_OUTPUT_DST_TYPES     IMP_TYPES_NONE
#endif

// Opcode 0x56: OP_RET
op_RET: {
    if (vm_state->call_stack_depth == 0) return IMPULSE_VM_ERR_STACK_UNDERFLOW;
    uint32_t ret_pc = vm_state->call_stack[--vm_state->call_stack_depth];
    vm_state->call_stack[vm_state->call_stack_depth] = 0;
    vm_state->pc = ret_pc;
    DISPATCH();
}

