// Opcode 0x8A: OP_FRONTIER_DIFF
op_FRONTIER_DIFF: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t r_curr = inst.payload & 0xFF;
    uint16_t r_prev = (inst.payload >> 8) & 0xFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(r_curr);
    VALIDATE_REG(r_prev);

    if (vm_state->register_types[dst] != TYPE_BITSET_HANDLE) {
        int h = acquire_bitset(vm_state->query_context);
        if (h < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        vm_state->registers[dst] = h;
        vm_state->register_types[dst] = TYPE_BITSET_HANDLE;
    }
    int h_dst = static_cast<int>(vm_state->registers[dst]);
    int h_curr = static_cast<int>(vm_state->registers[r_curr]);
    int h_prev = static_cast<int>(vm_state->registers[r_prev]);
    auto& bs_dst = vm_state->query_context->bitsets[h_dst];
    const auto& bs_curr = vm_state->query_context->bitsets[h_curr];
    const auto& bs_prev = vm_state->query_context->bitsets[h_prev];

    size_t words = vm_state->query_context->words_per_bitset;
    bool has_bits = false;
    for (size_t w = 0; w < words; ++w) {
        uint64_t d = bs_curr.words[w] & ~bs_prev.words[w];
        bs_dst.words[w] = d;
        if (d != 0) has_bits = true;
    }

    if (!has_bits) {
        vm_state->flags |= IMPULSE_VM_FLAG_ZF;
    } else {
        vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;
    }

    vm_state->pc++;
    DISPATCH();
}

