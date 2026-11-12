/**
 * Formal Opcode Contract: OP_JZ (0x51)
 *
 * Layout:
 *   payload:          int32_t PC jump offset if ZF == 1
 *   allowed_src:      none
 *   requires_ctx:     false
 */
#ifndef OP_51_CONTRACT_DEFINED
#define OP_51_CONTRACT_DEFINED
#define OP_51_DEFINED_FLAGS        0x00u
#define OP_51_OUTPUT_DST_TYPES     IMP_TYPES_NONE
#define OP_51_COV_SITE_ID          1u
#endif

// Opcode 0x51: OP_JZ
op_JZ: {
    IMPULSE_ASSERT(vm_state != nullptr);
    const auto& inst = bytecode[vm_state->pc];
    const int32_t offset = IMP_INST_PAYLOAD_I32(inst);
    const bool taken = IMP_VM_IS_ZF(vm_state);
    IMPULSE_TESTCASE(taken);
    IMPULSE_TESTCASE(offset < 0);
    IMPULSE_TESTCASE(offset == 0);
    IMP_VM_COV_BRANCH(vm_state, OP_JZ, OP_51_COV_SITE_ID, taken);
    if (taken) {
        vm_state->pc += offset;
    } else {
        vm_state->pc++;
    }
    DISPATCH();
}
