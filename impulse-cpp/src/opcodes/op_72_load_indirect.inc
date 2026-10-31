// Opcode 0x72: OP_LOAD_INDIRECT
op_LOAD_INDIRECT: {
    const impulse_instruction_t& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src_param = inst.payload & 0xFFFF;
    uint16_t index_reg = (inst.payload >> 16) & 0xFFFF;
    VALIDATE_REG(dst);

    if (inst.flags == 0) {
        VALIDATE_REG(src_param);
        uint64_t target_reg_idx = vm_state->registers[src_param];
        VALIDATE_REG(target_reg_idx);
        vm_state->registers[dst] = vm_state->registers[target_reg_idx];
        vm_state->register_types[dst] = vm_state->register_types[target_reg_idx];
    } else {
        VALIDATE_REG(src_param);
        VALIDATE_REG(index_reg);
        int h_vec = static_cast<int>(vm_state->registers[src_param]);
        uint64_t idx = vm_state->registers[index_reg];
        if (h_vec < 0 || h_vec >= static_cast<int>(vm_state->query_context->float_vectors.size())) {
            return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        }
        const auto& vec = vm_state->query_context->float_vectors[h_vec];
        if (idx >= vec.size()) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        float val = vec[idx];
        vm_state->registers[dst] = *reinterpret_cast<uint32_t*>(&val);
        vm_state->register_types[dst] = TYPE_FLOAT;
    }

    vm_state->pc++;
    DISPATCH();
}

