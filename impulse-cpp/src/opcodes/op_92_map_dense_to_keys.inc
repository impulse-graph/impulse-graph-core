#ifndef OP_92_CONTRACT_DEFINED
#define OP_92_CONTRACT_DEFINED
/**
 * @file op_92_map_dense_to_keys.inc
 * @brief Opcode 0x92: OP_MAP_DENSE_TO_KEYS — Maps dense node IDs to string catalog keys.
 *
 * Formal Opcode Contract:
 *  - Opcode: 0x92 (OP_MAP_DENSE_TO_KEYS)
 *  - Destination: dst_reg (RegFieldLayout::DST_REG)
 *  - Source: IMP_INST_U8_0(inst) (RegFieldLayout::PAYLOAD_LOW8)
 *  - Domain ID: OP_92_EXTRACT_DOMAIN_ID(inst) ((payload >> 8) & 0xFFFF)
 *  - Allowed Source Types: TYPE_SOURCE_SET (Node ID, Int64, BitSet, Node Vector), TYPE_LANE_FRONTIER
 *  - Output Destination Types: TYPE_STRING_VECTOR
 *  - Flags: Sets IMPULSE_VM_FLAG_ZF if result string vector is empty
 *  - Requirements: Requires valid query_context
 *  - Modifies Destination: True
 */
#define OP_92_DEFINED_FLAGS        0x00u
#define OP_92_ALLOWED_SRC_TYPES    (IMP_TYPES_SOURCE_SET | IMP_TYPE_BIT(TYPE_LANE_FRONTIER) | IMP_TYPE_BIT(TYPE_NULL))
#define OP_92_OUTPUT_DST_TYPES     (IMP_TYPE_BIT(TYPE_STRING_VECTOR))
#define OP_92_DOMAIN_ID_SHIFT      8u
#define OP_92_DOMAIN_ID_MASK       0xFFFFu
#define OP_92_EXTRACT_DOMAIN_ID(inst) \
    static_cast<uint16_t>(((inst).payload >> OP_92_DOMAIN_ID_SHIFT) & OP_92_DOMAIN_ID_MASK)
#endif

