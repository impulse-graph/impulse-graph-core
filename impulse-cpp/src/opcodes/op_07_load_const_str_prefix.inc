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
#define OP_07_DEFINED_FLAGS IMP_FLAG_NONE
#endif

// Opcode 0x07: OP_LOAD_CONST_STR_PREFIX
op_LOAD_CONST_STR_PREFIX: {
    const impulse_instruction_t& inst = bytecode[vm_state->pc];
    const uint16_t dst = IMP_INST_DST(inst);
    VALIDATE_REG(dst);
    IMPULSE_ASSERT(dst < IMP_VM_GPR_COUNT);
    const uint32_t prefix_bits = IMP_INST_PAYLOAD_U32(inst);
    IMPULSE_TESTCASE(prefix_bits == 0u);
    vm_state->registers[dst] = prefix_bits;
    vm_state->register_types[dst] = TYPE_INT64;
    vm_state->pc++;
    DISPATCH();
}

