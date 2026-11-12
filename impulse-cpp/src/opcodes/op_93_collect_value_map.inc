#ifndef OP_93_CONTRACT_DEFINED
#define OP_93_CONTRACT_DEFINED
/**
 * @file op_93_collect_value_map.inc
 * @brief Opcode 0x93: OP_COLLECT_VALUE_MAP — Pairs dense nodes with scalar values into a string key-value map.
 *
 * Formal Opcode Contract:
 *  - Opcode: 0x93 (OP_COLLECT_VALUE_MAP)
 *  - Destination: dst_reg (RegFieldLayout::DST_REG)
 *  - Source (Nodes): IMP_INST_U8_0(inst) (RegFieldLayout::PAYLOAD_LOW8)
 *  - Aux1 (Values): IMP_INST_U8_1(inst) (RegFieldLayout::PAYLOAD_BYTE1)
 *  - Domain ID: IMP_INST_HIGH16(inst)
 *  - Allowed Source Types: TYPE_SOURCE_SET (Node ID, Int64, BitSet, Node Vector), TYPE_LANE_FRONTIER
 *  - Allowed Aux1 Types: TYPE_FLOAT_VECTOR, TYPE_DOUBLE_VECTOR
 *  - Output Destination Types: TYPE_VALUE_MAP
 *  - Flags: Sets IMPULSE_VM_FLAG_ZF if resulting value map is empty
 *  - Requirements: Requires valid query_context
 *  - Modifies Destination: True
 */
#define OP_93_DEFINED_FLAGS          0x00u
#define OP_93_ALLOWED_SRC_TYPES      (IMP_TYPES_SOURCE_SET | IMP_TYPE_BIT(TYPE_LANE_FRONTIER) | IMP_TYPE_BIT(TYPE_NULL))
#define OP_93_ALLOWED_AUX1_TYPES     (IMP_TYPES_FLOAT_OR_DBL | IMP_TYPE_BIT(TYPE_NULL))
#define OP_93_OUTPUT_DST_TYPES       (IMP_TYPE_BIT(TYPE_VALUE_MAP))
#define OP_93_FALLBACK_KEY_BUF_LEN   24u
#endif

