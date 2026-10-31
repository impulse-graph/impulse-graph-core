// Opcode 0x33: OP_SET_CARDINALITY
op_SET_CARDINALITY: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src = inst.payload & 0xFFFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src);

    uint64_t count = 0;
    uint8_t stype = vm_state->register_types[src];
    if (stype == TYPE_BITSET_HANDLE) {
        int handle = static_cast<int>(vm_state->registers[src]);
        const auto& bs = vm_state->query_context->bitsets[handle];
        for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
            count += std::popcount(bs.words[i]);
        }
    } else if (stype == TYPE_NODE_ID || stype == TYPE_INT64) {
        count = 1;
    } else if (stype == TYPE_NODE_VECTOR || stype == TYPE_UINT64_VECTOR) {
        const uint64_t* ptr = nullptr;
        size_t len = 0;
        if (get_node_vector_slice(vm_state->query_context, vm_state->registers[src], &ptr, &len)) {
            auto& tmp_bs = vm_state->query_context->private_bitsets[0];
            tmp_bs.clear();
            bool has_oob = false;
            for (size_t i = 0; i < len; ++i) {
                uint64_t u = ptr[i];
                if (u < tmp_bs.word_count * 64) {
                    bitset_add(tmp_bs, u, vm_state->query_context->max_nodes);
                } else {
                    has_oob = true;
                }
            }
            for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                count += std::popcount(tmp_bs.words[i]);
            }
            if (has_oob) count++;
        }
    }

    vm_state->registers[dst] = count;
    vm_state->register_types[dst] = TYPE_INT64;

    if (count == 0) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
    else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

    vm_state->pc++;
    DISPATCH();
}

