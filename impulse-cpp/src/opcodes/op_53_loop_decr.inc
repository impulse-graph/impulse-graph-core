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
#define OP_53_COV_SITE_ID          1u
#endif

// Opcode 0x53: OP_LOOP_DECR
op_LOOP_DECR: {
    IMPULSE_ASSERT(vm_state != nullptr);
    const auto& inst = bytecode[vm_state->pc];
    const uint16_t dst = IMP_INST_DST(inst);
    IMP_VALIDATE_GPR(dst);
    IMPULSE_ASSERT(dst < IMP_VM_GPR_COUNT);

    int64_t val = static_cast<int64_t>(vm_state->registers[dst]);
    val--;
    vm_state->registers[dst] = static_cast<uint64_t>(val);
    vm_state->register_types[dst] = TYPE_INT64;
    IMP_VM_SET_ZF(vm_state, val == 0);

    const bool taken = (val > 0);
    IMPULSE_TESTCASE(taken);
    IMPULSE_TESTCASE(val == 0);
    IMPULSE_TESTCASE(val < 0);
    IMP_VM_COV_BRANCH(vm_state, OP_LOOP_DECR, OP_53_COV_SITE_ID, taken);
    if (taken) {
        const int32_t offset = IMP_INST_PAYLOAD_I32(inst);
        IMPULSE_TESTCASE(offset < 0);
        vm_state->pc += offset;
    } else {
        vm_state->pc++;
    }
    DISPATCH();
}
