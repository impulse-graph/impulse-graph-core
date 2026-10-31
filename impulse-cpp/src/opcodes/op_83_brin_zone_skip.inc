// Opcode 0x83: OP_BRIN_ZONE_SKIP
op_BRIN_ZONE_SKIP: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src = inst.payload & 0xFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src);
    vm_state->registers[dst] = 1;
    vm_state->register_types[dst] = TYPE_INT64;
    vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;
    vm_state->pc++;
    DISPATCH();
}

