/**
 * Formal Opcode Contract: OP_SET_DIFFERENCE (0x32)
 *
 * Layout:
 *   dst_reg:          inst.dst_reg (DST_REG)
 *   src_reg:          inst.payload & 0xFFFF (PAYLOAD_LOW16)
 *   aux1_reg:         (inst.payload >> 16) & 0xFFFF (PAYLOAD_HIGH16)
 *   allowed_src:      IMP_TYPES_SOURCE_SET | TYPE_LANE_FRONTIER
 *   allowed_aux1:     IMP_TYPES_SOURCE_SET | TYPE_LANE_FRONTIER
 *   output_dst:       TYPE_BITSET_HANDLE | TYPE_LANE_FRONTIER
 *   flags:            ZF set if empty
 *   requires_ctx:     true
 */
#ifndef OP_32_CONTRACT_DEFINED
#define OP_32_CONTRACT_DEFINED
#define OP_32_DEFINED_FLAGS        0x00u
#define OP_32_ALLOWED_SRC_TYPES    (IMP_TYPES_SOURCE_SET | IMP_TYPE_BIT(TYPE_LANE_FRONTIER) | IMP_TYPE_BIT(TYPE_NULL) | IMP_TYPES_FLOAT_OR_DBL | IMP_TYPE_BIT(TYPE_FLOAT) | IMP_TYPE_BIT(TYPE_DOUBLE))
#define OP_32_ALLOWED_AUX1_TYPES   (IMP_TYPES_SOURCE_SET | IMP_TYPE_BIT(TYPE_LANE_FRONTIER) | IMP_TYPE_BIT(TYPE_NULL) | IMP_TYPES_FLOAT_OR_DBL | IMP_TYPE_BIT(TYPE_FLOAT) | IMP_TYPE_BIT(TYPE_DOUBLE))
#define OP_32_OUTPUT_DST_TYPES     (IMP_TYPE_BIT(TYPE_BITSET_HANDLE) | IMP_TYPE_BIT(TYPE_LANE_FRONTIER))
#endif

