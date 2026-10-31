// Opcode 0x02: OP_INIT_INPUT_NODE
op_INIT_INPUT_NODE: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    VALIDATE_REG(dst);
    vm_state->registers[dst] = (inst.payload != 0) ? inst.payload : input_param;
    vm_state->register_types[dst] = TYPE_NODE_ID;
    vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;
    vm_state->pc++;
    DISPATCH();
}

