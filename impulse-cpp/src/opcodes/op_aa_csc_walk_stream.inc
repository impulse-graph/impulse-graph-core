// Opcode 0xAA: OP_CSC_WALK_STREAM
op_CSC_WALK_STREAM: {
    impulse_vm_status_t st = execute_walk_stream_common<OP_CSC_WALK_STREAM>(bytecode, instruction_count, vm_state);
    if (st != IMPULSE_VM_OK) return st;
    DISPATCH();
}