// Opcode 0x32: OP_SET_DIFFERENCE
op_SET_DIFFERENCE: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src1 = inst.payload & 0xFFFF;
    uint16_t src2 = (inst.payload >> 16) & 0xFFFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src1);
    VALIDATE_REG(src2);
    if (!vm_state->query_context) return IMPULSE_VM_ERR_NULL_SNAPSHOT;
    IMPULSE_CHECK_REG_TYPE(vm_state, src1, OP_32_ALLOWED_SRC_TYPES);
    IMPULSE_CHECK_REG_TYPE(vm_state, src2, OP_32_ALLOWED_AUX1_TYPES);

    uint8_t type1 = vm_state->register_types[src1];
    uint64_t val1 = vm_state->registers[src1];
    uint8_t type2 = vm_state->register_types[src2];
    uint64_t val2 = vm_state->registers[src2];
    if (type1 == TYPE_LANE_FRONTIER && type2 == TYPE_LANE_FRONTIER) {
        if (!vm_state->query_context) return IMPULSE_VM_ERR_NULL_SNAPSHOT;
        size_t h1 = static_cast<size_t>(val1);
        size_t h2 = static_cast<size_t>(val2);
        if (h1 >= VM_MAX_LANE_FRONTIER_HANDLES || h1 >= vm_state->query_context->lane_frontiers_count ||
            h2 >= VM_MAX_LANE_FRONTIER_HANDLES || h2 >= vm_state->query_context->lane_frontiers_count) {
            return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        }
        const auto& lf1 = vm_state->query_context->lane_frontiers[h1];
        const auto& lf2 = vm_state->query_context->lane_frontiers[h2];
        size_t n_nodes = std::max(lf1.lanes.size(), lf2.lanes.size());
        uint32_t k_lanes = std::max(lf1.num_lanes, lf2.num_lanes);
        int h_lf_dst = (dst == src1) ? static_cast<int>(h1)
                     : ((dst == src2) ? static_cast<int>(h2)
                     : acquire_lane_frontier(vm_state->query_context, n_nodes, k_lanes));
        if (h_lf_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        auto& lf_dst = vm_state->query_context->lane_frontiers[h_lf_dst];
        if (lf_dst.lanes.size() < n_nodes) lf_dst.lanes.resize(n_nodes, 0ULL);
        lf_dst.num_lanes = k_lanes;
        lf_dst.active_mask = lf1.active_mask | lf2.active_mask;
        bool any_set = false;
        for (size_t u = 0; u < n_nodes; ++u) {
            uint64_t w1 = (u < lf1.lanes.size()) ? lf1.lanes[u] : 0ULL;
            uint64_t w2 = (u < lf2.lanes.size()) ? lf2.lanes[u] : 0ULL;
            uint64_t w = (w1 & ~w2) & lf_dst.active_mask;
            lf_dst.lanes[u] = w;
            if (w != 0ULL) any_set = true;
        }
        vm_state->registers[dst] = static_cast<uint64_t>(h_lf_dst);
        vm_state->register_types[dst] = TYPE_LANE_FRONTIER;
        if (any_set) vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;
        else vm_state->flags |= IMPULSE_VM_FLAG_ZF;
        vm_state->pc++;
        DISPATCH();
    }
    bool dst_was_bitset = (vm_state->register_types[dst] == TYPE_BITSET_HANDLE);

    int h_dst = -1;
    if (dst_was_bitset) {
        h_dst = static_cast<int>(vm_state->registers[dst]);
    } else {
        h_dst = acquire_bitset(vm_state->query_context);
        if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        vm_state->registers[dst] = static_cast<uint64_t>(h_dst);
        vm_state->register_types[dst] = TYPE_BITSET_HANDLE;
    }
    auto& bs_dst = vm_state->query_context->bitsets[h_dst];

    auto load_into = [&](VmBitSet& bs, uint8_t rtype, uint64_t rval) {
        bs.clear();
        if (rtype == TYPE_BITSET_HANDLE) {
            int h_s = static_cast<int>(rval);
            const auto& bs_s = vm_state->query_context->bitsets[h_s];
            for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                bs.words[i] = bs_s.words[i];
            }
        } else if (rtype == TYPE_NODE_ID || rtype == TYPE_INT64) {
            bitset_add(bs, rval, vm_state->query_context->max_nodes);
        } else if (rtype == TYPE_NODE_VECTOR || rtype == TYPE_UINT64_VECTOR) {
            const uint64_t* ptr = nullptr;
            size_t len = 0;
            if (get_node_vector_slice(vm_state->query_context, rval, &ptr, &len)) {
                for (size_t i = 0; i < len; ++i) {
                    bitset_add(bs, ptr[i], vm_state->query_context->max_nodes);
                }
            }
        }
    };

    auto diff_into = [&](VmBitSet& bs, uint8_t rtype, uint64_t rval) {
        if (rtype == TYPE_BITSET_HANDLE) {
            int h_s = static_cast<int>(rval);
            const auto& bs_s = vm_state->query_context->bitsets[h_s];
            for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                bs.words[i] &= ~bs_s.words[i];
            }
        } else if (rtype == TYPE_NODE_ID || rtype == TYPE_INT64) {
            if (rval < bs.word_count * 64) {
                bs.words[rval / 64] &= ~(1ULL << (rval % 64));
            }
        } else if (rtype == TYPE_NODE_VECTOR || rtype == TYPE_UINT64_VECTOR) {
            const uint64_t* ptr = nullptr;
            size_t len = 0;
            if (get_node_vector_slice(vm_state->query_context, rval, &ptr, &len)) {
                for (size_t i = 0; i < len; ++i) {
                    uint64_t u = ptr[i];
                    if (u < bs.word_count * 64) {
                        bs.words[u / 64] &= ~(1ULL << (u % 64));
                    }
                }
            }
        }
    };

    if (dst == src1 && dst_was_bitset) {
        diff_into(bs_dst, type2, val2);
    } else if (dst == src2 && dst_was_bitset) {
        auto& tmp_bs = vm_state->query_context->private_bitsets[0];
        load_into(tmp_bs, type1, val1);
        for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
            bs_dst.words[i] = tmp_bs.words[i] & ~bs_dst.words[i];
        }
    } else {
        load_into(bs_dst, type1, val1);
        diff_into(bs_dst, type2, val2);
    }

    bool is_empty = true;
    for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
        if (bs_dst.words[i] != 0) {
            is_empty = false;
            break;
        }
    }

    if (is_empty) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
    else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

    vm_state->pc++;
    DISPATCH();
}

