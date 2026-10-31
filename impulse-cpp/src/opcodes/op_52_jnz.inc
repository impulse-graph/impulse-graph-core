/**
 * Formal Opcode Contract: OP_JNZ (0x52)
 *
 * Layout:
 *   payload:          int32_t PC jump offset if ZF == 0
 *   allowed_src:      none
 *   requires_ctx:     false
 */
#ifndef OP_52_CONTRACT_DEFINED
#define OP_52_CONTRACT_DEFINED
#define OP_52_DEFINED_FLAGS        0x00u
#define OP_52_OUTPUT_DST_TYPES     IMP_TYPES_NONE
#endif

// Opcode 0x52: OP_JNZ
op_JNZ: {
    const auto& inst = bytecode[vm_state->pc];
    int32_t offset = static_cast<int32_t>(inst.payload);
    bool taken = !(vm_state->flags & IMPULSE_VM_FLAG_ZF);
    IMPULSE_TESTCASE(taken);
    IMPULSE_TESTCASE(offset < 0);
    if (IMPULSE_UNLIKELY(g_vm_branch_cov_fast)) {
        impulse_vm_branch_coverage_record(static_cast<uint32_t>(vm_state->pc), OP_JNZ, 1u,
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

