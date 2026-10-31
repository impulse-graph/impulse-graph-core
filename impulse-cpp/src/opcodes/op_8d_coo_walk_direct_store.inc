// Opcode 0x8D: OP_COO_WALK_DIRECT_STORE
op_COO_WALK_DIRECT_STORE: {
    impulse_vm_status_t st = execute_coo_walk_common(bytecode, vm_state);
    if (st != IMPULSE_VM_OK) return st;
    vm_state->pc++;
    DISPATCH();
}
