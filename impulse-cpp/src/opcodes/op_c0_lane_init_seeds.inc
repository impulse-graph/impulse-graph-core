// Opcode 0xC0: OP_LANE_INIT_SEEDS
op_LANE_INIT_SEEDS: {
    {
        const auto& inst = bytecode[vm_state->pc];
        uint16_t dst = inst.dst_reg;
        uint16_t src_reg = inst.payload & 0xFFFF;
        uint16_t p_u16_high = (inst.payload >> 16) & 0xFFFF;
        uint8_t flags = inst.flags;
        VALIDATE_REG(dst);
        VALIDATE_REG(src_reg);
        if (!vm_state->query_context) return IMPULSE_VM_ERR_NULL_SNAPSHOT;

        uint8_t src_type = vm_state->register_types[src_reg];
        uint64_t src_val = vm_state->registers[src_reg];

        std::array<uint64_t, 64> seeds{};
        size_t seeds_len = 0;
        uint32_t k_override = 0;
        bool has_k_override = false;

        if (src_type == TYPE_NULL) {
            uint32_t k = (p_u16_high > 0) ? std::min<uint32_t>(64u, p_u16_high) : 8u;
            has_k_override = true;
            k_override = k;
            if ((flags & 0x01) != 0) {
                for (uint32_t b = 0; b < k; ++b) seeds[seeds_len++] = b;
            } else if (k >= 1u) {
                seeds[seeds_len++] = 0ULL;
            }
        } else if (src_type == TYPE_NODE_ID || src_type == TYPE_INT64) {
            if ((flags & 0x01) != 0 && p_u16_high > 1) {
                uint32_t k = std::min<uint32_t>(64u, p_u16_high);
                for (uint32_t b = 0; b < k; ++b) seeds[seeds_len++] = src_val + b;
            } else {
                seeds[seeds_len++] = src_val;
            }
        } else if (src_type == TYPE_BITSET_HANDLE) {
            if (src_val >= VM_MAX_BITSET_HANDLES || !vm_state->query_context->bitset_allocated[src_val]) {
                return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
            }
            const auto& bs = vm_state->query_context->bitsets[src_val];
            for (size_t w = 0; w < bs.word_count && seeds_len < 64; ++w) {
                uint64_t word = bs.words[w];
                while (word != 0ULL && seeds_len < 64) {
                    uint32_t tz = static_cast<uint32_t>(std::countr_zero(word));
                    seeds[seeds_len++] = static_cast<uint64_t>(w * 64 + tz);
                    word &= (word - 1ULL);
                }
            }
        } else if (src_type == TYPE_NODE_VECTOR || src_type == TYPE_UINT64_VECTOR) {
            const uint64_t* ptr = nullptr;
            size_t len = 0;
            if (!get_node_vector_slice(vm_state->query_context, src_val, &ptr, &len)) {
                return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
            }
            size_t k = std::min<size_t>(64, len);
            for (size_t b = 0; b < k; ++b) {
                seeds[seeds_len++] = ptr[b];
            }
        } else {
            return IMPULSE_VM_ERR_INVALID_REGISTER;
        }

        uint32_t k = has_k_override ? k_override : std::min<uint32_t>(64u, static_cast<uint32_t>(seeds_len));
        size_t max_seed = 0;
        for (size_t i = 0; i < seeds_len; ++i) {
            uint64_t s = seeds[i];
            if (s < (1ULL << 20) && static_cast<size_t>(s) > max_seed) {
                max_seed = static_cast<size_t>(s);
            }
        }
        size_t n_nodes = resolve_multilane_node_count(vm_state->query_context, -1, max_seed + 1);
        int h_lf = acquire_lane_frontier(vm_state->query_context, n_nodes, k);
        if (h_lf < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;

        auto& lf = vm_state->query_context->lane_frontiers[h_lf];
        bool any_set = false;
        size_t bind_len = std::min<size_t>(k, seeds_len);
        for (size_t b = 0; b < bind_len; ++b) {
            uint64_t u = seeds[b];
            if (u < n_nodes) {
                lf.lanes[u] |= (1ULL << b);
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

