// Opcode 0x04: OP_LOAD_CONST_INT
op_LOAD_CONST_INT: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    VALIDATE_REG(dst);
    int32_t imm = static_cast<int32_t>(inst.payload);
    vm_state->registers[dst] = static_cast<uint64_t>(static_cast<int64_t>(imm));
    vm_state->register_types[dst] = TYPE_INT64;
    vm_state->pc++;
    DISPATCH();
}

