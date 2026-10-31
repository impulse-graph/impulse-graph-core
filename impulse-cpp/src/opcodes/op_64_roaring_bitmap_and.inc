// Opcode 0x64: OP_ROARING_BITMAP_AND
op_ROARING_BITMAP_AND: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src1 = inst.payload & 0xFFFF;
    uint16_t src2 = (inst.payload >> 16) & 0xFFFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src1);
    VALIDATE_REG(src2);

    int h_dst = acquire_bitset(vm_state->query_context);
    if (h_dst < 0) {
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
    auto& bs_dst = vm_state->query_context->bitsets[h_dst];
    bs_dst.clear();

    if (vm_state->register_types[src1] == TYPE_BITSET_HANDLE && vm_state->register_types[src2] == TYPE_BITSET_HANDLE) {
        int h1 = static_cast<int>(vm_state->registers[src1]);
        int h2 = static_cast<int>(vm_state->registers[src2]);
        const auto& bs1 = vm_state->query_context->bitsets[h1];
        const auto& bs2 = vm_state->query_context->bitsets[h2];

        for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
            bs_dst.words[i] = bs1.words[i] & bs2.words[i];
        }
    }

    vm_state->registers[dst] = h_dst;
    vm_state->register_types[dst] = TYPE_BITSET_HANDLE;
    bool is_empty = true;
    for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
        if (bs_dst.words[i] != 0) { is_empty = false; break; }
    }
    if (is_empty) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
    else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

    vm_state->pc++;
    DISPATCH();
}