// Opcode 0x93: OP_COLLECT_VALUE_MAP
op_COLLECT_VALUE_MAP: {
    IMPULSE_ASSERT(vm_state != nullptr);
    const auto& inst = bytecode[vm_state->pc];
    const uint16_t dst = IMP_INST_DST(inst);
    const uint8_t nodes_reg = IMP_INST_U8_0(inst);
    const uint8_t vals_reg = IMP_INST_U8_1(inst);
    const uint16_t domain_id = IMP_INST_HIGH16(inst);
    IMP_VALIDATE_GPR(dst);
    IMP_VALIDATE_GPR(nodes_reg);
    IMP_VALIDATE_GPR(vals_reg);
    IMP_REQUIRE_CONTEXT(vm_state);
    IMPULSE_ASSERT(vm_state->query_context != nullptr);
    IMPULSE_ASSERT(dst < IMP_VM_GPR_COUNT && nodes_reg < IMP_VM_GPR_COUNT && vals_reg < IMP_VM_GPR_COUNT);
    IMPULSE_CHECK_REG_TYPE(vm_state, nodes_reg, OP_93_ALLOWED_SRC_TYPES);
    IMPULSE_CHECK_REG_TYPE(vm_state, vals_reg, OP_93_ALLOWED_AUX1_TYPES);

    const BoundAttributeSlot* attr = find_key_attribute(vm_state, domain_id);
    IMPULSE_TESTCASE(attr != nullptr);

    const int h_dst = acquire_value_map(vm_state->query_context);
    if (h_dst < 0) {
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
    IMPULSE_ASSERT(h_dst >= 0 && static_cast<size_t>(h_dst) < vm_state->query_context->value_maps.size());
    auto& vmap = vm_state->query_context->value_maps[h_dst];
    vmap.keys.clear();
    vmap.values.clear();
    vmap.owned_keys.clear();

    const bool nodes_is_bitset = (vm_state->register_types[nodes_reg] == TYPE_BITSET_HANDLE);
    const bool vals_is_double = (vm_state->register_types[vals_reg] == TYPE_DOUBLE_VECTOR);
    IMPULSE_TESTCASE(nodes_is_bitset);
    IMPULSE_TESTCASE(vals_is_double);

    const size_t max_nodes = vm_state->query_context->max_nodes;
    const size_t dom_rows = resolve_domain_row_count(vm_state->query_context, domain_id);
    const uint8_t base_type = attr ? IMP_ATTR_BASE_TYPE(attr->type_code) : 0u;

    auto add_entry = [&](uint64_t u) {
        const char* str_ptr = nullptr;
        if (attr && attr->data_ptr && base_type == IMP_TYPE_CODE_VAR_STRING) {
            const char* raw_ptr = nullptr;
            size_t raw_len = 0;
            if (extract_var_string_slice(*attr, dom_rows, u, &raw_ptr, &raw_len) && raw_ptr) {
                vmap.owned_keys.emplace_back(raw_ptr, raw_len);
                str_ptr = vmap.owned_keys.back().c_str();
            }
        }

        float val = 0.0f;
        if (vals_is_double) {
            const uint64_t v_h = vm_state->registers[vals_reg];
            if (v_h < VM_MAX_VECTOR_HANDLES && vm_state->query_context->double_vectors_allocated[v_h]) {
                const auto& dvec = vm_state->query_context->double_vectors[v_h];
                if (u < dvec.size()) val = static_cast<float>(dvec[u]);
            }
        } else if (vm_state->register_types[vals_reg] == TYPE_FLOAT_VECTOR) {
            const uint64_t v_h = vm_state->registers[vals_reg];
            if (v_h < VM_MAX_VECTOR_HANDLES && vm_state->query_context->float_vectors_allocated[v_h]) {
                const auto& fvec = vm_state->query_context->float_vectors[v_h];
                if (u < fvec.size()) val = fvec[u];
            }
        }

        if (!str_ptr && vm_state->query_context->snapshot) {
            std::string formatted;
            if (format_snapshot_node_key(vm_state->query_context->snapshot, domain_id, static_cast<uint32_t>(u), formatted) &&
                !formatted.empty()) {
                vmap.owned_keys.push_back(std::move(formatted));
                str_ptr = vmap.owned_keys.back().c_str();
            } else if (vmap.keys.size() < vmap.fallback_keys.size()) {
                char* buf = vmap.fallback_keys[vmap.keys.size()].data();
                std::snprintf(buf, OP_93_FALLBACK_KEY_BUF_LEN, "%llu", static_cast<unsigned long long>(u));
                str_ptr = buf;
            }
        }
        if (str_ptr) {
            vmap.keys.push_back(str_ptr);
            vmap.values.push_back(val);
        }
    };

    if (nodes_is_bitset) {
        const int h_nodes = static_cast<int>(vm_state->registers[nodes_reg]);
        if (h_nodes >= 0 && static_cast<size_t>(h_nodes) < vm_state->query_context->bitsets.size()) {
            const auto& bs_nodes = vm_state->query_context->bitsets[h_nodes];
            for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                uint64_t w = bs_nodes.words[i];
                while (w != 0ULL) {
                    const uint32_t b = static_cast<uint32_t>(std::countr_zero(w));
                    const uint64_t u = IMP_WORD_BIT_TO_NODE(i, b);
                    w = IMP_CLEAR_LOWEST_BIT(w);
                    if (u < max_nodes) add_entry(u);
                }
            }
        }
    } else if (vm_state->register_types[nodes_reg] == TYPE_LANE_FRONTIER) {
        const size_t h_lf = static_cast<size_t>(vm_state->registers[nodes_reg]);
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
                        add_entry(static_cast<uint64_t>(u));
                    }
                }
            }
        }
    } else if (vm_state->register_types[nodes_reg] == TYPE_NODE_VECTOR || vm_state->register_types[nodes_reg] == TYPE_UINT64_VECTOR) {
        const uint64_t* ptr = nullptr;
        size_t len = 0;
        if (get_node_vector_slice(vm_state->query_context, vm_state->registers[nodes_reg], &ptr, &len) && ptr) {
            for (size_t i = 0; i < len; ++i) {
                const uint64_t u = ptr[i];
                if (u != IMP_INVALID_NODE_U64 && u < max_nodes) add_entry(u);
            }
        }
    } else if (vm_state->register_types[nodes_reg] == TYPE_NODE_ID || vm_state->register_types[nodes_reg] == TYPE_INT64) {
        const uint64_t u = vm_state->registers[nodes_reg];
        if (u < max_nodes) add_entry(u);
    }

    vm_state->registers[dst] = static_cast<uint64_t>(h_dst);
    vm_state->register_types[dst] = TYPE_VALUE_MAP;
    IMP_VM_SET_ZF(vm_state, vmap.keys.empty());

    vm_state->pc++;
    DISPATCH();
}
