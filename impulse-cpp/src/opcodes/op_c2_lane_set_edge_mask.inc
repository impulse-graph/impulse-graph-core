/**
 * Formal Opcode Contract: OP_LANE_SET_EDGE_MASK (0xC2)
 *
 * Layout:
 *   Variant 0 (flags & 0x01 == 0): Clear Edge Mask
 *     dst_reg:        inst.dst_reg (DST_REG)
 *     allowed_src:    none
 *     output_dst:     TYPE_LANE_FRONTIER
 *     flags:          no flags modified
 *     requires_ctx:   true
 *
 *   Variant 1 (flags & 0x01 == 1): Set Edge Mask from Register
 *     dst_reg:        inst.dst_reg (DST_REG)
 *     edge_reg (src): (inst.payload >> 8) & 0xFF (PAYLOAD_BYTE1)
 *     allowed_src:    TYPE_INT64 | TYPE_NODE_ID
 *     output_dst:     TYPE_LANE_FRONTIER
 *     flags:          no flags modified
 *     requires_ctx:   true
 */
#ifndef OP_C2_CONTRACT_DEFINED
#define OP_C2_CONTRACT_DEFINED
#define OP_C2_DEFINED_FLAGS        0x01u
#define OP_C2_FLAG_SET_FROM_REG    IMP_FLAG_BIT0
#define OP_C2_LANE_INDEX_MASK      0x3Fu
#define OP_C2_MIN_EDGE_MASK_CAP    64u
#define OP_C2_EDGE_SCALE_FACTOR    4u
#define OP_C2_MAX_SANE_EDGE_IDX    (1ULL << 20)
#define OP_C2_PACKED_EDGE_MASK     0xFFFFFFFFULL
#define OP_C2_PACKED_LANE_SHIFT    32u
#define OP_C2_FLAG0_ALLOWED_SRC    IMP_TYPES_NONE
#define OP_C2_FLAG1_ALLOWED_SRC    (IMP_TYPE_BIT(TYPE_INT64) | IMP_TYPE_BIT(TYPE_NODE_ID) | IMP_TYPES_NODE_VEC)
#define OP_C2_OUTPUT_DST_TYPES     (IMP_TYPE_BIT(TYPE_LANE_FRONTIER))
#endif

