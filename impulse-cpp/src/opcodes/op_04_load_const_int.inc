/**
 * @file op_04_load_const_int.inc
 * @brief Formal Contract for OP_LOAD_CONST_INT (0x04).
 *
 * Mnemonic: OP_LOAD_CONST_INT
 * Opcode: 0x04
 * Description: Loads immediate 32-bit signed integer sign-extended to 64-bit into destination register.
 *
 * Operands:
 *  - dst = inst.dst_reg, imm = (int32_t)inst.payload
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
#ifndef OP_04_CONTRACT_DEFINED
#define OP_04_CONTRACT_DEFINED 1
#endif

// Opcode 0x04: OP_LOAD_CONST_INT
op_LOAD_CONST_INT: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    VALIDATE_REG(dst);
    int32_t imm = static_cast<int32_t>(inst.payload);
    vm_state->registers[dst] = static_cast<uint64_t>(static_cast<int64_t>(imm));
    vm_state->register_types[dst] = TYPE_INT64;
    vm_state->pc++;
    DISPATCH();
}

