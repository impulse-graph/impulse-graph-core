/**
 * Formal Opcode Contract: OP_LANE_FILTER_QUORUM (0xC6)
 *
 * Layout:
 *   Variant 0 (flags & 0x01 == 0): Basic Quorum Filter
 *     dst_reg:        inst.dst_reg (DST_REG)
 *     src_reg:        inst.payload & 0xFFFF (PAYLOAD_LOW16)
 *     min_k:          (inst.payload >> 16) & 0xFFFF
 *     allowed_src:    TYPE_LANE_FRONTIER
 *     allowed_aux1/2: none
 *     output_dst:     TYPE_BITSET_HANDLE
 *     flags:          ZF set if quorum bitset is empty
 *     requires_ctx:   true
 *
 *   Variant 1 (flags & 0x01 == 1): Extended Quorum with Req/Forbid Masks
 *     dst_reg:        inst.dst_reg (DST_REG)
 *     src_reg:        inst.payload & 0xFF (PAYLOAD_LOW8)
 *     min_k:          (inst.payload >> 8) & 0xFF
 *     req_reg (aux1): (inst.payload >> 16) & 0xFF (PAYLOAD_BYTE2)
 *     forbid_reg(aux2):(inst.payload >> 24) & 0xFF (PAYLOAD_BYTE3)
 *     allowed_src:    TYPE_LANE_FRONTIER
 *     allowed_aux1:   TYPE_INT64 | TYPE_NODE_ID (zero skip allowed)
 *     allowed_aux2:   TYPE_INT64 | TYPE_NODE_ID (zero skip allowed)
 *     output_dst:     TYPE_BITSET_HANDLE
 *     flags:          ZF set if quorum bitset is empty
 *     requires_ctx:   true
 */
#ifndef OP_C6_CONTRACT_DEFINED
#define OP_C6_CONTRACT_DEFINED
#define OP_C6_DEFINED_FLAGS        0x01u
#define OP_C6_ALLOWED_SRC_TYPES    (IMP_TYPE_BIT(TYPE_LANE_FRONTIER))
#define OP_C6_FLAG0_AUX1_TYPES     IMP_TYPES_NONE
#define OP_C6_FLAG1_AUX1_TYPES     IMP_TYPES_INT_OR_NODE
#define OP_C6_FLAG1_AUX2_TYPES     IMP_TYPES_INT_OR_NODE
#define OP_C6_OUTPUT_DST_TYPES     (IMP_TYPE_BIT(TYPE_BITSET_HANDLE))
#endif

// Opcode 0xC6: OP_LANE_FILTER_QUORUM
op_LANE_FILTER_QUORUM: {
    {
        const auto& inst = bytecode[vm_state->pc];
        uint16_t dst = IMP_INST_DST(inst);
        const bool is_ext_quorum = IMP_INST_HAS_FLAG(inst, IMP_FLAG_EXTENDED_QUORUM);
        uint16_t src_reg = is_ext_quorum ? IMP_INST_U8_0(inst) : IMP_INST_LOW16(inst);
        uint32_t min_k = is_ext_quorum
            ? IMP_INST_U8_1(inst)
            : (IMP_INST_HIGH16(inst) > 0u ? IMP_INST_HIGH16(inst) : 1u);
        uint16_t req_reg = IMP_INST_U8_2(inst);
        uint16_t forbid_reg = IMP_INST_U8_3(inst);
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

        IMPULSE_TESTCASE(is_ext_quorum);
        if (is_ext_quorum) {
            IMPULSE_TESTCASE(req_reg > 0u);
            if (req_reg > 0u) {
                IMP_VALIDATE_GPR(req_reg);
                IMPULSE_CHECK_REG_TYPE(vm_state, req_reg, IMP_TYPES_INT_OR_NODE);
            }
            IMPULSE_TESTCASE(forbid_reg > 0u);
            if (forbid_reg > 0u) {
                IMP_VALIDATE_GPR(forbid_reg);
                IMPULSE_CHECK_REG_TYPE(vm_state, forbid_reg, IMP_TYPES_INT_OR_NODE);
            }
        }

        uint64_t req_mask = (is_ext_quorum && req_reg > 0u)
            ? (vm_state->registers[req_reg] & lf.active_mask)
            : lf.active_mask;
        uint64_t forbid_mask = (is_ext_quorum && forbid_reg > 0u)
            ? (vm_state->registers[forbid_reg] & lf.active_mask)
            : 0ULL;

        int h_bs = acquire_bitset(vm_state->query_context);
        if (h_bs < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        IMPULSE_ASSERT(static_cast<size_t>(h_bs) < VM_MAX_BITSET_HANDLES);
        auto& bs = vm_state->query_context->bitsets[h_bs];
        bs.clear();

        bool any_out = false;
        IMPULSE_TESTCASE(min_k == 0u);
        if (min_k > 0u) {
            for (size_t w_idx = 0; w_idx < lf.active_words.size(); ++w_idx) {
                uint64_t word = lf.active_words[w_idx];
                while (word != 0ULL) {
                    uint32_t bit = static_cast<uint32_t>(std::countr_zero(word));
                    size_t u = static_cast<size_t>(IMP_WORD_BIT_TO_NODE(w_idx, bit));
                    word = IMP_CLEAR_LOWEST_BIT(word);
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
        IMP_VM_SET_ZF(vm_state, !any_out);
    }
    vm_state->pc++;
    DISPATCH();
}

