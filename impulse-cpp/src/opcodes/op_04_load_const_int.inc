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
#define OP_04_DEFINED_FLAGS IMP_FLAG_NONE
#endif

// Opcode 0x04: OP_LOAD_CONST_INT
op_LOAD_CONST_INT: {
    const auto& inst = bytecode[vm_state->pc];
    const uint16_t dst = IMP_INST_DST(inst);
    VALIDATE_REG(dst);
    IMPULSE_ASSERT(dst < IMP_VM_GPR_COUNT);
    const int32_t imm = IMP_INST_PAYLOAD_I32(inst);
    IMPULSE_TESTCASE(imm == 0);
    IMPULSE_TESTCASE(imm < 0);
    IMPULSE_TESTCASE(imm == INT32_MIN || imm == INT32_MAX);
    vm_state->registers[dst] = static_cast<uint64_t>(static_cast<int64_t>(imm));
    vm_state->register_types[dst] = TYPE_INT64;
    vm_state->pc++;
    DISPATCH();
}

