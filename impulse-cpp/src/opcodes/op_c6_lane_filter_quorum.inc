// Opcode 0xC6: OP_LANE_FILTER_QUORUM
op_LANE_FILTER_QUORUM: {
    {
        const auto& inst = bytecode[vm_state->pc];
        uint16_t dst = inst.dst_reg;
        uint8_t flags = inst.flags;
        uint16_t src_reg = (flags & 0x01) ? (inst.payload & 0xFF) : (inst.payload & 0xFFFF);
        uint32_t min_k = (flags & 0x01)
            ? ((inst.payload >> 8) & 0xFF)
            : (((inst.payload >> 16) & 0xFFFF) > 0 ? ((inst.payload >> 16) & 0xFFFF) : 1u);
        uint16_t req_reg = (inst.payload >> 16) & 0xFF;
        uint16_t forbid_reg = (inst.payload >> 24) & 0xFF;
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

        uint64_t req_mask = ((flags & 0x01) != 0 && req_reg > 0 && req_reg < 64 &&
            (vm_state->register_types[req_reg] == TYPE_INT64 || vm_state->register_types[req_reg] == TYPE_NODE_ID))
            ? (vm_state->registers[req_reg] & lf.active_mask)
            : lf.active_mask;
        uint64_t forbid_mask = ((flags & 0x01) != 0 && forbid_reg > 0 && forbid_reg < 64 &&
            (vm_state->register_types[forbid_reg] == TYPE_INT64 || vm_state->register_types[forbid_reg] == TYPE_NODE_ID))
            ? (vm_state->registers[forbid_reg] & lf.active_mask)
            : 0ULL;

        int h_bs = acquire_bitset(vm_state->query_context);
        if (h_bs < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        auto& bs = vm_state->query_context->bitsets[h_bs];
        bs.clear();

        bool any_out = false;
        if (min_k > 0) {
            for (size_t w_idx = 0; w_idx < lf.active_words.size(); ++w_idx) {
                uint64_t word = lf.active_words[w_idx];
                while (word != 0ULL) {
                    uint32_t bit = static_cast<uint32_t>(std::countr_zero(word));
                    size_t u = (w_idx << 6) + bit;
                    word &= (word - 1ULL);
                    if (u >= lf.lanes.size()) break;
                    uint64_t w = lf.lanes[u] & lf.active_mask;
                    uint32_t pc_bits = static_cast<uint32_t>(std::popcount(w & req_mask));
                    if (pc_bits >= min_k && (w & forbid_mask) == 0ULL) {
                        bs.set(u);
                        any_out = true;
                    }
                }
            }
        } else {
            for (size_t u = 0; u < lf.lanes.size(); ++u) {
                uint64_t w = lf.lanes[u] & lf.active_mask;
                uint32_t pc_bits = static_cast<uint32_t>(std::popcount(w & req_mask));
                if (pc_bits >= min_k && (w & forbid_mask) == 0ULL) {
                    bs.set(u);
                    any_out = true;
                }
            }
        }

        vm_state->registers[dst] = static_cast<uint64_t>(h_bs);
        vm_state->register_types[dst] = TYPE_BITSET_HANDLE;
        if (any_out) vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;
        else vm_state->flags |= IMPULSE_VM_FLAG_ZF;
    }
    vm_state->pc++;
    DISPATCH();
}

