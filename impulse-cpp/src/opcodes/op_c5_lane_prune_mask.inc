/**
 * Formal Opcode Contract: OP_LANE_PRUNE_MASK (0xC5)
 *
 * Layout:
 *   dst_reg:          inst.dst_reg (DST_REG)
 *   src_reg:          inst.payload & 0xFF (PAYLOAD_LOW8)
 *   arg1_reg (aux1):  (inst.payload >> 8) & 0xFF (PAYLOAD_BYTE1)
 *   allowed_src:      TYPE_LANE_FRONTIER
 *   output_dst:       TYPE_LANE_FRONTIER
 *   modes (flags & 0x03):
 *     0x00: Mode 0 - Mask AND (allowed aux1: TYPE_INT64 | TYPE_NODE_ID)
 *     0x01: Mode 1 - Mask AND-NOT (allowed aux1: TYPE_INT64 | TYPE_NODE_ID)
 *     0x02: Mode 2 - Degree Hub Pruning (allowed aux1: TYPE_INT64 | TYPE_NODE_ID, zero skip)
 *     0x03: Mode 3 - Invert Lanes (allowed aux1: none)
 *   flags:            ZF set if active frontier mask is empty
 *   requires_ctx:     true
 */
#ifndef OP_C5_CONTRACT_DEFINED
#define OP_C5_CONTRACT_DEFINED
#define OP_C5_DEFINED_FLAGS        0x03u
#define OP_C5_MODE_MASK_CLEAR      0x00u
#define OP_C5_MODE_ALLOW_DENY_HALF 0x01u
#define OP_C5_MODE_DEGREE_HUB      0x02u
#define OP_C5_MODE_PASSTHROUGH     0x03u
#define OP_C5_MAX_HALF_LANES       32u
#define OP_C5_ALLOWED_SRC_TYPES    (IMP_TYPE_BIT(TYPE_LANE_FRONTIER))
#define OP_C5_MODES012_AUX1_TYPES  IMP_TYPES_INT_OR_NODE
#define OP_C5_MODE3_AUX1_TYPES     IMP_TYPES_NONE
#define OP_C5_OUTPUT_DST_TYPES     (IMP_TYPE_BIT(TYPE_LANE_FRONTIER))
#endif

