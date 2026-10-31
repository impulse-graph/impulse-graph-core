// Opcode 0xA0: OP_COO_WALK_STREAM
op_COO_WALK_STREAM: {
    impulse_vm_status_t st = execute_walk_stream_common<OP_COO_WALK_STREAM>(bytecode, instruction_count, vm_state);
    if (st != IMPULSE_VM_OK) return st;
    DISPATCH();
}
