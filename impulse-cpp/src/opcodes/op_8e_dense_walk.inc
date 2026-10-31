// Opcode 0x8E: OP_DENSE_WALK
op_DENSE_WALK: {
    impulse_vm_status_t st = execute_dense_walk_common(bytecode, vm_state);
    if (st != IMPULSE_VM_OK) return st;
    vm_state->pc++;
    DISPATCH();
}
