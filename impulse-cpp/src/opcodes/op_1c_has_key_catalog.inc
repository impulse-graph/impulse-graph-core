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
#define OP_1C_DEFINED_FLAGS IMP_FLAG_NONE
#endif

// Opcode 0x1C: OP_HAS_KEY_CATALOG
op_HAS_KEY_CATALOG: {
    const auto& inst = bytecode[vm_state->pc];
    const uint16_t dst = IMP_INST_DST(inst);
    const uint16_t domain_id = IMP_INST_LOW16(inst);
    VALIDATE_REG(dst);
    IMPULSE_ASSERT(dst < IMP_VM_GPR_COUNT);

    bool present = true;
    IMPULSE_TESTCASE(vm_state->query_context == nullptr);
    if (vm_state->query_context && domain_id < vm_state->query_context->string_vectors.size()) {
        present = !vm_state->query_context->string_vectors[domain_id].empty();
    }

    vm_state->registers[dst] = present ? 1u : 0u;
    vm_state->register_types[dst] = TYPE_INT64;
    IMP_VM_SET_ZF(vm_state, !present);

    vm_state->pc++;
    DISPATCH();
}

