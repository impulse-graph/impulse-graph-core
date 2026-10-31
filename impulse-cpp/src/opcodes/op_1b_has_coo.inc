/**
 * @file op_1b_has_coo.inc
 * @brief Formal Contract for OP_HAS_COO (0x1B).
 *
 * Mnemonic: OP_HAS_COO
 * Opcode: 0x1B
 * Description: Checks whether coordinate (COO) edge-list stream exists for given relation.
 *
 * Operands:
 *  - dst = inst.dst_reg, rel = payload & 0xFFFF
 *
 * Inputs:
 *  - rel relation id in payload
 *
 * Outputs:
 *  - dst: TYPE_INT64 (1 if present, 0 if absent)
 *
 * Flags:
 *  - ZF: Set if absent, cleared if present
 *
 * Context Requirement:
 *  - Requires context (query_context snapshot slots)
 */
#ifndef OP_1B_CONTRACT_DEFINED
#define OP_1B_CONTRACT_DEFINED 1
#endif

// Opcode 0x1B: OP_HAS_COO
op_HAS_COO: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t rel = inst.payload & 0xFFFF;
    VALIDATE_REG(dst);
    if (!vm_state->query_context) return IMPULSE_VM_ERR_NULL_SNAPSHOT;

    bool present = false;
    if (rel < vm_state->query_context->slots.size()) {
        const auto& slot = vm_state->query_context->slots[rel];
        present = (slot.offsets_ptr != nullptr && slot.targets_ptr != nullptr);
    }

    vm_state->registers[dst] = present ? 1 : 0;
    vm_state->register_types[dst] = TYPE_INT64;

    if (!present) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
    else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

    vm_state->pc++;
    DISPATCH();
}

