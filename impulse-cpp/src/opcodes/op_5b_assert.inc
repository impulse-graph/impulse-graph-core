// Opcode 0x5B: OP_ASSERT
op_ASSERT: {
    const impulse_instruction_t& inst = bytecode[vm_state->pc];
    uint16_t src_reg = inst.dst_reg;
    uint32_t expected_val = inst.payload;
    VALIDATE_REG(src_reg);

    if (inst.flags == 0) {
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

