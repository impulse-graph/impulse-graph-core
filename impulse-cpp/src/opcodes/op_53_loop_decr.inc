/**
 * Formal Opcode Contract: OP_LOOP_DECR (0x53)
 *
 * Layout:
 *   dst_reg:          inst.dst_reg (DST_REG, counter)
 *   payload:          int32_t PC jump offset if decremented counter > 0
 *   allowed_src:      none
 *   output_dst:       TYPE_INT64
 *   flags:            ZF set if counter == 0
 *   requires_ctx:     false
 */
#ifndef OP_53_CONTRACT_DEFINED
#define OP_53_CONTRACT_DEFINED
#define OP_53_DEFINED_FLAGS        0x00u
#define OP_53_OUTPUT_DST_TYPES     (IMP_TYPE_BIT(TYPE_INT64))
#endif

// Opcode 0x53: OP_LOOP_DECR
op_LOOP_DECR: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    VALIDATE_REG(dst);
    int64_t val = static_cast<int64_t>(vm_state->registers[dst]);
    val--;
    vm_state->registers[dst] = static_cast<uint64_t>(val);
    vm_state->register_types[dst] = TYPE_INT64;
    if (val == 0) {
        vm_state->flags |= IMPULSE_VM_FLAG_ZF;
    } else {
        vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;
    }
    bool taken = (val > 0);
    IMPULSE_TESTCASE(taken);
    IMPULSE_TESTCASE(val == 0);
    if (IMPULSE_UNLIKELY(g_vm_branch_cov_fast)) {
        impulse_vm_branch_coverage_record(static_cast<uint32_t>(vm_state->pc), OP_LOOP_DECR, 1u,
                                          IMPULSE_COV_KIND_TWO_WAY, IMPULSE_COV_BOTH,
                                          taken ? 0u : 1u);
    }
    if (taken) {
        int32_t offset = static_cast<int32_t>(inst.payload);
        vm_state->pc += offset;
    } else {
        vm_state->pc++;
    }
    DISPATCH();
}

