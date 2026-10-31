// Opcode 0x15: OP_NODE_FILTER_STR_PREFIX
op_NODE_FILTER_STR_PREFIX: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint8_t src = inst.payload & 0xFF;
    uint8_t val_reg = (inst.payload >> 8) & 0xFF;
    uint8_t attr_id = (inst.payload >> 16) & 0xFF;
    uint8_t rel_id = (inst.payload >> 24) & 0xFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src);
    VALIDATE_REG(val_reg);

    int h_dst = acquire_bitset(vm_state->query_context);
    if (h_dst < 0) {
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
    auto& bs_dst = vm_state->query_context->bitsets[h_dst];
    bs_dst.clear();

    bool src_is_bitset = (vm_state->register_types[src] == TYPE_BITSET_HANDLE);
    if (src_is_bitset) {
        int h_src = static_cast<int>(vm_state->registers[src]);
        bs_dst = vm_state->query_context->bitsets[h_src];
    } else {
        bitset_add(bs_dst, vm_state->registers[src], vm_state->query_context->max_nodes);
    }

    bool has_attr = (rel_id < vm_state->query_context->attribute_slots.size() &&
                     attr_id < vm_state->query_context->attribute_slots[rel_id].size() &&
                     vm_state->query_context->attribute_slots[rel_id][attr_id].data_ptr != nullptr);
    const char* prefix = reinterpret_cast<const char*>(vm_state->registers[val_reg]);

    if (has_attr && prefix != nullptr) {
        const auto& attr = vm_state->query_context->attribute_slots[rel_id][attr_id];
        size_t prefix_len = std::strlen(prefix);
        auto eval_match = [&](uint64_t u) -> bool {
            const char* str_ptr = nullptr;
            size_t str_len = 0;
            if (!attr.offsets_ptr) {
                str_ptr = static_cast<const char*>(attr.data_ptr) + u * attr.dimension;
                str_len = attr.dimension;
            } else {
                const uint32_t* offsets = static_cast<const uint32_t*>(attr.offsets_ptr);
                str_ptr = static_cast<const char*>(attr.data_ptr) + offsets[u];
                str_len = offsets[u + 1] - offsets[u];
            }
            if (!str_ptr) return false;
            for (size_t idx = 0; idx < prefix_len; ++idx) {
                if (idx >= str_len || str_ptr[idx] != prefix[idx]) return false;
            }
            return true;
        };

        for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
            uint64_t w = bs_dst.words[i];
            if (w == 0) continue;
            for (int b = 0; b < 64; ++b) {
                if (w & (1ULL << b)) {
                    uint64_t u = i * 64 + b;
                    if (!eval_match(u)) {
                        bs_dst.words[i] &= ~(1ULL << b);
                    }
                }
            }
        }
    }

    vm_state->registers[dst] = static_cast<uint64_t>(h_dst);
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

