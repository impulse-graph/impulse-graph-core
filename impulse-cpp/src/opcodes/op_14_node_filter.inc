/**
 * @file op_14_node_filter.inc
 * @brief Formal Contract for OP_NODE_FILTER (0x14).
 *
 * Mnemonic: OP_NODE_FILTER
 * Opcode: 0x14
 * Description: Filters active frontier nodes against a scalar attribute comparison.
 *
 * Operands:
 *  - dst = inst.dst_reg, src = payload & 0xFF, val_reg = (payload >> 8) & 0xFF, attr_id = (payload >> 16) & 0xFF
 *
 * Inputs:
 *  - src: TYPE_BITSET_HANDLE | TYPE_NODE_ID, val_reg: TYPE_INT64 | TYPE_FLOAT
 *
 * Outputs:
 *  - dst: TYPE_BITSET_HANDLE
 *
 * Flags:
 *  - ZF: Set if filtered set is empty
 *
 * Context Requirement:
 *  - Requires context (query_context attribute slots and bitset pool)
 */
#ifndef OP_14_CONTRACT_DEFINED
#define OP_14_CONTRACT_DEFINED 1
#endif

// Opcode 0x14: OP_NODE_FILTER
op_NODE_FILTER: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint8_t src = inst.payload & 0xFF;
    uint8_t val_reg = (inst.payload >> 8) & 0xFF;
    uint8_t attr_id = (inst.payload >> 16) & 0xFF;
    uint8_t rel_id = (inst.payload >> 24) & 0xFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src);
    VALIDATE_REG(val_reg);
    if (!vm_state->query_context) return IMPULSE_VM_ERR_NULL_SNAPSHOT;
    IMPULSE_CHECK_REG_TYPE(vm_state, src, (IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_BITSET_HANDLE)));
    IMPULSE_CHECK_REG_TYPE(vm_state, val_reg, (IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_FLOAT)));

    int h_dst = acquire_bitset(vm_state->query_context);
    if (h_dst < 0) {
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
    auto& bs_dst = vm_state->query_context->bitsets[h_dst];
    bs_dst.clear();

    bool src_is_bitset = (vm_state->register_types[src] == TYPE_BITSET_HANDLE);
    uint64_t val = vm_state->registers[val_reg];

    bool has_attr = (rel_id < vm_state->query_context->attribute_slots.size() &&
                     attr_id < vm_state->query_context->attribute_slots[rel_id].size() &&
                     vm_state->query_context->attribute_slots[rel_id][attr_id].data_ptr != nullptr);

    const auto* mock_ptr = find_mock_node_attr(vm_state->query_context, attr_id);
    auto eval_match = [&](uint64_t u) -> bool {
        if (mock_ptr) {
            const auto& mock = *mock_ptr;
            if (mock.has_mask) {
                if (!mock.mask.test(u)) return false;
            }
            if (!mock.int_data.empty()) {
                if (u >= mock.int_data.size()) return false;
                return mock.int_data[u] == val;
            }
            if (!mock.float_data.empty()) {
                if (u >= mock.float_data.size()) return false;
                float expected_f = *reinterpret_cast<const float*>(&val);
                return mock.float_data[u] == expected_f;
            }
            return false;
        }
        if (!has_attr) return true;
        const auto& attr = vm_state->query_context->attribute_slots[rel_id][attr_id];
        uint8_t base_type = attr.type_code & 0x7F;
        if (base_type == 0x01 || base_type == 0x02) {
            const int64_t* data = static_cast<const int64_t*>(attr.data_ptr);
            return data[u] == static_cast<int64_t>(val);
        } else if (base_type == 0x03) {
            const int32_t* data = static_cast<const int32_t*>(attr.data_ptr);
            return data[u] == static_cast<int32_t>(val);
        }
        return true;
    };

    if (src_is_bitset) {
        int h_src = static_cast<int>(vm_state->registers[src]);
        const auto& bs_src = vm_state->query_context->bitsets[h_src];
#if defined(_OPENMP)
        #pragma omp parallel for schedule(dynamic, 1024)
#endif
        for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
            uint64_t w = bs_src.words[i];
            if (w == 0) continue;
            uint64_t w_dst = 0;
            for (int b = 0; b < 64; ++b) {
                if (w & (1ULL << b)) {
                    uint64_t u = i * 64 + b;
                    if (eval_match(u)) {
                        w_dst |= (1ULL << b);
                    }
                }
            }
            bs_dst.words[i] = w_dst;
        }
    } else {
        uint64_t u = vm_state->registers[src];
        if (eval_match(u)) bitset_add(bs_dst, u, vm_state->query_context->max_nodes);
    }

    vm_state->registers[dst] = h_dst;
    vm_state->register_types[dst] = TYPE_BITSET_HANDLE;

    bool is_empty = true;
    for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
        if (bs_dst.words[i] != 0) { is_empty = false; break; }
    }
    if (is_empty) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
    else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

    vm_state->pc++;
    DISPATCH();
}

