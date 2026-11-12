/**
 * @file op_15_node_filter_str_prefix.inc
 * @brief Formal Contract for OP_NODE_FILTER_STR_PREFIX (0x15).
 *
 * Mnemonic: OP_NODE_FILTER_STR_PREFIX
 * Opcode: 0x15
 * Description: Filters active frontier nodes matching a 4-byte or full string prefix attribute.
 *
 * Operands:
 *  - dst = inst.dst_reg, src = payload & 0xFF, val_reg = (payload >> 8) & 0xFF, attr_id = (payload >> 16) & 0xFF
 *
 * Inputs:
 *  - src: TYPE_BITSET_HANDLE | TYPE_NODE_ID, val_reg: TYPE_INT64 (string pointer/prefix)
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
#ifndef OP_15_CONTRACT_DEFINED
#define OP_15_CONTRACT_DEFINED 1
#endif

// Opcode 0x15: OP_NODE_FILTER_STR_PREFIX
op_NODE_FILTER_STR_PREFIX: {
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
    IMPULSE_CHECK_REG_TYPE(vm_state, val_reg, (IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_NULL)));

    int h_dst = acquire_bitset(vm_state->query_context);
    if (h_dst < 0) {
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
    auto& bs_dst = vm_state->query_context->bitsets[h_dst];
    bs_dst.clear();

    bool src_is_bitset = (vm_state->register_types[src] == TYPE_BITSET_HANDLE);
    if (src_is_bitset) {
        int h_src = static_cast<int>(vm_state->registers[src]);
        bs_dst = vm_state->query_context->bitsets[h_src];
    } else {
        bitset_add(bs_dst, vm_state->registers[src], vm_state->query_context->max_nodes);
    }

    const BoundAttributeSlot* attr_ptr = resolve_node_attribute_slot(vm_state->query_context, rel_id, attr_id);
    bool has_attr = (attr_ptr != nullptr &&
                     ((attr_ptr->type_code & 0x7F) == 0x0B || attr_ptr->offsets_ptr != nullptr));
    const char* prefix = reinterpret_cast<const char*>(vm_state->registers[val_reg]);
    bool valid_prefix = (prefix != nullptr && reinterpret_cast<uintptr_t>(prefix) >= 4096);

    if (has_attr && valid_prefix) {
        const auto& attr = *attr_ptr;
        size_t dom_rows = resolve_domain_row_count(vm_state->query_context, rel_id);
        size_t prefix_len = std::strlen(prefix);
        auto eval_match = [&](uint64_t u) -> bool {
            const char* str_ptr = nullptr;
            size_t str_len = 0;
            if (!extract_var_string_slice(attr, dom_rows, u, &str_ptr, &str_len) || !str_ptr) {
                return false;
            }
            if (prefix_len > str_len) return false;
            return prefix_len == 0 || std::memcmp(str_ptr, prefix, prefix_len) == 0;
        };

        for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
            uint64_t w = bs_dst.words[i];
            if (w == 0) continue;
            for (int b = 0; b < 64; ++b) {
                if (w & (1ULL << b)) {
                    uint64_t u = i * 64 + b;
                    if (!eval_match(u)) {
                        bs_dst.words[i] &= ~(1ULL << b);
                    }
                }
            }
        }
    }

    vm_state->registers[dst] = static_cast<uint64_t>(h_dst);
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

