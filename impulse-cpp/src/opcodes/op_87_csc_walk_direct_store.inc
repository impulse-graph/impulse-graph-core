/**
 * @file op_87_csc_walk_direct_store.inc
 * @brief Formal Contract for OP_CSC_WALK_DIRECT_STORE (0x87).
 *
 * Syntax: OP_CSC_WALK_DIRECT_STORE <dst_reg>, <src_reg> (payload: [rel_id:16 | ... | src:8])
 * Inputs:
 *   - dst_reg: Target register receiving visited incoming source node bitset
 *   - src_reg (payload_low8): Target frontier bitset or scalar node ID
 * Preconditions:
 *   - src_reg must be TYPE_INT64, TYPE_NODE_ID, or TYPE_BITSET_HANDLE
 * Outputs:
 *   - dst_reg set to TYPE_BITSET_HANDLE
 *   - Flags: ZF set if reverse traversal output is empty
 */
#ifndef OP_87_CONTRACT_DEFINED
#define OP_87_CONTRACT_DEFINED
#define OP_87_DEFINED_FLAGS        0x00u
#define OP_87_ALLOWED_SRC_TYPES    (IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_BITSET_HANDLE))
#define OP_87_OUTPUT_DST_TYPES     (IMP_TYPE_BIT(TYPE_BITSET_HANDLE))
#endif

// Opcode 0x87: OP_CSC_WALK_DIRECT_STORE
op_CSC_WALK_DIRECT_STORE: {
    IMPULSE_ASSERT(vm_state != nullptr);
    const auto& inst = bytecode[vm_state->pc];
    const uint16_t dst = IMP_INST_DST(inst);
    const uint16_t src = IMP_INST_U8_0(inst);
    const uint16_t rel = IMP_INST_HIGH16(inst);
    IMP_VALIDATE_GPR(dst);
    IMP_VALIDATE_GPR(src);

    IMP_REQUIRE_CONTEXT(vm_state);
    IMPULSE_ASSERT(vm_state->query_context != nullptr);
    IMPULSE_ASSERT(dst < IMP_VM_GPR_COUNT && src < IMP_VM_GPR_COUNT);
    IMPULSE_CHECK_REG_TYPE(vm_state, src, OP_87_ALLOWED_SRC_TYPES);

    const uint8_t src_type = vm_state->register_types[src];
    const uint64_t src_val = vm_state->registers[src];
    const bool src_is_bitset = (src_type == TYPE_BITSET_HANDLE);
    IMPULSE_TESTCASE(src_is_bitset);
    IMPULSE_TESTCASE(dst == src);
    if (src_is_bitset && static_cast<size_t>(src_val) >= vm_state->query_context->bitsets.size()) {
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }

    int h_dst = -1;
    if (vm_state->register_types[dst] == TYPE_BITSET_HANDLE && !(dst == src && src_is_bitset) &&
        vm_state->registers[dst] < vm_state->query_context->bitsets.size()) {
        h_dst = static_cast<int>(vm_state->registers[dst]);
    } else {
        h_dst = acquire_bitset(vm_state->query_context);
        if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
    IMPULSE_ASSERT(h_dst >= 0 && static_cast<size_t>(h_dst) < vm_state->query_context->bitsets.size());
    auto& bs_dst = vm_state->query_context->bitsets[h_dst];
    bs_dst.clear();

    const bool valid_rel = (rel < vm_state->query_context->slots.size());
    IMPULSE_TESTCASE(!valid_rel);
    if (valid_rel) {
        const auto& slot = vm_state->query_context->slots[rel];
        const bool has_csc = (slot.csc_offsets_ptr != nullptr || !slot.dynamic_csc_offsets.empty());
        IMPULSE_TESTCASE(has_csc);
        if (has_csc) {
            const size_t csc_bound = (slot.csc_node_count > 0) ? slot.csc_node_count : slot.node_count;
            if (src_type == TYPE_NODE_ID || src_type == TYPE_INT64) {
                const uint64_t v = src_val;
                if (v < csc_bound) {
                    const uint64_t start = slot.get_csc_offset(v);
                    const uint64_t end = slot.get_csc_offset(v + 1);
                    IMPULSE_WELL_FORMED_ASSERT(start <= end);
                    for (uint64_t e = start; e < end; ++e) {
                        const uint64_t u = slot.get_csc_target(e);
                        bitset_add(bs_dst, u, vm_state->query_context->max_nodes);
                    }
                }
            } else if (src_is_bitset) {
                const int h_src = static_cast<int>(src_val);
                IMPULSE_ASSERT(h_src >= 0 && static_cast<size_t>(h_src) < vm_state->query_context->bitsets.size());
                const auto& bs_src = vm_state->query_context->bitsets[h_src];
                for (size_t w = 0; w < vm_state->query_context->words_per_bitset; ++w) {
                    uint64_t word = bs_src.words[w];
                    while (word != 0ULL) {
                        const int bit = std::countr_zero(word);
                        const uint64_t v = IMP_WORD_BIT_TO_NODE(w, bit);
                        word = IMP_CLEAR_LOWEST_BIT(word);
                        if (v < csc_bound) {
                            const uint64_t start = slot.get_csc_offset(v);
                            const uint64_t end = slot.get_csc_offset(v + 1);
                            IMPULSE_WELL_FORMED_ASSERT(start <= end);
                            for (uint64_t e = start; e < end; ++e) {
                                const uint64_t u = slot.get_csc_target(e);
                                bitset_add(bs_dst, u, vm_state->query_context->max_nodes);
                            }
                        }
                    }
                }
                if (dst == src) release_bitset(vm_state->query_context, h_src);
            }
        } else if (slot.offsets_ptr && slot.targets_ptr) {
            const size_t N = (slot.node_count > 0) ? slot.node_count : vm_state->query_context->max_nodes;
            if (src_type == TYPE_NODE_ID || src_type == TYPE_INT64) {
                const uint64_t target_v = src_val;
                for (size_t u = 0; u < N; ++u) {
                    const uint64_t start = slot.get_csr_offset(u);
                    const uint64_t end = slot.get_csr_offset(u + 1);
                    IMPULSE_WELL_FORMED_ASSERT(start <= end);
                    for (uint64_t e = start; e < end; ++e) {
                        if (slot.get_csr_target(e) == target_v) {
                            bs_dst.set(u);
                            break;
                        }
                    }
                }
            } else if (src_is_bitset) {
                const int h_src = static_cast<int>(src_val);
                IMPULSE_ASSERT(h_src >= 0 && static_cast<size_t>(h_src) < vm_state->query_context->bitsets.size());
                const auto& bs_src = vm_state->query_context->bitsets[h_src];
                for (size_t u = 0; u < N; ++u) {
                    const uint64_t start = slot.get_csr_offset(u);
                    const uint64_t end = slot.get_csr_offset(u + 1);
                    IMPULSE_WELL_FORMED_ASSERT(start <= end);
                    for (uint64_t e = start; e < end; ++e) {
                        const uint64_t v = slot.get_csr_target(e);
                        if (bs_src.test(v)) {
                            bs_dst.set(u);
                            break;
                        }
                    }
                }
                if (dst == src) release_bitset(vm_state->query_context, h_src);
            }
        }
    }
    vm_state->registers[dst] = static_cast<uint64_t>(h_dst);
    vm_state->register_types[dst] = TYPE_BITSET_HANDLE;

    bool is_empty = true;
    for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
        if (bs_dst.words[i] != 0ULL) {
            is_empty = false;
            break;
        }
    }
    IMP_VM_SET_ZF(vm_state, is_empty);

    vm_state->pc++;
    DISPATCH();
}
