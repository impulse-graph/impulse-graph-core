// Opcode 0x86: OP_COO_WALK
op_COO_WALK: {
    impulse_vm_status_t st = execute_coo_walk_common(bytecode, vm_state);
    if (st != IMPULSE_VM_OK) return st;
    vm_state->pc++;
    DISPATCH();
}
