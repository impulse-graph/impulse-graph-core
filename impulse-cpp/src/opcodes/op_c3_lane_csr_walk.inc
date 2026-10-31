// Opcode 0xC3: OP_LANE_CSR_WALK
op_LANE_CSR_WALK: {
    impulse_vm_status_t st = execute_lane_walk_common<false>(bytecode, vm_state);
    if (st != IMPULSE_VM_OK) return st;
    vm_state->pc++;
    DISPATCH();
}
