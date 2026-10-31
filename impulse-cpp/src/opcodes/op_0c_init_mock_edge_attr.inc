// Opcode 0x0C: OP_INIT_MOCK_EDGE_ATTR
op_INIT_MOCK_EDGE_ATTR: {
    impulse_vm_status_t st = execute_init_mock_attr<false>(bytecode, vm_state);
    if (st != IMPULSE_VM_OK) return st;
    vm_state->pc++;
    DISPATCH();
}
