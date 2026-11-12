/**
 * Formal Opcode Contract: OP_LANE_INIT_GROUPS (0xC1)
 *
 * Layout:
 *   dst_reg:          inst.dst_reg (DST_REG)
 *   nodes_reg (src):  inst.payload & 0xFFFF (PAYLOAD_LOW16, or LOW8 if separate groups_reg)
 *   groups_reg(aux1): (inst.payload >> 8) & 0xFF
 *   allowed_src:      TYPE_NODE_VECTOR | TYPE_UINT64_VECTOR
 *   output_dst:       TYPE_LANE_FRONTIER
 *   flags:
 *     0x00: Standard group partitioning
 *     0x01: Inverted group partitioning
 *     ZF set if active frontier mask is empty
 *   requires_ctx:     true
 */
#ifndef OP_C1_CONTRACT_DEFINED
#define OP_C1_CONTRACT_DEFINED
#define OP_C1_DEFINED_FLAGS        0x03u
#define OP_C1_FLAG_PACKED_PAIRS    IMP_FLAG_BIT0
#define OP_C1_DEFAULT_NULL_LANES   8u
#define OP_C1_DEFAULT_NULL_NODES   64u
#define OP_C1_MAX_SANE_NODE_ID     (1ULL << 20)
#define OP_C1_PACKED_LOW32_MASK    0xFFFFFFFFULL
#define OP_C1_PACKED_GRP_SHIFT     32u
#define OP_C1_ALLOWED_SRC_TYPES    (IMP_TYPE_BIT(TYPE_NULL) | IMP_TYPE_BIT(TYPE_NODE_VECTOR) | IMP_TYPE_BIT(TYPE_UINT64_VECTOR))
#define OP_C1_OUTPUT_DST_TYPES     (IMP_TYPE_BIT(TYPE_LANE_FRONTIER))
#endif

