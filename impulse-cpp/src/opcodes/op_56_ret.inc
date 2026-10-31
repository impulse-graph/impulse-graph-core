// Opcode 0x56: OP_RET
op_RET: {
    if (vm_state->call_stack_depth == 0) return IMPULSE_VM_ERR_STACK_UNDERFLOW;
    vm_state->pc = vm_state->call_stack[--vm_state->call_stack_depth];
    DISPATCH();
}

