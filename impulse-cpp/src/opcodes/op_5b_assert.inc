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
#define OP_5B_FLAG_ASSERT_VM_FLAGS IMP_FLAG_BIT0
#endif

// Opcode 0x5B: OP_ASSERT
op_ASSERT: {
    IMPULSE_ASSERT(vm_state != nullptr);
    const impulse_instruction_t& inst = bytecode[vm_state->pc];
    const uint16_t src_reg = IMP_INST_DST(inst);
    const uint32_t expected_val = IMP_INST_PAYLOAD_U32(inst);
    IMP_VALIDATE_GPR(src_reg);
    IMPULSE_ASSERT(src_reg < IMP_VM_GPR_COUNT);

    const bool check_reg_mode = (IMP_INST_FLAGS(inst) == IMP_FLAG_NONE);
    IMPULSE_TESTCASE(check_reg_mode);

    if (check_reg_mode) {
        IMPULSE_CHECK_REG_TYPE(vm_state, src_reg, OP_5B_ALLOWED_SRC_TYPES);
        const uint64_t actual_val = vm_state->registers[src_reg];
        const uint64_t sign_ext_val = static_cast<uint64_t>(static_cast<int64_t>(IMP_INST_PAYLOAD_I32(inst)));
        const bool exact_match = (actual_val == static_cast<uint64_t>(expected_val));
        const bool signed_match = (vm_state->register_types[src_reg] == TYPE_INT64 && actual_val == sign_ext_val);
        IMPULSE_TESTCASE(exact_match);
        IMPULSE_TESTCASE(!exact_match && signed_match);
        IMPULSE_TESTCASE(!exact_match && !signed_match);
        if (!exact_match && !signed_match) {
            return IMPULSE_VM_ERR_ASSERTION_FAILED;
        }
    } else {
        const bool flags_match = ((vm_state->flags & expected_val) == static_cast<uint64_t>(expected_val));
        IMPULSE_TESTCASE(flags_match);
        if (!flags_match) {
            return IMPULSE_VM_ERR_ASSERTION_FAILED;
        }
    }

    vm_state->pc++;
    DISPATCH();
}
