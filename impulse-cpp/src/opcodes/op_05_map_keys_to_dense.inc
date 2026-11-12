/**
 * @file op_05_map_keys_to_dense.inc
 * @brief Formal Contract for OP_MAP_KEYS_TO_DENSE (0x05).
 *
 * Mnemonic: OP_MAP_KEYS_TO_DENSE
 * Opcode: 0x05
 * Description: Maps domain string/integer keys from input to dense node IDs within a BitSet frontier.
 *
 * Operands:
 *  - dst = inst.dst_reg, domain_id = inst.payload & 0xFFFF
 *
 * Inputs:
 *  - input_param: impulse_vm_input_keys*, domain key attribute table
 *
 * Outputs:
 *  - dst: TYPE_BITSET_HANDLE
 *
 * Flags:
 *  - ZF: Set if mapped set is empty, cleared otherwise
 *
 * Context Requirement:
 *  - Requires context (query_context attribute slots and bitset pool)
 */
#ifndef OP_05_CONTRACT_DEFINED
#define OP_05_CONTRACT_DEFINED 1
#define OP_05_INT32_ELEM_BYTES sizeof(int32_t)
#define OP_05_INT64_ELEM_BYTES sizeof(int64_t)
#define OP_05_DECIMAL_RADIX    10
#endif

// Opcode 0x05: OP_MAP_KEYS_TO_DENSE
op_MAP_KEYS_TO_DENSE: {
    const auto& inst = bytecode[vm_state->pc];
    const uint16_t dst = IMP_INST_DST(inst);
    const uint16_t domain_id = IMP_INST_LOW16(inst);
    VALIDATE_REG(dst);
    IMPULSE_ASSERT(dst < IMP_VM_GPR_COUNT);
    IMP_REQUIRE_CONTEXT(vm_state);
    IMPULSE_ASSERT(vm_state->query_context != nullptr);

    if (domain_id >= vm_state->query_context->domain_attribute_slots.size() &&
        domain_id >= vm_state->query_context->attribute_slots.size()) {
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }

    const BoundAttributeSlot* attr = find_key_attribute(vm_state, domain_id);
    IMPULSE_TESTCASE(attr == nullptr);

    int h_dst = acquire_bitset(vm_state->query_context);
    if (h_dst < 0) {
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
    IMPULSE_ASSERT(h_dst >= 0 && static_cast<size_t>(h_dst) < vm_state->query_context->bitsets.size());
    auto& bs_dst = vm_state->query_context->bitsets[h_dst];
    bs_dst.clear();

    IMPULSE_TESTCASE(input_param == 0u);
    if (attr && attr->data_ptr) {
        const impulse_vm_input_keys* input_keys = reinterpret_cast<const impulse_vm_input_keys*>(input_param);
        if (input_keys && input_keys->keys && input_keys->count > 0) {
            const uint8_t base_type = IMP_ATTR_BASE_TYPE(attr->type_code);
            const bool is_nullable = IMP_ATTR_IS_NULLABLE(attr->type_code);
            IMPULSE_TESTCASE(is_nullable);
            IMPULSE_TESTCASE(base_type == IMP_TYPE_CODE_VAR_STRING);
            size_t node_count = resolve_domain_row_count(vm_state->query_context, domain_id);

            for (size_t k = 0; k < input_keys->count; ++k) {
                const char* target_key = input_keys->keys[k];
                if (!target_key) continue;
                size_t target_len = std::strlen(target_key);

                for (size_t u = 0; u < node_count; ++u) {
                    bool is_match = false;
                    if (base_type == IMP_TYPE_CODE_VAR_STRING) {
                        const char* key_val = nullptr;
                        size_t key_len = 0;
                        if (extract_var_string_slice(*attr, node_count, u, &key_val, &key_len)) {
                            if (key_len == target_len && (target_len == 0 || std::memcmp(key_val, target_key, target_len) == 0)) {
                                is_match = true;
                            }
                        }
                    } else if (base_type == IMP_TYPE_CODE_INT32 || base_type == IMP_TYPE_CODE_INT64) {
                        const uint8_t* val_ptr = static_cast<const uint8_t*>(attr->data_ptr);
                        uint64_t val_bytes = attr->data_bytes;
                        if (is_nullable) {
                            const uint64_t padded_bm = IMP_VALIDITY_PADDED_BYTES(node_count);
                            if (val_bytes < padded_bm || !IMP_VALIDITY_IS_VALID(val_ptr, u)) continue;
                            val_ptr += padded_bm;
                            val_bytes -= padded_bm;
                        }
                        const size_t esz = (base_type == IMP_TYPE_CODE_INT32) ? OP_05_INT32_ELEM_BYTES : OP_05_INT64_ELEM_BYTES;
                        if ((u + 1) * esz > val_bytes) continue;
                        int64_t val = (base_type == IMP_TYPE_CODE_INT32)
                            ? static_cast<int64_t>(reinterpret_cast<const int32_t*>(val_ptr)[u])
                            : reinterpret_cast<const int64_t*>(val_ptr)[u];

                        char* endptr = nullptr;
                        int64_t target_val = std::strtoll(target_key, &endptr, OP_05_DECIMAL_RADIX);
                        if (endptr != target_key && val == target_val) {
                            is_match = true;
                        }
                    }

                    if (is_match) {
                        bitset_add(bs_dst, u, vm_state->query_context->max_nodes);
                        break;
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

