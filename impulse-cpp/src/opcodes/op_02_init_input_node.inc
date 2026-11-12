/**
 * @file op_02_init_input_node.inc
 * @brief Formal Contract for OP_INIT_INPUT_NODE (0x02).
 *
 * Mnemonic: OP_INIT_INPUT_NODE
 * Opcode: 0x02
 * Description: Initializes destination register with scalar query input seed node ID or inline payload.
 *
 * Operands:
 *  - dst = inst.dst_reg, imm = inst.payload
 *
 * Inputs:
 *  - input_param or inst.payload
 *
 * Outputs:
 *  - dst: TYPE_NODE_ID
 *
 * Flags:
 *  - ZF: Cleared (0)
 *
 * Context Requirement:
 *  - Independent (requires_context = false)
 */
#ifndef OP_02_CONTRACT_DEFINED
#define OP_02_CONTRACT_DEFINED 1
#define OP_02_DEFINED_FLAGS IMP_FLAG_NONE
#endif

// Opcode 0x02: OP_INIT_INPUT_NODE
op_INIT_INPUT_NODE: {
    const auto& inst = bytecode[vm_state->pc];
    const uint16_t dst = IMP_INST_DST(inst);
    VALIDATE_REG(dst);
    IMPULSE_ASSERT(dst < IMP_VM_GPR_COUNT);
    const uint32_t imm_node = IMP_INST_PAYLOAD_U32(inst);
    IMPULSE_TESTCASE(imm_node != 0u);
    IMPULSE_TESTCASE(imm_node == 0u && input_param == 0u);
    vm_state->registers[dst] = (imm_node != 0u) ? imm_node : input_param;
    vm_state->register_types[dst] = TYPE_NODE_ID;
    IMP_VM_SET_ZF(vm_state, false);
    vm_state->pc++;
    DISPATCH();
}

