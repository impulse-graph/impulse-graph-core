/**
 * @file op_01_nop.inc
 * @brief Formal Contract for OP_NOP (0x01).
 *
 * Mnemonic: OP_NOP
 * Opcode: 0x01
 * Description: No operation; advances program counter to next instruction.
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
#ifndef OP_01_CONTRACT_DEFINED
#define OP_01_CONTRACT_DEFINED 1
#endif

// Opcode 0x01: OP_NOP
op_NOP:
    vm_state->pc++;
    DISPATCH();

