// Opcode 0x8F: OP_DENSE_WALK_BITMATRIX
op_DENSE_WALK_BITMATRIX: {
    impulse_vm_status_t st = execute_dense_walk_common(bytecode, vm_state);
    if (st != IMPULSE_VM_OK) return st;
    vm_state->pc++;
    DISPATCH();
}
