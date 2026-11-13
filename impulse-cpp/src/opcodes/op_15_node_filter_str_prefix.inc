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
#define OP_15_MIN_VALID_USER_PTR 4096u
#define OP_15_ALLOWED_SRC_TYPES  (IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_BITSET_HANDLE))
#define OP_15_ALLOWED_VAL_TYPES  (IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_NULL))
#endif

// Opcode 0x15: OP_NODE_FILTER_STR_PREFIX
op_NODE_FILTER_STR_PREFIX: {
    const auto& inst = bytecode[vm_state->pc];
    const uint16_t dst = IMP_INST_DST(inst);
    const uint8_t src = IMP_INST_U8_0(inst);
    const uint8_t val_reg = IMP_INST_U8_1(inst);
    const uint8_t attr_id = IMP_INST_U8_2(inst);
    const uint8_t rel_id = IMP_INST_U8_3(inst);
    VALIDATE_REG(dst);
    VALIDATE_REG(src);
    VALIDATE_REG(val_reg);
    IMPULSE_ASSERT(dst < IMP_VM_GPR_COUNT && src < IMP_VM_GPR_COUNT && val_reg < IMP_VM_GPR_COUNT);
    IMP_REQUIRE_CONTEXT(vm_state);
    IMPULSE_ASSERT(vm_state->query_context != nullptr);
    IMPULSE_CHECK_REG_TYPE(vm_state, src, OP_15_ALLOWED_SRC_TYPES);
    IMPULSE_CHECK_REG_TYPE(vm_state, val_reg, OP_15_ALLOWED_VAL_TYPES);
    if (((1ULL << vm_state->register_types[src]) & OP_15_ALLOWED_SRC_TYPES) == 0) {
        return IMPULSE_VM_ERR_INVALID_REGISTER;
    }

    int h_dst = acquire_bitset(vm_state->query_context);
    if (h_dst < 0) {
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
    IMPULSE_ASSERT(h_dst >= 0 && static_cast<size_t>(h_dst) < vm_state->query_context->bitsets.size());
    auto& bs_dst = vm_state->query_context->bitsets[h_dst];
    bs_dst.clear();

    bool src_is_bitset = (vm_state->register_types[src] == TYPE_BITSET_HANDLE);
    IMPULSE_TESTCASE(src_is_bitset);
    if (src_is_bitset) {
        int h_src = static_cast<int>(vm_state->registers[src]);
        bs_dst = vm_state->query_context->bitsets[h_src];
    } else {
        bitset_add(bs_dst, vm_state->registers[src], vm_state->query_context->max_nodes);
    }

    const BoundAttributeSlot* attr_ptr = resolve_node_attribute_slot(vm_state->query_context, rel_id, attr_id);
    bool has_attr = (attr_ptr != nullptr &&
                     (IMP_ATTR_BASE_TYPE(attr_ptr->type_code) == IMP_TYPE_CODE_VAR_STRING || attr_ptr->offsets_ptr != nullptr));
    const char* prefix = reinterpret_cast<const char*>(vm_state->registers[val_reg]);
    bool valid_prefix = (prefix != nullptr && reinterpret_cast<uintptr_t>(prefix) >= OP_15_MIN_VALID_USER_PTR);
    IMPULSE_TESTCASE(!has_attr);
    IMPULSE_TESTCASE(!valid_prefix);

    if (has_attr && valid_prefix) {
        const auto& attr = *attr_ptr;
        size_t dom_rows = resolve_domain_row_count(vm_state->query_context, rel_id);
        size_t prefix_len = std::strlen(prefix);
        IMPULSE_TESTCASE(prefix_len == 0u);
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
            for (uint32_t b = 0; b < IMP_BITS_PER_WORD; ++b) {
                if (w & IMP_BIT_MASK64(b)) {
                    uint64_t u = IMP_WORD_BIT_TO_NODE(i, b);
                    if (!eval_match(u)) {
                        bs_dst.words[i] &= ~IMP_BIT_MASK64(b);
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
    IMP_VM_SET_ZF(vm_state, is_empty);

    vm_state->pc++;
    DISPATCH();
}

