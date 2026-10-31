// Opcode 0x51: OP_JZ
op_JZ: {
    const auto& inst = bytecode[vm_state->pc];
    int32_t offset = static_cast<int32_t>(inst.payload);
    bool taken = (vm_state->flags & IMPULSE_VM_FLAG_ZF) != 0;
    IMPULSE_TESTCASE(taken);
    IMPULSE_TESTCASE(offset < 0);
    if (IMPULSE_UNLIKELY(g_vm_branch_cov_fast)) {
        impulse_vm_branch_coverage_record(static_cast<uint32_t>(vm_state->pc), OP_JZ, 1u,
                                          IMPULSE_COV_KIND_TWO_WAY, IMPULSE_COV_BOTH,
                                          taken ? 0u : 1u);
    }
    if (taken) {
        vm_state->pc += offset;
    } else {
        vm_state->pc++;
    }
    DISPATCH();
}

