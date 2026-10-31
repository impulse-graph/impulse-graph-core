// Opcode 0x07: OP_LOAD_CONST_STR_PREFIX
op_LOAD_CONST_STR_PREFIX: {
    const impulse_instruction_t& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    VALIDATE_REG(dst);
    vm_state->registers[dst] = inst.payload;
    vm_state->register_types[dst] = TYPE_INT64;
    vm_state->pc++;
    DISPATCH();
}