// Opcode 0xC1: OP_LANE_INIT_GROUPS
op_LANE_INIT_GROUPS: {
    {
        const auto& inst = bytecode[vm_state->pc];
        uint16_t dst = IMP_INST_DST(inst);
        uint16_t nodes_reg = IMP_INST_U8_0(inst);
        uint16_t groups_reg = IMP_INST_U8_1(inst);
        uint16_t p_u16_high = IMP_INST_HIGH16(inst);
        IMP_VALIDATE_GPR(dst);
        IMP_VALIDATE_GPR(nodes_reg);
        IMP_VALIDATE_GPR(groups_reg);
        IMP_REQUIRE_CONTEXT(vm_state);
        IMPULSE_ASSERT(vm_state->query_context != nullptr);

        IMPULSE_CHECK_REG_TYPE(vm_state, nodes_reg, OP_C1_ALLOWED_SRC_TYPES);
        if (groups_reg > 0 && groups_reg != nodes_reg) {
            IMPULSE_CHECK_REG_TYPE(vm_state, groups_reg, OP_C1_ALLOWED_SRC_TYPES);
        }

        uint8_t t_nodes = vm_state->register_types[nodes_reg];
        uint8_t t_groups = vm_state->register_types[groups_reg];
        IMPULSE_TESTCASE(t_nodes == TYPE_NULL);

        if (t_nodes == TYPE_NULL) {
            uint32_t num_lanes = std::max<uint32_t>(1u, std::min<uint32_t>(IMP_VM_MAX_LANES, (p_u16_high > 0) ? p_u16_high : OP_C1_DEFAULT_NULL_LANES));
            size_t n_nodes = resolve_multilane_node_count(vm_state->query_context, 0, OP_C1_DEFAULT_NULL_NODES);
            int h_lf = acquire_lane_frontier(vm_state->query_context, n_nodes, num_lanes);
            if (h_lf < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
            IMPULSE_ASSERT(static_cast<size_t>(h_lf) < VM_MAX_LANE_FRONTIER_HANDLES);
            auto& lf = vm_state->query_context->lane_frontiers[h_lf];
            if (n_nodes > 0) {
                lf.lanes[0] = 1ULL & lf.active_mask;
                if (lf.lanes[0] != 0ULL) {
                    lf.active_words[0] |= 1ULL;
                }
            }
            vm_state->registers[dst] = static_cast<uint64_t>(h_lf);
            vm_state->register_types[dst] = TYPE_LANE_FRONTIER;
            IMP_VM_SET_ZF(vm_state, lf.active_mask == 0ULL);
            vm_state->pc++;
            DISPATCH();
        }

        auto& pair_nodes = vm_state->query_context->scratch_u64_a;
        auto& pair_grps = vm_state->query_context->scratch_u32_a;
        pair_nodes.clear();
        pair_grps.clear();

        const uint64_t* n_ptr = nullptr;
        size_t n_len = 0;
        if (!get_node_vector_slice(vm_state->query_context, vm_state->registers[nodes_reg], &n_ptr, &n_len)) {
            return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        }
        const bool has_separate_groups = (nodes_reg != groups_reg && groups_reg > 0 &&
                                          (t_groups == TYPE_NODE_VECTOR || t_groups == TYPE_UINT64_VECTOR));
        IMPULSE_TESTCASE(has_separate_groups);
        if (has_separate_groups) {
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
            bool is_packed = IMP_INST_HAS_FLAG(inst, OP_C1_FLAG_PACKED_PAIRS);
            IMPULSE_TESTCASE(is_packed);
            for (size_t i = 0; i < n_len; ++i) {
                uint64_t packed = n_ptr[i];
                uint64_t u = is_packed ? (packed & OP_C1_PACKED_LOW32_MASK) : packed;
                uint32_t hi = static_cast<uint32_t>((packed >> OP_C1_PACKED_GRP_SHIFT) & OP_C1_PACKED_LOW32_MASK);
                uint32_t grp = is_packed ? hi : std::min<uint32_t>(IMP_VM_MAX_LANES - 1u, static_cast<uint32_t>(i));
                pair_nodes.push_back(u);
                pair_grps.push_back(grp);
            }
        }

        size_t max_node = 0;
        int max_grp = -1;
        for (size_t i = 0; i < pair_nodes.size(); ++i) {
            if (pair_nodes[i] < OP_C1_MAX_SANE_NODE_ID && static_cast<size_t>(pair_nodes[i]) > max_node) {
                max_node = static_cast<size_t>(pair_nodes[i]);
            }
            if (pair_grps[i] < IMP_VM_MAX_LANES && static_cast<int>(pair_grps[i]) > max_grp) {
                max_grp = static_cast<int>(pair_grps[i]);
            }
        }
        IMPULSE_ASSERT(t_nodes != TYPE_NULL);
        uint32_t num_lanes = (p_u16_high > 0)
            ? std::min<uint32_t>(IMP_VM_MAX_LANES, p_u16_high)
            : std::min<uint32_t>(IMP_VM_MAX_LANES, static_cast<uint32_t>(std::max(0, max_grp + 1)));
        IMPULSE_ASSERT(num_lanes <= IMP_VM_MAX_LANES);
        IMPULSE_TESTCASE(num_lanes == 0u);
        IMPULSE_TESTCASE(num_lanes == IMP_VM_MAX_LANES);

        size_t n_nodes = resolve_multilane_node_count(vm_state->query_context, -1, max_node + 1);
        int h_lf = acquire_lane_frontier(vm_state->query_context, n_nodes, num_lanes);
        if (h_lf < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        IMPULSE_ASSERT(static_cast<size_t>(h_lf) < VM_MAX_LANE_FRONTIER_HANDLES);

        auto& lf = vm_state->query_context->lane_frontiers[h_lf];
        bool any_set = false;
        for (size_t i = 0; i < pair_nodes.size(); ++i) {
            if (pair_nodes[i] < n_nodes && pair_grps[i] < num_lanes) {
                size_t u = static_cast<size_t>(pair_nodes[i]);
                lf.lanes[u] |= IMP_BIT_MASK64(pair_grps[i]);
                lf.active_words[IMP_WORD_INDEX(u)] |= IMP_BIT_MASK64(u);
                any_set = true;
            }
        }

        vm_state->registers[dst] = static_cast<uint64_t>(h_lf);
        vm_state->register_types[dst] = TYPE_LANE_FRONTIER;
        IMP_VM_SET_ZF(vm_state, !any_set);
    }
    vm_state->pc++;
    DISPATCH();
}

