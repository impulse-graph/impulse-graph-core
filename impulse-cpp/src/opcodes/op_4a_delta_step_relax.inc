/**
 * Formal Opcode Contract: OP_DELTA_STEP_RELAX (0x4A)
 *
 * Layout:
 *   dst_reg:          inst.dst_reg (DST_REG)
 *   allowed_src:      none
 *   requires_ctx:     false
 */
#ifndef OP_4A_CONTRACT_DEFINED
#define OP_4A_CONTRACT_DEFINED
#define OP_4A_DEFINED_FLAGS        0x00u
#define OP_4A_OUTPUT_DST_TYPES     IMP_TYPES_NONE
#endif

// Opcode 0x4A: OP_DELTA_STEP_RELAX
op_DELTA_STEP_RELAX: {
    const auto& inst = bytecode[vm_state->pc];
    const uint16_t dst = IMP_INST_DST(inst);
    VALIDATE_REG(dst);
    IMPULSE_ASSERT(dst < IMP_VM_GPR_COUNT);
    IMPULSE_TESTCASE(dst == 0u);
    vm_state->pc++;
    DISPATCH();
}