// Opcode 0xC5: OP_LANE_PRUNE_MASK
op_LANE_PRUNE_MASK: {
    {
        const auto& inst = bytecode[vm_state->pc];
        uint16_t dst = IMP_INST_DST(inst);
        uint16_t src_reg = IMP_INST_U8_0(inst);
        uint16_t arg1 = IMP_INST_U8_1(inst);
        uint16_t arg2 = IMP_INST_U8_2(inst);
        uint16_t rel_id = IMP_INST_U8_3(inst);
        uint8_t mode = static_cast<uint8_t>(IMP_INST_FLAGS(inst) & IMP_FLAG_LOW2_MASK);
        IMP_VALIDATE_GPR(dst);
        IMP_VALIDATE_GPR(src_reg);
        IMP_REQUIRE_CONTEXT(vm_state);
        IMPULSE_ASSERT(vm_state->query_context != nullptr);

        if (vm_state->register_types[src_reg] != TYPE_LANE_FRONTIER) {
            return IMPULSE_VM_ERR_INVALID_REGISTER;
        }
        size_t h_src = static_cast<size_t>(vm_state->registers[src_reg]);
        IMPULSE_TESTCASE(h_src >= VM_MAX_LANE_FRONTIER_HANDLES || h_src >= vm_state->query_context->lane_frontiers_count);
        if (h_src >= VM_MAX_LANE_FRONTIER_HANDLES || h_src >= vm_state->query_context->lane_frontiers_count) {
            return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        }
        int h_dst = clone_lane_frontier(vm_state->query_context, h_src);
        if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        IMPULSE_ASSERT(static_cast<size_t>(h_dst) < VM_MAX_LANE_FRONTIER_HANDLES);
        auto& lf_dst = vm_state->query_context->lane_frontiers[h_dst];
        size_t n_nodes = lf_dst.lanes.size();

        IMPULSE_TESTCASE(mode == OP_C5_MODE_MASK_CLEAR);
        IMPULSE_TESTCASE(mode == OP_C5_MODE_ALLOW_DENY_HALF);
        IMPULSE_TESTCASE(mode == OP_C5_MODE_DEGREE_HUB);
        IMPULSE_TESTCASE(mode == OP_C5_MODE_PASSTHROUGH);

        if (mode == OP_C5_MODE_MASK_CLEAR) {
            IMP_VALIDATE_GPR(arg1);
            IMPULSE_CHECK_REG_TYPE(vm_state, arg1, (IMP_TYPE_BIT(TYPE_BITSET_HANDLE) | IMP_TYPES_NODE_VEC | IMP_TYPES_INT_OR_NODE));
            uint8_t t_arg = vm_state->register_types[arg1];
            if (t_arg == TYPE_BITSET_HANDLE) {
                uint64_t h_bs = vm_state->registers[arg1];
                if (h_bs >= VM_MAX_BITSET_HANDLES || !vm_state->query_context->bitset_allocated[h_bs]) {
                    return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                }
                const auto& bs = vm_state->query_context->bitsets[h_bs];
                for (size_t u = 0; u < n_nodes; ++u) {
                    if (bs.test(u)) lf_dst.lanes[u] = 0ULL;
                }
            } else if (t_arg == TYPE_NODE_VECTOR || t_arg == TYPE_UINT64_VECTOR) {
                const uint64_t* m_ptr = nullptr;
                size_t m_len = 0;
                if (!get_node_vector_slice(vm_state->query_context, vm_state->registers[arg1], &m_ptr, &m_len)) {
                    return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                }
                size_t lim = std::min(n_nodes, m_len);
                for (size_t u = 0; u < lim; ++u) {
                    lf_dst.lanes[u] &= ~m_ptr[u];
                }
            } else if (t_arg == TYPE_INT64 || t_arg == TYPE_NODE_ID) {
                uint64_t mask = vm_state->registers[arg1];
                for (size_t u = 0; u < n_nodes; ++u) {
                    lf_dst.lanes[u] &= ~mask;
                }
            }
        } else if (mode == OP_C5_MODE_ALLOW_DENY_HALF) {
            uint32_t half = (arg1 > 0) ? std::min<uint32_t>(OP_C5_MAX_HALF_LANES, arg1) : std::max<uint32_t>(1u, lf_dst.num_lanes / 2u);
            uint64_t allow_mask = impulse_make_active_mask_u64(half);
            for (size_t u = 0; u < n_nodes; ++u) {
                uint64_t w = lf_dst.lanes[u];
                uint64_t allow_bits = w & allow_mask;
                uint64_t deny_bits = (w >> half) & allow_mask;
                lf_dst.lanes[u] = allow_bits & ~deny_bits;
            }
        } else if (mode == OP_C5_MODE_DEGREE_HUB) {
            if (arg1 > 0 && arg1 != src_reg) {
                IMP_VALIDATE_GPR(arg1);
                IMPULSE_CHECK_REG_TYPE(vm_state, arg1, (IMP_TYPES_INT_OR_NODE | IMP_TYPES_NODE_VEC));
            }
            uint8_t t_arg = (arg1 > 0 && arg1 != src_reg) ? vm_state->register_types[arg1] : static_cast<uint8_t>(TYPE_NULL);
            uint64_t max_deg = (t_arg == TYPE_INT64 || t_arg == TYPE_NODE_ID) ? vm_state->registers[arg1] : arg1;
            uint32_t min_k = std::max<uint32_t>(1u, arg2);
            for (size_t u = 0; u < n_nodes; ++u) {
                uint64_t deg = 0;
                if (rel_id < vm_state->query_context->slots.size()) {
                    const auto& slot = vm_state->query_context->slots[rel_id];
                    if (slot.offsets_ptr && u < slot.node_count) {
                        deg = slot.get_csr_offset(u + 1) - slot.get_csr_offset(u);
                    }
                }
                if (deg > max_deg) {
                    uint32_t pc_bits = static_cast<uint32_t>(std::popcount(lf_dst.lanes[u] & lf_dst.active_mask));
                    if (pc_bits < min_k) {
                        lf_dst.lanes[u] = 0ULL;
                    }
                }
            }
        }

        bool any_set = false;
        lf_dst.active_words.assign((n_nodes + IMP_WORD_BIT_MASK) >> IMP_WORD_BIT_SHIFT, 0ULL);
        for (size_t u = 0; u < n_nodes; ++u) {
            lf_dst.lanes[u] &= lf_dst.active_mask;
            if (lf_dst.lanes[u] != 0ULL) {
                lf_dst.active_words[IMP_WORD_INDEX(u)] |= IMP_BIT_MASK64(u);
                any_set = true;
            }
        }

        vm_state->registers[dst] = static_cast<uint64_t>(h_dst);
        vm_state->register_types[dst] = TYPE_LANE_FRONTIER;
        IMP_VM_SET_ZF(vm_state, !any_set);
    }
    vm_state->pc++;
    DISPATCH();
}

