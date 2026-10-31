/**
 * Formal Opcode Contract: OP_ASSERT (0x5B)
 *
 * Layout:
 *   dst_reg:          inst.dst_reg (DST_REG, tested register)
 *   payload:          expected 32-bit value
 *   flags:            0x00: assert registers[dst_reg] == payload
 *                     0x01: assert (vm_state->flags & payload) == payload
 *   allowed_src:      IMP_TYPES_ANY
 *   requires_ctx:     false
 */
#ifndef OP_5B_CONTRACT_DEFINED
#define OP_5B_CONTRACT_DEFINED
#define OP_5B_DEFINED_FLAGS        0x01u
#define OP_5B_ALLOWED_SRC_TYPES    (IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_FLOAT) | IMP_TYPE_BIT(TYPE_BOOLEAN))
#define OP_5B_OUTPUT_DST_TYPES     IMP_TYPES_NONE
#endif

// Opcode 0x5B: OP_ASSERT
op_ASSERT: {
    const impulse_instruction_t& inst = bytecode[vm_state->pc];
    uint16_t src_reg = inst.dst_reg;
    uint32_t expected_val = inst.payload;
    VALIDATE_REG(src_reg);

    if (inst.flags == 0) {
        IMPULSE_CHECK_REG_TYPE(vm_state, src_reg, OP_5B_ALLOWED_SRC_TYPES);
        uint64_t actual_val = vm_state->registers[src_reg];
        uint64_t sign_ext_val = static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(expected_val)));
        if (actual_val != static_cast<uint64_t>(expected_val) &&
            !(vm_state->register_types[src_reg] == TYPE_INT64 && actual_val == sign_ext_val)) {
            return IMPULSE_VM_ERR_ASSERTION_FAILED;
        }
    } else {
        if ((vm_state->flags & expected_val) != static_cast<uint64_t>(expected_val)) {
            return IMPULSE_VM_ERR_ASSERTION_FAILED;
        }
    }

    vm_state->pc++;
    DISPATCH();
}

