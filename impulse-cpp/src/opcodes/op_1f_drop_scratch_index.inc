// Opcode 0x1F: OP_DROP_SCRATCH_INDEX
op_DROP_SCRATCH_INDEX: {
    vm_state->pc++;
    DISPATCH();
}

