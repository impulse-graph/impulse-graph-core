// Opcode 0x06: OP_LOAD_CONST_FLOAT
op_LOAD_CONST_FLOAT: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    VALIDATE_REG(dst);
    vm_state->registers[dst] = inst.payload;
    vm_state->register_types[dst] = TYPE_FLOAT;
    vm_state->pc++;
    DISPATCH();
}

