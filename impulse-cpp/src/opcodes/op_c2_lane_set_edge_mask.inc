// Opcode 0xC2: OP_LANE_SET_EDGE_MASK
op_LANE_SET_EDGE_MASK: {
    {
        const auto& inst = bytecode[vm_state->pc];
        uint16_t dst = inst.dst_reg;
        uint16_t src_reg = inst.payload & 0xFF;
        uint16_t edge_reg = (inst.payload >> 8) & 0xFF;
        uint16_t rel_id = (inst.payload >> 16) & 0xFFFF;
        uint8_t flags = inst.flags;
        VALIDATE_REG(dst);
        VALIDATE_REG(src_reg);
        VALIDATE_REG(edge_reg);
        if (!vm_state->query_context) return IMPULSE_VM_ERR_NULL_SNAPSHOT;

        if (vm_state->register_types[src_reg] != TYPE_LANE_FRONTIER) {
            return IMPULSE_VM_ERR_INVALID_REGISTER;
        }
        size_t h_src = static_cast<size_t>(vm_state->registers[src_reg]);
        if (h_src >= VM_MAX_LANE_FRONTIER_HANDLES || h_src >= vm_state->query_context->lane_frontiers_count) {
            return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        }
        int h_dst = clone_lane_frontier(vm_state->query_context, h_src);
        if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        auto& lf_dst = vm_state->query_context->lane_frontiers[h_dst];

        size_t rel_slot = (rel_id < VM_MAX_REL_SLOTS) ? rel_id : 0;
        size_t n_edges = 64;
        if (rel_id < vm_state->query_context->slots.size() && vm_state->query_context->slots[rel_id].edge_count > 0) {
            n_edges = std::max<size_t>(n_edges, vm_state->query_context->slots[rel_id].edge_count);
        } else {
            n_edges = std::max<size_t>(n_edges, lf_dst.lanes.size() * 4);
        }
        auto& trip_arr = lf_dst.edge_trip_masks[rel_slot];
        if ((lf_dst.edge_trip_active_mask & (1u << rel_slot)) == 0) {
            trip_arr.clear();
            trip_arr.resize(n_edges, 0ULL);
            lf_dst.edge_trip_active_mask |= static_cast<uint16_t>(1u << rel_slot);
        } else if (trip_arr.size() < n_edges) {
            trip_arr.resize(n_edges, 0ULL);
        }

        uint8_t t_edge = vm_state->register_types[edge_reg];
        if (t_edge == TYPE_INT64 || t_edge == TYPE_NODE_ID) {
            uint64_t e_idx = vm_state->registers[edge_reg];
            uint32_t lane_b = flags & 0x3F;
            if (lane_b < 64u && e_idx < (1ULL << 20)) {
                if (e_idx >= trip_arr.size()) trip_arr.resize(static_cast<size_t>(e_idx) + 1, 0ULL);
                trip_arr[e_idx] |= (1ULL << lane_b) & lf_dst.active_mask;
            }
        } else if (t_edge == TYPE_NODE_VECTOR || t_edge == TYPE_UINT64_VECTOR) {
            const uint64_t* e_ptr = nullptr;
            size_t e_len = 0;
            if (!get_node_vector_slice(vm_state->query_context, vm_state->registers[edge_reg], &e_ptr, &e_len)) {
                return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
            }
            if ((flags & 0x01) != 0) {
                for (size_t i = 0; i < e_len; ++i) {
                    uint64_t packed = e_ptr[i];
                    uint64_t e_idx = packed & 0xFFFFFFFFULL;
                    uint32_t lane_b = static_cast<uint32_t>((packed >> 32) & 0x3FULL);
                    if (e_idx < (1ULL << 20)) {
                        if (e_idx >= trip_arr.size()) trip_arr.resize(static_cast<size_t>(e_idx) + 1, 0ULL);
                        trip_arr[e_idx] |= (1ULL << lane_b) & lf_dst.active_mask;
                    }
                }
            } else {
                size_t lim = std::min<size_t>(64, std::min<size_t>(lf_dst.num_lanes, e_len));
                for (size_t b = 0; b < lim; ++b) {
                    uint64_t e_idx = e_ptr[b];
                    if (e_idx != ~0ULL && e_idx < (1ULL << 20)) {
                        if (e_idx >= trip_arr.size()) trip_arr.resize(static_cast<size_t>(e_idx) + 1, 0ULL);
                        trip_arr[e_idx] |= (1ULL << b) & lf_dst.active_mask;
                    }
                }
            }
        } else if (t_edge != TYPE_NULL) {
            return IMPULSE_VM_ERR_INVALID_REGISTER;
        }

        vm_state->registers[dst] = static_cast<uint64_t>(h_dst);
        vm_state->register_types[dst] = TYPE_LANE_FRONTIER;
    }
    vm_state->pc++;
    DISPATCH();
}

