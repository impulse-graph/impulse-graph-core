// Opcode 0xC4: OP_LANE_CSC_WALK
op_LANE_CSC_WALK: {
    impulse_vm_status_t st = execute_lane_walk_common<true>(bytecode, vm_state);
    if (st != IMPULSE_VM_OK) return st;
    vm_state->pc++;
    DISPATCH();
}
