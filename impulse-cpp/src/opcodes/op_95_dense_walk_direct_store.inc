// Opcode 0x95: OP_DENSE_WALK_DIRECT_STORE
op_DENSE_WALK_DIRECT_STORE: {
    impulse_vm_status_t st = execute_dense_walk_common(bytecode, vm_state);
    if (st != IMPULSE_VM_OK) return st;
    vm_state->pc++;
    DISPATCH();
}
