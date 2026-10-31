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
#endif

// Opcode 0x02: OP_INIT_INPUT_NODE
op_INIT_INPUT_NODE: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    VALIDATE_REG(dst);
    vm_state->registers[dst] = (inst.payload != 0) ? inst.payload : input_param;
    vm_state->register_types[dst] = TYPE_NODE_ID;
    vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;
    vm_state->pc++;
    DISPATCH();
}

