/**
 * Formal Opcode Contract: OP_LANE_INIT_SEEDS (0xC0)
 *
 * Layout:
 *   Variant 0 (flags & 0x01 == 0): Init Seeds from Node Vector / BitSet / ID / Null
 *     dst_reg:        inst.dst_reg (DST_REG)
 *     src_reg:        inst.payload & 0xFFFF (PAYLOAD_LOW16)
 *     allowed_src:    TYPE_NULL | TYPE_NODE_ID | TYPE_INT64 | TYPE_BITSET_HANDLE |
 *                     TYPE_NODE_VECTOR | TYPE_UINT64_VECTOR
 *     output_dst:     TYPE_LANE_FRONTIER
 *     flags:          ZF set if active frontier mask is empty
 *     requires_ctx:   true
 *
 *   Variant 1 (flags & 0x01 == 1): Init Range Seeds
 *     dst_reg:        inst.dst_reg (DST_REG)
 *     payload_hi16:   (inst.payload >> 16) & 0xFFFF (k seeds count)
 *     allowed_src:    none (immediate range)
 *     output_dst:     TYPE_LANE_FRONTIER
 *     flags:          ZF set if active frontier mask is empty
 *     requires_ctx:   true
 */
#ifndef OP_C0_CONTRACT_DEFINED
#define OP_C0_CONTRACT_DEFINED
#define OP_C0_DEFINED_FLAGS        0x01u
#define OP_C0_FLAG_RANGE_SEEDS     IMP_FLAG_BIT0
#define OP_C0_DEFAULT_NULL_LANES   8u
#define OP_C0_MAX_SANE_SEED_ID     (1ULL << 20)
#define OP_C0_FLAG0_ALLOWED_SRC    (IMP_TYPE_BIT(TYPE_NULL) | IMP_TYPE_BIT(TYPE_NODE_ID) | \
                                    IMP_TYPE_BIT(TYPE_INT64) | IMP_TYPE_BIT(TYPE_BITSET_HANDLE) | \
                                    IMP_TYPE_BIT(TYPE_NODE_VECTOR) | IMP_TYPE_BIT(TYPE_UINT64_VECTOR))
#define OP_C0_FLAG1_ALLOWED_SRC    IMP_TYPES_NONE
#define OP_C0_OUTPUT_DST_TYPES     (IMP_TYPE_BIT(TYPE_LANE_FRONTIER))
#endif

// Opcode 0xC0: OP_LANE_INIT_SEEDS
op_LANE_INIT_SEEDS: {
    {
        const auto& inst = bytecode[vm_state->pc];
        uint16_t dst = IMP_INST_DST(inst);
        uint16_t src_reg = IMP_INST_LOW16(inst);
        uint16_t p_u16_high = IMP_INST_HIGH16(inst);
        const bool is_range_mode = IMP_INST_HAS_FLAG(inst, OP_C0_FLAG_RANGE_SEEDS);
        IMP_VALIDATE_GPR(dst);
        IMP_VALIDATE_GPR(src_reg);
        IMP_REQUIRE_CONTEXT(vm_state);
        IMPULSE_ASSERT(vm_state->query_context != nullptr);

        uint8_t src_type = vm_state->register_types[src_reg];
        uint64_t src_val = vm_state->registers[src_reg];
        IMPULSE_TESTCASE(is_range_mode);
        IMPULSE_TESTCASE(src_type == TYPE_NULL);

        std::array<uint64_t, IMP_VM_MAX_LANES> seeds{};
        size_t seeds_len = 0;
        uint32_t k_override = 0;
        bool has_k_override = false;

        if (src_type == TYPE_NULL) {
            uint32_t k = (p_u16_high > 0) ? std::min<uint32_t>(IMP_VM_MAX_LANES, p_u16_high) : OP_C0_DEFAULT_NULL_LANES;
            has_k_override = true;
            k_override = k;
            if (is_range_mode) {
                for (uint32_t b = 0; b < k; ++b) seeds[seeds_len++] = b;
            } else if (k >= 1u) {
                seeds[seeds_len++] = 0ULL;
            }
        } else if (src_type == TYPE_NODE_ID || src_type == TYPE_INT64) {
            if (is_range_mode && p_u16_high > 1) {
                uint32_t k = std::min<uint32_t>(IMP_VM_MAX_LANES, p_u16_high);
                for (uint32_t b = 0; b < k; ++b) seeds[seeds_len++] = src_val + b;
            } else {
                seeds[seeds_len++] = src_val;
            }
        } else if (src_type == TYPE_BITSET_HANDLE) {
            IMPULSE_TESTCASE(src_val >= VM_MAX_BITSET_HANDLES || !vm_state->query_context->bitset_allocated[src_val]);
            if (src_val >= VM_MAX_BITSET_HANDLES || !vm_state->query_context->bitset_allocated[src_val]) {
                return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
            }
            const auto& bs = vm_state->query_context->bitsets[src_val];
            for (size_t w = 0; w < bs.word_count && seeds_len < IMP_VM_MAX_LANES; ++w) {
                uint64_t word = bs.words[w];
                while (word != 0ULL && seeds_len < IMP_VM_MAX_LANES) {
                    uint32_t tz = static_cast<uint32_t>(std::countr_zero(word));
                    seeds[seeds_len++] = IMP_WORD_BIT_TO_NODE(w, tz);
                    word = IMP_CLEAR_LOWEST_BIT(word);
                }
            }
        } else if (src_type == TYPE_NODE_VECTOR || src_type == TYPE_UINT64_VECTOR) {
            const uint64_t* ptr = nullptr;
            size_t len = 0;
            if (!get_node_vector_slice(vm_state->query_context, src_val, &ptr, &len)) {
                return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
            }
            size_t k = std::min<size_t>(IMP_VM_MAX_LANES, len);
            for (size_t b = 0; b < k; ++b) {
                seeds[seeds_len++] = ptr[b];
            }
        } else {
            return IMPULSE_VM_ERR_INVALID_REGISTER;
        }

        uint32_t k = has_k_override ? k_override : std::min<uint32_t>(IMP_VM_MAX_LANES, static_cast<uint32_t>(seeds_len));
        IMPULSE_ASSERT(k <= IMP_VM_MAX_LANES);
        IMPULSE_TESTCASE(k == 0u);
        IMPULSE_TESTCASE(k == IMP_VM_MAX_LANES);

        size_t max_seed = 0;
        for (size_t i = 0; i < seeds_len; ++i) {
            uint64_t s = seeds[i];
            if (s < OP_C0_MAX_SANE_SEED_ID && static_cast<size_t>(s) > max_seed) {
                max_seed = static_cast<size_t>(s);
            }
        }
        size_t n_nodes = resolve_multilane_node_count(vm_state->query_context, -1, max_seed + 1);
        int h_lf = acquire_lane_frontier(vm_state->query_context, n_nodes, k);
        if (h_lf < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        IMPULSE_ASSERT(static_cast<size_t>(h_lf) < VM_MAX_LANE_FRONTIER_HANDLES);

        auto& lf = vm_state->query_context->lane_frontiers[h_lf];
        bool any_set = false;
        size_t bind_len = std::min<size_t>(k, seeds_len);
        for (size_t b = 0; b < bind_len; ++b) {
            uint64_t u = seeds[b];
            if (u < n_nodes) {
                lf.lanes[u] |= IMP_BIT_MASK64(b);
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

