/**
 * @file op_11_csr_walk_filtered.inc
 * @brief Formal Contract for OP_CSR_WALK_FILTERED (0x11).
 *
 * Mnemonic: OP_CSR_WALK_FILTERED
 * Opcode: 0x11
 * Description: CSR forward traversal with target-side in-flight BitSet membership filter.
 *
 * Operands:
 *  - dst = inst.dst_reg, src = payload & 0xFF, filter = (payload >> 8) & 0xFF, rel = payload >> 16
 *
 * Inputs:
 *  - src: SOURCE_SET, filter: TYPE_BITSET_HANDLE
 *
 * Outputs:
 *  - dst: TYPE_BITSET_HANDLE
 *
 * Flags:
 *  - ZF: Set if filtered frontier is empty
 *
 * Context Requirement:
 *  - Requires context (query_context snapshot slots and bitset pool)
 */
#ifndef OP_11_CONTRACT_DEFINED
#define OP_11_CONTRACT_DEFINED 1
#define OP_11_ALLOWED_FILTER_TYPES (IMP_TYPE_BIT(TYPE_BITSET_HANDLE) | IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_NULL))
#define OP_11_IS_VALID_TARGET(t)   ((t) != IMP_SENTINEL_U32 && (t) != IMP_SENTINEL_U16 && (t) != IMP_INVALID_NODE_U64)
#endif

// Opcode 0x11: OP_CSR_WALK_FILTERED
op_CSR_WALK_FILTERED: {
    const auto& inst = bytecode[vm_state->pc];
    const uint16_t dst = IMP_INST_DST(inst);
    const uint16_t src = IMP_INST_U8_0(inst);
    uint16_t filter_reg = IMP_INST_U8_1(inst);
    uint16_t rel;
    IMPULSE_TESTCASE(IMP_INST_IS_EXTENDED(inst));
    if (IMP_INST_IS_EXTENDED(inst)) {
        if (vm_state->pc + 1 >= instruction_count) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        rel = IMP_INST_DST(bytecode[vm_state->pc + 1]);
        vm_state->pc++;
    } else {
        rel = IMP_INST_HIGH16(inst);
    }
    if (!IMP_INST_IS_EXTENDED(inst) && filter_reg == 0 && rel > 0 && rel < IMP_VM_GPR_COUNT && vm_state->register_types[rel] == TYPE_BITSET_HANDLE) {
        filter_reg = rel;
        rel = 0;
    }
    VALIDATE_REG(dst);
    VALIDATE_REG(src);
    VALIDATE_REG(filter_reg);
    IMPULSE_ASSERT(dst < IMP_VM_GPR_COUNT && src < IMP_VM_GPR_COUNT && filter_reg < IMP_VM_GPR_COUNT);
    IMP_REQUIRE_CONTEXT(vm_state);
    IMPULSE_ASSERT(vm_state->query_context != nullptr);
    IMPULSE_CHECK_REG_TYPE(vm_state, src, IMP_TYPES_SOURCE_SET);
    IMPULSE_CHECK_REG_TYPE(vm_state, filter_reg, OP_11_ALLOWED_FILTER_TYPES);

    IMP_REQUIRE_REL_SLOT(vm_state, rel);
    const auto& slot = vm_state->query_context->slots[rel];

    int h_dst = -1;
    if (vm_state->register_types[dst] == TYPE_BITSET_HANDLE && dst != src && dst != filter_reg) {
        h_dst = static_cast<int>(vm_state->registers[dst]);
        vm_state->query_context->bitsets[h_dst].clear();
    } else {
        h_dst = acquire_bitset(vm_state->query_context);
        if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        IMPULSE_ASSERT(h_dst >= 0 && static_cast<size_t>(h_dst) < vm_state->query_context->bitsets.size());
        vm_state->query_context->bitsets[h_dst].clear();
    }
    auto& bs_dst = vm_state->query_context->bitsets[h_dst];

    bool src_is_null = (vm_state->register_types[src] == TYPE_NULL && !IMP_INST_HAS_FLAG(inst, IMPULSE_VM_OP_FLAG_INPUT_SEED));
    bool src_is_bitset = (vm_state->register_types[src] == TYPE_BITSET_HANDLE);
    int h_src = src_is_bitset ? static_cast<int>(vm_state->registers[src]) : -1;
    if (src_is_bitset) {
        IMP_REQUIRE_BITSET_HANDLE(vm_state, h_src);
    }
    uint64_t scalar_src = !src_is_bitset ? vm_state->registers[src] : 0;

    const uint64_t* filter_words = nullptr;
    uint64_t scalar_filter = IMP_INVALID_NODE_U64;
    if (vm_state->register_types[filter_reg] == TYPE_BITSET_HANDLE) {
        int h_filt = static_cast<int>(vm_state->registers[filter_reg]);
        if (h_filt >= 0 && static_cast<size_t>(h_filt) < VM_MAX_BITSET_HANDLES) {
            filter_words = vm_state->query_context->bitsets[h_filt].words;
        }
    } else if (vm_state->register_types[filter_reg] == TYPE_NODE_ID || vm_state->register_types[filter_reg] == TYPE_INT64) {
        scalar_filter = vm_state->registers[filter_reg];
    }
    IMPULSE_TESTCASE(src_is_bitset);
    IMPULSE_TESTCASE(filter_words != nullptr);
    IMPULSE_TESTCASE(scalar_filter != IMP_INVALID_NODE_U64);
    uint64_t max_nodes = vm_state->query_context->max_nodes;

    auto target_matches = [&](uint64_t target_node) -> bool {
        if (filter_words) {
            return (target_node < max_nodes && (filter_words[IMP_WORD_INDEX(target_node)] & IMP_BIT_MASK64(target_node)) != 0ULL);
        }
        if (scalar_filter != IMP_INVALID_NODE_U64) {
            return target_node == scalar_filter;
        }
        return false;
    };

    if (!src_is_null && slot.offsets_ptr && slot.targets_ptr) {
        if (src_is_bitset) {
            #pragma omp parallel for schedule(dynamic, 64)
            for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                uint64_t w = vm_state->query_context->bitsets[h_src].words[i];
                if (w == 0) continue;
                for (uint32_t b = 0; b < IMP_BITS_PER_WORD; ++b) {
                    if (w & IMP_BIT_MASK64(b)) {
                        uint64_t u = IMP_WORD_BIT_TO_NODE(i, b);
                        if (u < slot.node_count) {
                            uint64_t start = slot.get_csr_offset(u);
                            uint64_t end   = slot.get_csr_offset(u + 1);
                            IMPULSE_WELL_FORMED_ASSERT(start <= end);
                            for (uint64_t idx = start; idx < end; ++idx) {
                                uint64_t target_node = slot.get_csr_target(idx);
                                if (OP_11_IS_VALID_TARGET(target_node)) {
                                    if (target_matches(target_node)) {
                                        bitset_add_atomic(bs_dst, target_node, max_nodes);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        } else {
            uint64_t u = scalar_src;
            if (u < slot.node_count) {
                uint64_t start = slot.get_csr_offset(u);
                uint64_t end   = slot.get_csr_offset(u + 1);
                IMPULSE_WELL_FORMED_ASSERT(start <= end);
                IMPULSE_TESTCASE(start == end);
                for (uint64_t idx = start; idx < end; ++idx) {
                    uint64_t target_node = slot.get_csr_target(idx);
                    if (OP_11_IS_VALID_TARGET(target_node)) {
                        if (target_matches(target_node)) {
                            bitset_add(bs_dst, target_node, max_nodes);
                        }
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

