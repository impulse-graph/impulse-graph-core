// Opcode 0x91: OP_COLLECT_ARRAY
op_COLLECT_ARRAY: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src = inst.payload & 0xFFFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src);

    auto& node_buf = vm_state->query_context->node_buffer;
    node_buf.clear();

    if (vm_state->register_types[src] == TYPE_BITSET_HANDLE) {
        int handle = static_cast<int>(vm_state->registers[src]);
        const auto& bs = vm_state->query_context->bitsets[handle];
        for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
            uint64_t w = bs.words[i];
            if (w == 0) continue;
            for (int b = 0; b < 64; ++b) {
                if (w & (1ULL << b)) {
                    uint64_t node_id = i * 64 + b;
                    node_buf.push_back(node_id);
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
                    if ((lf.lanes[u] & lf.active_mask) != 0ULL) {
                        node_buf.push_back(static_cast<uint64_t>(u));
                    }
                }
            }
        }
    } else if (vm_state->register_types[src] == TYPE_NODE_VECTOR || vm_state->register_types[src] == TYPE_UINT64_VECTOR) {
        const uint64_t* ptr = nullptr;
        size_t len = 0;
        if (get_node_vector_slice(vm_state->query_context, vm_state->registers[src], &ptr, &len) && ptr) {
            if (ptr != node_buf.data()) {
                node_buf.assign(ptr, ptr + len);
            }
        }
    } else if (vm_state->register_types[src] == TYPE_NODE_ID || vm_state->register_types[src] == TYPE_INT64) {
        node_buf.push_back(vm_state->registers[src]);
    }

    vm_state->registers[dst] = reinterpret_cast<uint64_t>(node_buf.data());
    vm_state->register_types[dst] = TYPE_NODE_VECTOR;

    if (node_buf.empty()) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
    else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

    vm_state->pc++;
    DISPATCH();
}

