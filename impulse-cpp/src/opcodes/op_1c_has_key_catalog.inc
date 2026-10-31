/**
 * @file op_1c_has_key_catalog.inc
 * @brief Formal Contract for OP_HAS_KEY_CATALOG (0x1C).
 *
 * Mnemonic: OP_HAS_KEY_CATALOG
 * Opcode: 0x1C
 * Description: Checks whether domain key dictionary / catalog exists for given domain.
 *
 * Operands:
 *  - dst = inst.dst_reg, domain_id = payload & 0xFFFF
 *
 * Inputs:
 *  - domain_id in payload
 *
 * Outputs:
 *  - dst: TYPE_INT64 (1 if present, 0 if absent)
 *
 * Flags:
 *  - ZF: Set if absent, cleared if present
 *
 * Context Requirement:
 *  - Independent
 */
#ifndef OP_1C_CONTRACT_DEFINED
#define OP_1C_CONTRACT_DEFINED 1
#endif

// Opcode 0x1C: OP_HAS_KEY_CATALOG
op_HAS_KEY_CATALOG: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t domain_id = inst.payload & 0xFFFF;
    VALIDATE_REG(dst);

    bool present = true;
    if (vm_state->query_context && domain_id < vm_state->query_context->string_vectors.size()) {
        present = !vm_state->query_context->string_vectors[domain_id].empty();
    }

    vm_state->registers[dst] = present ? 1 : 0;
    vm_state->register_types[dst] = TYPE_INT64;

    if (!present) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
    else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

    vm_state->pc++;
    DISPATCH();
}

