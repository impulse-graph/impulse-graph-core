// Opcode 0x73: OP_ALLOC_SCRATCH
op_ALLOC_SCRATCH: {
    const impulse_instruction_t& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint32_t req_bytes = inst.payload;
    VALIDATE_REG(dst);

    uint64_t aligned = (static_cast<uint64_t>(req_bytes) + 63ULL) & ~63ULL;
    vm_state->registers[dst] = aligned;
    vm_state->register_types[dst] = TYPE_INT64;
    vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

    vm_state->pc++;
    DISPATCH();
}

