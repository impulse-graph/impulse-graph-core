// Opcode 0x0B: OP_INIT_MOCK_NODE_ATTR
op_INIT_MOCK_NODE_ATTR: {
    impulse_vm_status_t st = execute_init_mock_attr<true>(bytecode, vm_state);
    if (st != IMPULSE_VM_OK) return st;
    vm_state->pc++;
    DISPATCH();
}
