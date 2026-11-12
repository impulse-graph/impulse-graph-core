/**
 * @file op_0f_csr_walk_state.inc
 * @brief Formal Contract for OP_CSR_WALK_STATE (0x0F).
 *
 * Mnemonic: OP_CSR_WALK_STATE
 * Opcode: 0x0F
 * Description: Initializes frontier traversal tracking state register.
 *
 * Operands:
 *  - dst = inst.dst_reg, rel = payload & 0xFFFF
 *
 * Inputs:
 *  - None
 *
 * Outputs:
 *  - dst: TYPE_FRONTIER_STATE
 *
 * Flags:
 *  - Unmodified
 *
 * Context Requirement:
 *  - Independent
 */
#ifndef OP_0F_CONTRACT_DEFINED
#define OP_0F_CONTRACT_DEFINED 1
#define OP_0F_DEFINED_FLAGS IMP_FLAG_EXTENDED_WORD
#endif

// Opcode 0x0F: OP_CSR_WALK_STATE
op_CSR_WALK_STATE: {
    const auto& inst = bytecode[vm_state->pc];
    const uint16_t dst = IMP_INST_DST(inst);
    VALIDATE_REG(dst);
    IMPULSE_ASSERT(dst < IMP_VM_GPR_COUNT);
    IMPULSE_TESTCASE(IMP_INST_IS_EXTENDED(inst));
    if (IMP_INST_IS_EXTENDED(inst)) {
        if (vm_state->pc + 1 >= instruction_count) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        uint16_t rel = IMP_INST_DST(bytecode[vm_state->pc + 1]);
        vm_state->pc++;
        IMP_REQUIRE_CONTEXT(vm_state);
        IMP_REQUIRE_REL_SLOT(vm_state, rel);
    }
    vm_state->register_types[dst] = TYPE_FRONTIER_STATE;
    vm_state->registers[dst] = 0;
    vm_state->pc++;
    DISPATCH();
}

