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
#define OP_14_ALLOWED_SRC_TYPES (IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_BITSET_HANDLE))
#define OP_14_ALLOWED_VAL_TYPES (IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_FLOAT) | IMP_TYPE_BIT(TYPE_NULL))
#endif

// Opcode 0x14: OP_NODE_FILTER
op_NODE_FILTER: {
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
    IMPULSE_CHECK_REG_TYPE(vm_state, src, OP_14_ALLOWED_SRC_TYPES);
    IMPULSE_CHECK_REG_TYPE(vm_state, val_reg, OP_14_ALLOWED_VAL_TYPES);

    int h_dst = acquire_bitset(vm_state->query_context);
    if (h_dst < 0) {
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
    IMPULSE_ASSERT(h_dst >= 0 && static_cast<size_t>(h_dst) < vm_state->query_context->bitsets.size());
    auto& bs_dst = vm_state->query_context->bitsets[h_dst];
    bs_dst.clear();

    bool src_is_bitset = (vm_state->register_types[src] == TYPE_BITSET_HANDLE);
    uint64_t val = vm_state->registers[val_reg];

    const BoundAttributeSlot* attr_ptr = resolve_node_attribute_slot(vm_state->query_context, rel_id, attr_id);
    bool has_attr = (attr_ptr != nullptr);
    size_t dom_rows = resolve_domain_row_count(vm_state->query_context, rel_id);

    const auto* mock_ptr = find_mock_node_attr(vm_state->query_context, attr_id);
    IMPULSE_TESTCASE(src_is_bitset);
    IMPULSE_TESTCASE(mock_ptr != nullptr);
    IMPULSE_TESTCASE(!has_attr);
    if (has_attr) {
        IMPULSE_TESTCASE(IMP_ATTR_IS_NULLABLE(attr_ptr->type_code));
    }

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
        const auto& attr = *attr_ptr;
        const uint8_t base_type = IMP_ATTR_BASE_TYPE(attr.type_code);
        const uint8_t* val_ptr = static_cast<const uint8_t*>(attr.data_ptr);
        uint64_t val_bytes = attr.data_bytes;
        if (IMP_ATTR_IS_NULLABLE(attr.type_code)) {
            const uint64_t raw_bm = IMP_VALIDITY_RAW_BYTES(dom_rows);
            const uint64_t padded_bm = IMP_VALIDITY_PADDED_BYTES(dom_rows);
            if (val_bytes < padded_bm || IMP_VALIDITY_BYTE_OFFSET(u) >= raw_bm) return false;
            if (!IMP_VALIDITY_IS_VALID(val_ptr, u)) return false;
            val_ptr += padded_bm;
            val_bytes -= padded_bm;
        }
        if (base_type == IMP_TYPE_CODE_INT64) {
            if ((u + 1) * sizeof(int64_t) > val_bytes) return false;
            return reinterpret_cast<const int64_t*>(val_ptr)[u] == static_cast<int64_t>(val);
        } else if (base_type == IMP_TYPE_CODE_INT8 || base_type == IMP_TYPE_CODE_INT16) {
            if (dom_rows > 0 && val_bytes >= dom_rows * sizeof(int64_t)) {
                return reinterpret_cast<const int64_t*>(val_ptr)[u] == static_cast<int64_t>(val);
            }
            if (u >= val_bytes) return false;
            if (base_type == IMP_TYPE_CODE_INT8) return val_ptr[u] == static_cast<uint8_t>(val);
            return reinterpret_cast<const int8_t*>(val_ptr)[u] == static_cast<int8_t>(val);
        } else if (base_type == IMP_TYPE_CODE_INT32) {
            if ((u + 1) * sizeof(int32_t) > val_bytes) return false;
            return reinterpret_cast<const int32_t*>(val_ptr)[u] == static_cast<int32_t>(val);
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
            for (uint32_t b = 0; b < IMP_BITS_PER_WORD; ++b) {
                if (w & IMP_BIT_MASK64(b)) {
                    uint64_t u = IMP_WORD_BIT_TO_NODE(i, b);
                    if (eval_match(u)) {
                        w_dst |= IMP_BIT_MASK64(b);
                    }
                }
            }
            bs_dst.words[i] = w_dst;
        }
    } else {
        uint64_t u = vm_state->registers[src];
        if (eval_match(u)) bitset_add(bs_dst, u, vm_state->query_context->max_nodes);
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

