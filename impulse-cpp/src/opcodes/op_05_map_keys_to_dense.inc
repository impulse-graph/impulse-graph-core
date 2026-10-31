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
#endif

// Opcode 0x05: OP_MAP_KEYS_TO_DENSE
op_MAP_KEYS_TO_DENSE: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t domain_id = inst.payload & 0xFFFF;
    VALIDATE_REG(dst);
    if (!vm_state->query_context) return IMPULSE_VM_ERR_NULL_SNAPSHOT;
    if (domain_id >= vm_state->query_context->attribute_slots.size()) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;

    const BoundAttributeSlot* attr = find_key_attribute(vm_state, domain_id);

    int h_dst = acquire_bitset(vm_state->query_context);
    if (h_dst < 0) {
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
    auto& bs_dst = vm_state->query_context->bitsets[h_dst];
    bs_dst.clear();

    if (attr && attr->data_ptr) {
        const impulse_vm_input_keys* input_keys = reinterpret_cast<const impulse_vm_input_keys*>(input_param);
        if (input_keys && input_keys->keys && input_keys->count > 0) {
            uint8_t base_type = attr->type_code & 0x7F;
            size_t node_count = vm_state->query_context->max_nodes;

            for (size_t k = 0; k < input_keys->count; ++k) {
                const char* target_key = input_keys->keys[k];
                if (!target_key) continue;

                for (size_t u = 0; u < node_count; ++u) {
                    bool is_match = false;
                    if (base_type == 0x0B) {
                        const char* key_val = nullptr;
                        if (!attr->offsets_ptr) {
                            key_val = static_cast<const char*>(attr->data_ptr) + u * attr->dimension;
                        } else {
                            const uint32_t* offsets = static_cast<const uint32_t*>(attr->offsets_ptr);
                            key_val = static_cast<const char*>(attr->data_ptr) + offsets[u];
                        }
                        if (key_val) {
                            if (!attr->offsets_ptr) {
                                size_t target_len = std::strlen(target_key);
                                size_t match_len = std::min<size_t>(target_len, static_cast<size_t>(attr->dimension));
                                if (std::strncmp(key_val, target_key, match_len) == 0 &&
                                    (target_len <= static_cast<size_t>(attr->dimension) || key_val[match_len] == '\0')) {
                                    is_match = true;
                                }
                            } else if (std::strcmp(key_val, target_key) == 0) {
                                is_match = true;
                            }
                        }
                    } else if (base_type == 0x03 || base_type == 0x04) {
                        int64_t val = 0;
                        if (base_type == 0x03) val = static_cast<const int32_t*>(attr->data_ptr)[u];
                        else val = static_cast<const int64_t*>(attr->data_ptr)[u];
                        
                        char* endptr = nullptr;
                        int64_t target_val = std::strtoll(target_key, &endptr, 10);
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

