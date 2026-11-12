/**
 * @file op_1e_create_scratch_index.inc
 * @brief Formal Contract for OP_CREATE_SCRATCH_INDEX (0x1E).
 *
 * Mnemonic: OP_CREATE_SCRATCH_INDEX
 * Opcode: 0x1E
 * Description: Instantiates transient thread-local scratch index structure.
 *
 * Operands:
 *  - None
 *
 * Inputs:
 *  - None
 *
 * Outputs:
 *  - None
 *
 * Flags:
 *  - Unmodified
 *
 * Context Requirement:
 *  - Independent
 */
#ifndef OP_1E_CONTRACT_DEFINED
#define OP_1E_CONTRACT_DEFINED 1
#define OP_1E_DEFINED_FLAGS IMP_FLAG_NONE
#endif

// Opcode 0x1E: OP_CREATE_SCRATCH_INDEX
op_CREATE_SCRATCH_INDEX: {
    IMPULSE_ASSERT(vm_state != nullptr);
    // Scratch index creation is an optional performance optimization (benign fault scope)
    impulse_faultsim_begin_benign();
    const int fault_rc = impulse_faultsim_step();
    impulse_faultsim_end_benign();
    IMPULSE_TESTCASE(fault_rc != 0);
    (void)fault_rc;
    vm_state->pc++;
    DISPATCH();
}

