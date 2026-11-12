/**
 * Formal Opcode Contract: OP_LANE_REDUCE_AND (0xC7)
 *
 * Layout:
 *   dst_reg:          inst.dst_reg (DST_REG)
 *   src_reg:          inst.payload & 0xFFFF (PAYLOAD_LOW16)
 *   max_nodes_opt:    (inst.payload >> 16) & 0xFFFF
 *   allowed_src:      TYPE_LANE_FRONTIER
 *   output_dst:       TYPE_INT64
 *   modes (flags & 0x03):
 *     0x00: Bitwise AND all reached mask
 *     0x01: Islanded mask (active & ~all_reached)
 *     0x02: Universal node count
 *   flags:            ZF set if result is zero
 *   requires_ctx:     true
 */
#ifndef OP_C7_CONTRACT_DEFINED
#define OP_C7_CONTRACT_DEFINED
#define OP_C7_DEFINED_FLAGS        0x03u
#define OP_C7_FLAG_ISLANDED_MASK   IMP_FLAG_BIT0
#define OP_C7_FLAG_UNIVERSAL_COUNT IMP_FLAG_BIT1
#define OP_C7_ALLOWED_SRC_TYPES    (IMP_TYPE_BIT(TYPE_LANE_FRONTIER))
#define OP_C7_OUTPUT_DST_TYPES     (IMP_TYPE_BIT(TYPE_INT64))
#endif

// Opcode 0xC7: OP_LANE_REDUCE_AND
op_LANE_REDUCE_AND: {
    {
        const auto& inst = bytecode[vm_state->pc];
        uint16_t dst = IMP_INST_DST(inst);
        uint16_t src_reg = IMP_INST_LOW16(inst);
        uint16_t max_nodes_opt = IMP_INST_HIGH16(inst);
        const bool want_islanded = IMP_INST_HAS_FLAG(inst, OP_C7_FLAG_ISLANDED_MASK);
        const bool want_universal = IMP_INST_HAS_FLAG(inst, OP_C7_FLAG_UNIVERSAL_COUNT);
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
        const auto& lf = vm_state->query_context->lane_frontiers[h_src];

        IMPULSE_TESTCASE(max_nodes_opt > 0u);
        IMPULSE_TESTCASE(want_islanded);
        IMPULSE_TESTCASE(want_universal);
        size_t n_check = (max_nodes_opt > 0u) ? std::min<size_t>(lf.lanes.size(), max_nodes_opt) : lf.lanes.size();
        IMPULSE_TESTCASE(n_check == 0u);
        uint64_t all_reached = (n_check > 0u) ? lf.active_mask : 0ULL;
        uint64_t universal_nodes = 0;
        for (size_t u = 0; u < n_check; ++u) {
            uint64_t w = lf.lanes[u] & lf.active_mask;
            all_reached &= w;
            if (lf.active_mask != 0ULL && w == lf.active_mask) {
                universal_nodes++;
            }
        }

        uint64_t res = want_islanded
            ? (lf.active_mask & ~all_reached)
            : (want_universal ? universal_nodes : all_reached);

        vm_state->registers[dst] = res;
        vm_state->register_types[dst] = TYPE_INT64;
        IMP_VM_SET_ZF(vm_state, res == 0ULL);
    }
    vm_state->pc++;
    DISPATCH();
}

