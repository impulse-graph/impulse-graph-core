// Opcode 0x92: OP_MAP_DENSE_TO_KEYS
op_MAP_DENSE_TO_KEYS: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint8_t src = inst.payload & 0xFF;
    uint16_t domain_id = (inst.payload >> 8) & 0xFFFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src);

    const BoundAttributeSlot* attr = find_key_attribute(vm_state, domain_id);

    int h_dst = acquire_string_vector(vm_state->query_context);
    if (h_dst < 0) {
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
    auto& svec = vm_state->query_context->string_vectors[h_dst];
    svec.clear();

    bool src_is_bitset = (vm_state->register_types[src] == TYPE_BITSET_HANDLE);
    uint8_t base_type = attr ? (attr->type_code & 0x7F) : 0;
    size_t max_nodes = vm_state->query_context->max_nodes;

    auto add_key = [&](uint64_t u) {
        if (attr && attr->data_ptr && base_type == 0x0B) {
            const char* str_ptr = nullptr;
            if (!attr->offsets_ptr) {
                str_ptr = static_cast<const char*>(attr->data_ptr) + u * attr->dimension;
            } else {
                const uint32_t* offsets = static_cast<const uint32_t*>(attr->offsets_ptr);
                str_ptr = static_cast<const char*>(attr->data_ptr) + offsets[u];
            }
            if (str_ptr) {
                svec.push_back(str_ptr);
            }
        } else {
            svec.push_back("mock_key");
        }
    };

    if (src_is_bitset) {
        int h_src = static_cast<int>(vm_state->registers[src]);
        const auto& bs_src = vm_state->query_context->bitsets[h_src];
        for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
            uint64_t w = bs_src.words[i];
            if (w == 0) continue;
            for (int b = 0; b < 64; ++b) {
                if (w & (1ULL << b)) {
                    uint64_t u = i * 64 + b;
                    if (u < max_nodes) add_key(u);
                }
            }
        }
    } else if (vm_state->register_types[src] == TYPE_LANE_FRONTIER) {
        size_t h_lf = static_cast<size_t>(vm_state->registers[src]);
        if (h_lf < VM_MAX_LANE_FRONTIER_HANDLES && h_lf < vm_state->query_context->lane_frontiers_count) {
            const auto& lf = vm_state->query_context->lane_frontiers[h_lf];
            for (size_t w_idx = 0; w_idx < lf.active_words.size(); ++w_idx) {
                uint64_t word = lf.active_words[w_idx];
                while (word != 0ULL) {
                    uint32_t bit = static_cast<uint32_t>(std::countr_zero(word));
                    size_t u = (w_idx << 6) + bit;
                    word &= (word - 1ULL);
                    if (u >= lf.lanes.size()) break;
                    if ((lf.lanes[u] & lf.active_mask) != 0ULL && u < max_nodes) {
                        add_key(static_cast<uint64_t>(u));
                    }
                }
            }
        }
    } else if (vm_state->register_types[src] == TYPE_NODE_VECTOR || vm_state->register_types[src] == TYPE_UINT64_VECTOR) {
        const uint64_t* ptr = nullptr;
        size_t len = 0;
        if (get_node_vector_slice(vm_state->query_context, vm_state->registers[src], &ptr, &len) && ptr) {
            for (size_t i = 0; i < len; ++i) {
                uint64_t u = ptr[i];
                if (u != ~0ULL && u < max_nodes) add_key(u);
            }
        }
    } else {
        uint64_t u = vm_state->registers[src];
        if (u < max_nodes) add_key(u);
    }

    vm_state->registers[dst] = h_dst;
    vm_state->register_types[dst] = TYPE_STRING_VECTOR;

    if (svec.empty()) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
    else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

    vm_state->pc++;
    DISPATCH();
}

