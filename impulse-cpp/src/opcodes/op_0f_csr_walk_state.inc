/**
 * @file op_0f_csr_walk_state.inc
 * @brief Formal Contract for OP_CSR_WALK_STATE (0x0F).
 *
 * Mnemonic: OP_CSR_WALK_STATE
 * Opcode: 0x0F
 * Description: Initializes frontier traversal tracking state register.
 *
 * Operands:
 *  - dst = inst.dst_reg, rel = payload & 0xFFFF
 *
 * Inputs:
 *  - None
 *
 * Outputs:
 *  - dst: TYPE_FRONTIER_STATE
 *
 * Flags:
 *  - Unmodified
 *
 * Context Requirement:
 *  - Independent
 */
#ifndef OP_0F_CONTRACT_DEFINED
#define OP_0F_CONTRACT_DEFINED 1
#endif

// Opcode 0x0F: OP_CSR_WALK_STATE
op_CSR_WALK_STATE: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    VALIDATE_REG(dst);
    if (inst.flags & 0x80) {
        if (vm_state->pc + 1 >= instruction_count) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        uint16_t rel = bytecode[vm_state->pc + 1].dst_reg;
        vm_state->pc++;
        if (!vm_state->query_context) return IMPULSE_VM_ERR_NULL_SNAPSHOT;
        if (rel >= vm_state->query_context->slots.size()) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
    vm_state->register_types[dst] = TYPE_FRONTIER_STATE;
    vm_state->registers[dst] = 0;
    vm_state->pc++;
    DISPATCH();
}

