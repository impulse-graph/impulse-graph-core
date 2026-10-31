// Opcode 0xA9: OP_CSR_WALK_STREAM
op_CSR_WALK_STREAM: {
    impulse_vm_status_t st = execute_walk_stream_common<OP_CSR_WALK_STREAM>(bytecode, instruction_count, vm_state);
    if (st != IMPULSE_VM_OK) return st;
    DISPATCH();
}
