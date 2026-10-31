// Opcode 0x5A: OP_THROW
op_THROW: {
    const impulse_instruction_t& inst = bytecode[vm_state->pc];
    vm_state->registers[0] = inst.payload;
    vm_state->register_types[0] = TYPE_INT64;
    return IMPULSE_VM_ERR_USER_THROW;
}

