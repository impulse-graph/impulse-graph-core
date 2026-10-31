// Opcode 0xC9: OP_LANE_SET_COVER
op_LANE_SET_COVER: {
    {
        const auto& inst = bytecode[vm_state->pc];
        uint16_t dst = inst.dst_reg;
        uint16_t src_reg = inst.payload & 0xFF;
        uint16_t mask_reg = (inst.payload >> 8) & 0xFF;
        uint16_t max_pick = (inst.payload >> 16) & 0xFFFF;
        uint8_t flags = inst.flags;
        VALIDATE_REG(dst);
        VALIDATE_REG(src_reg);
        if (!vm_state->query_context) return IMPULSE_VM_ERR_NULL_SNAPSHOT;

        if (vm_state->register_types[src_reg] != TYPE_LANE_FRONTIER) {
            return IMPULSE_VM_ERR_INVALID_REGISTER;
        }
        size_t h_src = static_cast<size_t>(vm_state->registers[src_reg]);
        if (h_src >= VM_MAX_LANE_FRONTIER_HANDLES || h_src >= vm_state->query_context->lane_frontiers_count) {
            return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        }
        const auto& lf = vm_state->query_context->lane_frontiers[h_src];

        uint64_t target_mask = (mask_reg != 0 && mask_reg < 64 &&
            (vm_state->register_types[mask_reg] == TYPE_INT64 || vm_state->register_types[mask_reg] == TYPE_NODE_ID))
            ? (vm_state->registers[mask_reg] & lf.active_mask)
            : lf.active_mask;

        int h_out = acquire_node_vector(vm_state->query_context);
        if (h_out < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        auto& out_vec = vm_state->query_context->node_vectors[h_out];
        size_t selected_len = 0;

        size_t limit = (max_pick > 0) ? max_pick : 64;
        size_t n_nodes = lf.lanes.size();
        uint64_t uncovered = target_mask;

        if ((flags & 0x01) != 0) {
            uncovered = 0ULL;
            for (size_t u = 0; u < n_nodes; ++u) {
                uint64_t wu = lf.lanes[u] & target_mask;
                for (size_t v = u + 1; v < n_nodes; ++v) {
                    uint64_t wv = lf.lanes[v] & target_mask;
                    if ((wu | wv) == target_mask && target_mask != 0ULL && selected_len + 2 <= out_vec.size()) {
                        out_vec[selected_len++] = u;
                        out_vec[selected_len++] = v;
                    }
                }
            }
        } else {
            size_t steps = 0;
            while (uncovered != 0ULL && steps < limit && selected_len < out_vec.size()) {
                int64_t best_u = -1;
                uint32_t best_gain = 0;
                for (size_t u = 0; u < n_nodes; ++u) {
                    uint32_t gain = static_cast<uint32_t>(std::popcount(lf.lanes[u] & uncovered));
                    if (gain > best_gain) {
                        best_gain = gain;
                        best_u = static_cast<int64_t>(u);
                    }
                }
                if (best_u < 0 || best_gain == 0) break;
                out_vec[selected_len++] = static_cast<uint64_t>(best_u);
                uncovered &= ~lf.lanes[static_cast<size_t>(best_u)];
                steps++;
            }
        }

        vm_state->query_context->node_vector_lengths[h_out] = selected_len;
        vm_state->query_context->node_vector_length_explicit[h_out] = true;

        vm_state->registers[dst] = static_cast<uint64_t>(h_out);
        vm_state->register_types[dst] = TYPE_NODE_VECTOR;
        bool is_zf = ((flags & 0x01) != 0) ? (selected_len == 0) : (uncovered != 0ULL || selected_len == 0);
        if (is_zf) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
        else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;
    }
    vm_state->pc++;
    DISPATCH();
}

