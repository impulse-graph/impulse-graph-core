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
#define OP_06_DEFINED_FLAGS IMP_FLAG_NONE
#endif

// Opcode 0x06: OP_LOAD_CONST_FLOAT
op_LOAD_CONST_FLOAT: {
    const auto& inst = bytecode[vm_state->pc];
    const uint16_t dst = IMP_INST_DST(inst);
    VALIDATE_REG(dst);
    IMPULSE_ASSERT(dst < IMP_VM_GPR_COUNT);
    const uint32_t raw_f32 = IMP_INST_PAYLOAD_U32(inst);
    IMPULSE_TESTCASE(raw_f32 == 0u);
    vm_state->registers[dst] = raw_f32;
    vm_state->register_types[dst] = TYPE_FLOAT;
    vm_state->pc++;
    DISPATCH();
}

