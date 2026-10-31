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
#define OP_C7_ALLOWED_SRC_TYPES    (IMP_TYPE_BIT(TYPE_LANE_FRONTIER))
#define OP_C7_OUTPUT_DST_TYPES     (IMP_TYPE_BIT(TYPE_INT64))
#endif

// Opcode 0xC7: OP_LANE_REDUCE_AND
op_LANE_REDUCE_AND: {
    {
        const auto& inst = bytecode[vm_state->pc];
        uint16_t dst = inst.dst_reg;
        uint16_t src_reg = inst.payload & 0xFFFF;
        uint16_t max_nodes_opt = (inst.payload >> 16) & 0xFFFF;
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

        size_t n_check = (max_nodes_opt > 0) ? std::min<size_t>(lf.lanes.size(), max_nodes_opt) : lf.lanes.size();
        uint64_t all_reached = (n_check > 0) ? lf.active_mask : 0ULL;
        uint64_t universal_nodes = 0;
        for (size_t u = 0; u < n_check; ++u) {
            uint64_t w = lf.lanes[u] & lf.active_mask;
            all_reached &= w;
            if (lf.active_mask != 0ULL && w == lf.active_mask) {
                universal_nodes++;
            }
        }

        uint64_t res = ((flags & 0x01) != 0)
            ? (lf.active_mask & ~all_reached)
            : (((flags & 0x02) != 0) ? universal_nodes : all_reached);

        vm_state->registers[dst] = res;
        vm_state->register_types[dst] = TYPE_INT64;
        if (res == 0ULL) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
        else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;
    }
    vm_state->pc++;
    DISPATCH();
}