// Opcode 0xC2: OP_LANE_SET_EDGE_MASK
op_LANE_SET_EDGE_MASK: {
    {
        const auto& inst = bytecode[vm_state->pc];
        uint16_t dst = IMP_INST_DST(inst);
        uint16_t src_reg = IMP_INST_U8_0(inst);
        uint16_t edge_reg = IMP_INST_U8_1(inst);
        uint16_t rel_id = IMP_INST_HIGH16(inst);
        const bool set_from_reg = IMP_INST_HAS_FLAG(inst, OP_C2_FLAG_SET_FROM_REG);
        IMP_VALIDATE_GPR(dst);
        IMP_VALIDATE_GPR(src_reg);
        IMPULSE_TESTCASE(set_from_reg);
        if (set_from_reg) {
            IMP_VALIDATE_GPR(edge_reg);
            IMPULSE_CHECK_REG_TYPE(vm_state, edge_reg, OP_C2_FLAG1_ALLOWED_SRC);
        }
        IMP_REQUIRE_CONTEXT(vm_state);
        IMPULSE_ASSERT(vm_state->query_context != nullptr);

        IMPULSE_CHECK_REG_TYPE(vm_state, src_reg, OP_C2_OUTPUT_DST_TYPES);
        size_t h_src = static_cast<size_t>(vm_state->registers[src_reg]);
        IMPULSE_TESTCASE(h_src >= VM_MAX_LANE_FRONTIER_HANDLES || h_src >= vm_state->query_context->lane_frontiers_count);
        if (h_src >= VM_MAX_LANE_FRONTIER_HANDLES || h_src >= vm_state->query_context->lane_frontiers_count) {
            return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        }
        int h_dst = clone_lane_frontier(vm_state->query_context, h_src);
        if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        IMPULSE_ASSERT(static_cast<size_t>(h_dst) < VM_MAX_LANE_FRONTIER_HANDLES);
        auto& lf_dst = vm_state->query_context->lane_frontiers[h_dst];

        size_t rel_slot = (rel_id < VM_MAX_REL_SLOTS) ? rel_id : 0;
        IMPULSE_ASSERT(rel_slot < VM_MAX_REL_SLOTS);
        size_t n_edges = OP_C2_MIN_EDGE_MASK_CAP;
        if (rel_id < vm_state->query_context->slots.size() && vm_state->query_context->slots[rel_id].edge_count > 0) {
            n_edges = std::max<size_t>(n_edges, vm_state->query_context->slots[rel_id].edge_count);
        } else {
            n_edges = std::max<size_t>(n_edges, lf_dst.lanes.size() * OP_C2_EDGE_SCALE_FACTOR);
        }
        auto& trip_arr = lf_dst.edge_trip_masks[rel_slot];
        if ((lf_dst.edge_trip_active_mask & (1u << rel_slot)) == 0) {
            trip_arr.clear();
            trip_arr.resize(n_edges, 0ULL);
            lf_dst.edge_trip_active_mask |= static_cast<uint16_t>(1u << rel_slot);
        } else if (trip_arr.size() < n_edges) {
            trip_arr.resize(n_edges, 0ULL);
        }

        uint8_t t_edge = (edge_reg < IMP_VM_GPR_COUNT)
            ? vm_state->register_types[edge_reg]
            : static_cast<uint8_t>(TYPE_NULL);
        IMPULSE_TESTCASE(t_edge == TYPE_NULL);
        if (t_edge == TYPE_INT64 || t_edge == TYPE_NODE_ID) {
            uint64_t e_idx = vm_state->registers[edge_reg];
            uint32_t lane_b = static_cast<uint32_t>(IMP_INST_FLAGS(inst) & OP_C2_LANE_INDEX_MASK);
            if (lane_b < IMP_VM_MAX_LANES && e_idx < OP_C2_MAX_SANE_EDGE_IDX) {
                if (e_idx >= trip_arr.size()) trip_arr.resize(static_cast<size_t>(e_idx) + 1, 0ULL);
                trip_arr[e_idx] |= IMP_BIT_MASK64(lane_b) & lf_dst.active_mask;
            }
        } else if (t_edge == TYPE_NODE_VECTOR || t_edge == TYPE_UINT64_VECTOR) {
            const uint64_t* e_ptr = nullptr;
            size_t e_len = 0;
            if (!get_node_vector_slice(vm_state->query_context, vm_state->registers[edge_reg], &e_ptr, &e_len)) {
                return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
            }
            if (set_from_reg) {
                for (size_t i = 0; i < e_len; ++i) {
                    uint64_t packed = e_ptr[i];
                    uint64_t e_idx = packed & OP_C2_PACKED_EDGE_MASK;
                    uint32_t lane_b = static_cast<uint32_t>((packed >> OP_C2_PACKED_LANE_SHIFT) & OP_C2_LANE_INDEX_MASK);
                    if (e_idx < OP_C2_MAX_SANE_EDGE_IDX) {
                        if (e_idx >= trip_arr.size()) trip_arr.resize(static_cast<size_t>(e_idx) + 1, 0ULL);
                        trip_arr[e_idx] |= IMP_BIT_MASK64(lane_b) & lf_dst.active_mask;
                    }
                }
            } else {
                size_t lim = std::min<size_t>(IMP_VM_MAX_LANES, std::min<size_t>(lf_dst.num_lanes, e_len));
                for (size_t b = 0; b < lim; ++b) {
                    uint64_t e_idx = e_ptr[b];
                    IMPULSE_TESTCASE(e_idx == IMP_INVALID_NODE_U64);
                    if (e_idx != IMP_INVALID_NODE_U64 && e_idx < OP_C2_MAX_SANE_EDGE_IDX) {
                        if (e_idx >= trip_arr.size()) trip_arr.resize(static_cast<size_t>(e_idx) + 1, 0ULL);
                        trip_arr[e_idx] |= IMP_BIT_MASK64(b) & lf_dst.active_mask;
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