// Opcode 0x92: OP_MAP_DENSE_TO_KEYS
op_MAP_DENSE_TO_KEYS: {
    IMPULSE_ASSERT(vm_state != nullptr);
    const auto& inst = bytecode[vm_state->pc];
    const uint16_t dst = IMP_INST_DST(inst);
    const uint8_t src = IMP_INST_U8_0(inst);
    const uint16_t domain_id = OP_92_EXTRACT_DOMAIN_ID(inst);
    IMP_VALIDATE_GPR(dst);
    IMP_VALIDATE_GPR(src);
    IMP_REQUIRE_CONTEXT(vm_state);
    IMPULSE_ASSERT(vm_state->query_context != nullptr);
    IMPULSE_ASSERT(dst < IMP_VM_GPR_COUNT && src < IMP_VM_GPR_COUNT);
    IMPULSE_CHECK_REG_TYPE(vm_state, src, OP_92_ALLOWED_SRC_TYPES);

    const BoundAttributeSlot* attr = find_key_attribute(vm_state, domain_id);
    IMPULSE_TESTCASE(attr != nullptr);

    const int h_dst = acquire_string_vector(vm_state->query_context);
    if (h_dst < 0) {
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
    IMPULSE_ASSERT(h_dst >= 0 && static_cast<size_t>(h_dst) < vm_state->query_context->string_vectors.size());
    auto& svec = vm_state->query_context->string_vectors[h_dst];
    auto& sowned = vm_state->query_context->string_vector_owned[h_dst];
    svec.clear();
    sowned.clear();

    const bool src_is_bitset = (vm_state->register_types[src] == TYPE_BITSET_HANDLE);
    IMPULSE_TESTCASE(src_is_bitset);
    const uint8_t base_type = attr ? IMP_ATTR_BASE_TYPE(attr->type_code) : 0u;
    const size_t max_nodes = vm_state->query_context->max_nodes;
    const size_t dom_rows = resolve_domain_row_count(vm_state->query_context, domain_id);

    auto add_key = [&](uint64_t u) {
        if (vm_state->query_context->snapshot) {
            std::string formatted;
            if (format_snapshot_node_key(vm_state->query_context->snapshot, domain_id, static_cast<uint32_t>(u), formatted)) {
                sowned.push_back(std::move(formatted));
                svec.push_back(sowned.back().c_str());
                return;
            }
        }
        if (attr && attr->data_ptr && base_type == IMP_TYPE_CODE_VAR_STRING) {
            const char* str_ptr = nullptr;
            size_t str_len = 0;
            if (extract_var_string_slice(*attr, dom_rows, u, &str_ptr, &str_len) && str_ptr) {
                sowned.emplace_back(str_ptr, str_len);
                svec.push_back(sowned.back().c_str());
                return;
            }
        }
        if (vm_state->query_context->snapshot) {
            sowned.push_back(std::to_string(u));
            svec.push_back(sowned.back().c_str());
        }
    };

    if (src_is_bitset) {
        const int h_src = static_cast<int>(vm_state->registers[src]);
        if (h_src >= 0 && static_cast<size_t>(h_src) < vm_state->query_context->bitsets.size()) {
            const auto& bs_src = vm_state->query_context->bitsets[h_src];
            for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                uint64_t w = bs_src.words[i];
                while (w != 0ULL) {
                    const uint32_t b = static_cast<uint32_t>(std::countr_zero(w));
                    const uint64_t u = IMP_WORD_BIT_TO_NODE(i, b);
                    w = IMP_CLEAR_LOWEST_BIT(w);
                    if (u < max_nodes) add_key(u);
                }
            }
        }
    } else if (vm_state->register_types[src] == TYPE_LANE_FRONTIER) {
        const size_t h_lf = static_cast<size_t>(vm_state->registers[src]);
        if (h_lf < VM_MAX_LANE_FRONTIER_HANDLES && h_lf < vm_state->query_context->lane_frontiers_count) {
            const auto& lf = vm_state->query_context->lane_frontiers[h_lf];
            for (size_t w_idx = 0; w_idx < lf.active_words.size(); ++w_idx) {
                uint64_t word = lf.active_words[w_idx];
                while (word != 0ULL) {
                    const uint32_t bit = static_cast<uint32_t>(std::countr_zero(word));
                    const size_t u = static_cast<size_t>(IMP_WORD_BIT_TO_NODE(w_idx, bit));
                    word = IMP_CLEAR_LOWEST_BIT(word);
                    if (u >= lf.lanes.size()) break;
                    if ((lf.lanes[u] & lf.active_mask) != 0ULL && u < max_nodes) {
                        add_key(static_cast<uint64_t>(u));
                    }
                }
            }
        }
    } else if (vm_state->register_types[src] == TYPE_NODE_VECTOR || vm_state->register_types[src] == TYPE_UINT64_VECTOR) {
        const uint64_t* ptr = nullptr;
        size_t len = 0;
        if (get_node_vector_slice(vm_state->query_context, vm_state->registers[src], &ptr, &len) && ptr) {
            for (size_t i = 0; i < len; ++i) {
                const uint64_t u = ptr[i];
                if (u != IMP_INVALID_NODE_U64 && u < max_nodes) add_key(u);
            }
        }
    } else if (vm_state->register_types[src] == TYPE_NODE_ID || vm_state->register_types[src] == TYPE_INT64) {
        const uint64_t u = vm_state->registers[src];
        if (u < max_nodes) add_key(u);
    }

    vm_state->registers[dst] = static_cast<uint64_t>(h_dst);
    vm_state->register_types[dst] = TYPE_STRING_VECTOR;
    IMP_VM_SET_ZF(vm_state, svec.empty());

    vm_state->pc++;
    DISPATCH();
}
