/**
 * @file op_1a_has_csc.inc
 * @brief Formal Contract for OP_HAS_CSC (0x1A).
 *
 * Mnemonic: OP_HAS_CSC
 * Opcode: 0x1A
 * Description: Checks whether CSC inverted index topology exists for given relation.
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
#ifndef OP_1A_CONTRACT_DEFINED
#define OP_1A_CONTRACT_DEFINED 1
#define OP_1A_DEFINED_FLAGS IMP_FLAG_NONE
#endif

// Opcode 0x1A: OP_HAS_CSC
op_HAS_CSC: {
    const auto& inst = bytecode[vm_state->pc];
    const uint16_t dst = IMP_INST_DST(inst);
    const uint16_t rel = IMP_INST_LOW16(inst);
    VALIDATE_REG(dst);
    IMPULSE_ASSERT(dst < IMP_VM_GPR_COUNT);
    IMP_REQUIRE_CONTEXT(vm_state);
    IMPULSE_ASSERT(vm_state->query_context != nullptr);

    bool present = false;
    IMPULSE_TESTCASE(rel >= vm_state->query_context->slots.size());
    if (rel < vm_state->query_context->slots.size()) {
        const auto& slot = vm_state->query_context->slots[rel];
        present = (slot.csc_offsets_ptr != nullptr && slot.csc_targets_ptr != nullptr);
    }

    vm_state->registers[dst] = present ? 1u : 0u;
    vm_state->register_types[dst] = TYPE_INT64;
    IMP_VM_SET_ZF(vm_state, !present);

    vm_state->pc++;
    DISPATCH();
}

