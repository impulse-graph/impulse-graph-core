/**
 * @file op_00_halt.inc
 * @brief Formal Contract for OP_HALT (0x00).
 *
 * Mnemonic: OP_HALT
 * Opcode: 0x00
 * Description: Terminates VM execution cleanly and returns IMPULSE_VM_OK.
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
 *  - ZF: Unmodified
 *
 * Context Requirement:
 *  - Independent (requires_context = false)
 */
#ifndef OP_00_CONTRACT_DEFINED
#define OP_00_CONTRACT_DEFINED 1
#define OP_00_DEFINED_FLAGS IMP_FLAG_NONE
#endif

// Opcode 0x00: OP_HALT
op_HALT:
    IMPULSE_ASSERT(vm_state != nullptr);
    IMPULSE_TESTCASE(vm_state->pc == 0);
    return IMPULSE_VM_OK;
