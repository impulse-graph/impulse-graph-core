/**
 * @file op_06_load_const_float.inc
 * @brief Formal Contract for OP_LOAD_CONST_FLOAT (0x06).
 *
 * Mnemonic: OP_LOAD_CONST_FLOAT
 * Opcode: 0x06
 * Description: Loads 32-bit IEEE-754 floating point constant from payload into destination register.
 *
 * Operands:
 *  - dst = inst.dst_reg, imm_f32 = inst.payload
 *
 * Inputs:
 *  - inst.payload
 *
 * Outputs:
 *  - dst: TYPE_FLOAT
 *
 * Flags:
 *  - Unmodified
 *
 * Context Requirement:
 *  - Independent (requires_context = false)
 */
#ifndef OP_06_CONTRACT_DEFINED
#define OP_06_CONTRACT_DEFINED 1
#endif

// Opcode 0x06: OP_LOAD_CONST_FLOAT
op_LOAD_CONST_FLOAT: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    VALIDATE_REG(dst);
    vm_state->registers[dst] = inst.payload;
    vm_state->register_types[dst] = TYPE_FLOAT;
    vm_state->pc++;
    DISPATCH();
}

