// Opcode 0x1E: OP_CREATE_SCRATCH_INDEX
op_CREATE_SCRATCH_INDEX: {
    // Scratch index creation is an optional performance optimization (benign fault scope)
    impulse_faultsim_begin_benign();
    (void)impulse_faultsim_step();
    impulse_faultsim_end_benign();
    vm_state->pc++;
    DISPATCH();
}

