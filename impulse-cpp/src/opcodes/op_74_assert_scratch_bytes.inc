// Opcode 0x74: OP_ASSERT_SCRATCH_BYTES
op_ASSERT_SCRATCH_BYTES: {
    const impulse_instruction_t& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint32_t req_bytes = inst.payload;
    VALIDATE_REG(dst);

    uint64_t available_bytes = 512 * 1024 * 1024ULL; // 512 MB default scratch cap
    if (static_cast<uint64_t>(req_bytes) > available_bytes) {
        return IMPULSE_VM_ERR_ASSERTION_FAILED;
    }
    vm_state->registers[dst] = available_bytes;
    vm_state->register_types[dst] = TYPE_INT64;
    vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

    vm_state->pc++;
    DISPATCH();
}

