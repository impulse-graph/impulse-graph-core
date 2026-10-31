// Opcode 0xC1: OP_LANE_INIT_GROUPS
op_LANE_INIT_GROUPS: {
    {
        const auto& inst = bytecode[vm_state->pc];
        uint16_t dst = inst.dst_reg;
        uint16_t nodes_reg = inst.payload & 0xFF;
        uint16_t groups_reg = (inst.payload >> 8) & 0xFF;
        uint16_t p_u16_high = (inst.payload >> 16) & 0xFFFF;
        uint8_t flags = inst.flags;
        VALIDATE_REG(dst);
        VALIDATE_REG(nodes_reg);
        VALIDATE_REG(groups_reg);
        if (!vm_state->query_context) return IMPULSE_VM_ERR_NULL_SNAPSHOT;

        uint8_t t_nodes = vm_state->register_types[nodes_reg];
        uint8_t t_groups = vm_state->register_types[groups_reg];

        auto& pair_nodes = vm_state->query_context->scratch_u64_a;
        auto& pair_grps = vm_state->query_context->scratch_u32_a;
        pair_nodes.clear();
        pair_grps.clear();

        if (t_nodes == TYPE_NULL) {
            uint32_t k = (p_u16_high > 0) ? std::min<uint32_t>(64u, p_u16_high) : 8u;
            if ((flags & 0x01) != 0) {
                for (uint32_t b = 0; b < k; ++b) {
                    pair_nodes.push_back(b);
                    pair_grps.push_back(b);
                    pair_nodes.push_back(b + k);
                    pair_grps.push_back(b);
                }
            } else {
                pair_nodes.push_back(0ULL);
                pair_grps.push_back(0u);
            }
        } else if (t_nodes == TYPE_NODE_VECTOR || t_nodes == TYPE_UINT64_VECTOR) {
            const uint64_t* n_ptr = nullptr;
            size_t n_len = 0;
            if (!get_node_vector_slice(vm_state->query_context, vm_state->registers[nodes_reg], &n_ptr, &n_len)) {
                return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
            }
            if (nodes_reg != groups_reg && groups_reg > 0 && (t_groups == TYPE_NODE_VECTOR || t_groups == TYPE_UINT64_VECTOR)) {
                const uint64_t* g_ptr = nullptr;
                size_t g_len = 0;
                if (!get_node_vector_slice(vm_state->query_context, vm_state->registers[groups_reg], &g_ptr, &g_len)) {
                    return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                }
                size_t len = std::min(n_len, g_len);
                for (size_t i = 0; i < len; ++i) {
                    pair_nodes.push_back(n_ptr[i]);
                    pair_grps.push_back(static_cast<uint32_t>(g_ptr[i]));
                }
            } else {
                bool any_hi = ((flags & 0x01) != 0);
                for (size_t i = 0; i < n_len && !any_hi; ++i) {
                    if ((n_ptr[i] >> 32) != 0ULL) any_hi = true;
                }
                for (size_t i = 0; i < n_len; ++i) {
                    uint64_t packed = n_ptr[i];
                    uint64_t u = packed & 0xFFFFFFFFULL;
                    uint32_t hi = static_cast<uint32_t>((packed >> 32) & 0xFFFFFFFFULL);
                    uint32_t grp = any_hi ? hi : std::min<uint32_t>(63u, static_cast<uint32_t>(i));
                    pair_nodes.push_back(u);
                    pair_grps.push_back(grp);
                }
            }
        } else {
            return IMPULSE_VM_ERR_INVALID_REGISTER;
        }

        size_t max_node = (t_nodes == TYPE_NULL && flags == 0) ? 63 : 0;
        int max_grp = -1;
        for (size_t i = 0; i < pair_nodes.size(); ++i) {
            if (pair_nodes[i] < (1ULL << 20) && static_cast<size_t>(pair_nodes[i]) > max_node) {
                max_node = static_cast<size_t>(pair_nodes[i]);
            }
            if (pair_grps[i] < 64u && static_cast<int>(pair_grps[i]) > max_grp) {
                max_grp = static_cast<int>(pair_grps[i]);
            }
        }
        uint32_t num_lanes = (p_u16_high > 0)
            ? std::min<uint32_t>(64u, p_u16_high)
            : std::min<uint32_t>(64u, static_cast<uint32_t>(std::max(0, max_grp + 1)));
        size_t n_nodes = resolve_multilane_node_count(vm_state->query_context, -1, max_node + 1);
        int h_lf = acquire_lane_frontier(vm_state->query_context, n_nodes, num_lanes);
        if (h_lf < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;

        auto& lf = vm_state->query_context->lane_frontiers[h_lf];
        bool any_set = false;
        for (size_t i = 0; i < pair_nodes.size(); ++i) {
            if (pair_nodes[i] < n_nodes && pair_grps[i] < num_lanes) {
                size_t u = static_cast<size_t>(pair_nodes[i]);
                lf.lanes[u] |= (1ULL << pair_grps[i]);
                lf.active_words[u >> 6] |= (1ULL << (u & 63));
                any_set = true;
            }
        }

        vm_state->registers[dst] = static_cast<uint64_t>(h_lf);
        vm_state->register_types[dst] = TYPE_LANE_FRONTIER;
        if (any_set) vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;
        else vm_state->flags |= IMPULSE_VM_FLAG_ZF;
    }
    vm_state->pc++;
    DISPATCH();
}

