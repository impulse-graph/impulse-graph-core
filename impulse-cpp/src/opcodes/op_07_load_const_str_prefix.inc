/**
 * @file op_07_load_const_str_prefix.inc
 * @brief Formal Contract for OP_LOAD_CONST_STR_PREFIX (0x07).
 *
 * Mnemonic: OP_LOAD_CONST_STR_PREFIX
 * Opcode: 0x07
 * Description: Loads 4-byte packed string prefix immediate into destination register.
 *
 * Operands:
 *  - dst = inst.dst_reg, prefix = inst.payload
 *
 * Inputs:
 *  - inst.payload
 *
 * Outputs:
 *  - dst: TYPE_INT64
 *
 * Flags:
 *  - Unmodified
 *
 * Context Requirement:
 *  - Independent (requires_context = false)
 */
#ifndef OP_07_CONTRACT_DEFINED
#define OP_07_CONTRACT_DEFINED 1
#endif

// Opcode 0x07: OP_LOAD_CONST_STR_PREFIX
op_LOAD_CONST_STR_PREFIX: {
    const impulse_instruction_t& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    VALIDATE_REG(dst);
    vm_state->registers[dst] = inst.payload;
    vm_state->register_types[dst] = TYPE_INT64;
    vm_state->pc++;
    DISPATCH();
}

