// Opcode 0x12: OP_CSR_DEGREE
op_CSR_DEGREE: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src = inst.payload & 0xFFFF;
    uint16_t rel;
    if (inst.flags & 0x80) {
        if (vm_state->pc + 1 >= instruction_count) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        rel = bytecode[vm_state->pc + 1].dst_reg;
        vm_state->pc++;
    } else {
        rel = (inst.payload >> 16) & 0xFFFF;
    }
    VALIDATE_REG(dst);
    VALIDATE_REG(src);

    if (rel >= vm_state->query_context->slots.size()) {
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
    const auto& slot = vm_state->query_context->slots[rel];

    uint64_t degree = 0;
    uint8_t stype = vm_state->register_types[src];
    if (stype == TYPE_NODE_ID || stype == TYPE_INT64) {
        uint64_t u = vm_state->registers[src];
        if (slot.offsets_ptr && u < slot.node_count) {
            degree = slot.get_csr_offset(u + 1) - slot.get_csr_offset(u);
        }
    } else if (stype == TYPE_BITSET_HANDLE && slot.offsets_ptr) {
        int h_src = static_cast<int>(vm_state->registers[src]);
        const auto& bs_src = vm_state->query_context->bitsets[h_src];
        for (size_t w = 0; w < vm_state->query_context->words_per_bitset; ++w) {
            uint64_t word = bs_src.words[w];
            while (word) {
                int bit = std::countr_zero(word);
                uint64_t u = w * 64 + bit;
                word &= word - 1;
                if (u < slot.node_count) {
                    degree += slot.get_csr_offset(u + 1) - slot.get_csr_offset(u);
                }
            }
        }
    } else if ((stype == TYPE_NODE_VECTOR || stype == TYPE_UINT64_VECTOR) && slot.offsets_ptr) {
        const uint64_t* vec_ptr = nullptr;
        size_t vec_len = 0;
        if (get_node_vector_slice(vm_state->query_context, vm_state->registers[src], &vec_ptr, &vec_len)) {
            for (size_t k = 0; k < vec_len; ++k) {
                uint64_t u = vec_ptr[k];
                if (u < slot.node_count) {
                    degree += slot.get_csr_offset(u + 1) - slot.get_csr_offset(u);
                }
            }
        }
    }

    vm_state->registers[dst] = degree;
    vm_state->register_types[dst] = TYPE_INT64;

    if (degree == 0) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
    else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

    vm_state->pc++;
    DISPATCH();
}

