/**
 * @file op_8b_coo_walk_filtered.inc
 * @brief Formal Contract for OP_COO_WALK_FILTERED (0x8B).
 *
 * Syntax: OP_COO_WALK_FILTERED <dst_reg>, <src_reg>, <filter_reg> (payload: [rel:16 | filter:8 | src:8])
 * Inputs:
 *   - dst_reg: Target register receiving filtered target nodes bitset
 *   - src_reg (payload_low8): Source frontier bitset or scalar node ID
 *   - filter_reg (payload_byte1): Allowed target node filter bitset or scalar node ID
 * Preconditions:
 *   - Both src_reg and filter_reg must be TYPE_INT64, TYPE_NODE_ID, or TYPE_BITSET_HANDLE
 * Outputs:
 *   - dst_reg set to TYPE_BITSET_HANDLE
 *   - Flags: ZF set if filtered output is empty
 */
#ifndef OP_8B_CONTRACT_DEFINED
#define OP_8B_CONTRACT_DEFINED
#define OP_8B_DEFINED_FLAGS        0x00u
#define OP_8B_ALLOWED_SRC_TYPES    (IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_BITSET_HANDLE) | IMP_TYPE_BIT(TYPE_NULL))
#define OP_8B_ALLOWED_AUX1_TYPES   (IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_BITSET_HANDLE) | IMP_TYPE_BIT(TYPE_NULL))
#define OP_8B_OUTPUT_DST_TYPES     (IMP_TYPE_BIT(TYPE_BITSET_HANDLE))
#endif

// Opcode 0x8B: OP_COO_WALK_FILTERED
op_COO_WALK_FILTERED: {
    IMPULSE_ASSERT(vm_state != nullptr);
    const auto& inst = bytecode[vm_state->pc];
    const uint16_t dst = IMP_INST_DST(inst);
    const uint16_t src = IMP_INST_U8_0(inst);
    const uint16_t filter_reg = IMP_INST_U8_1(inst);
    const uint16_t rel = IMP_INST_HIGH16(inst);
    IMP_VALIDATE_GPR(dst);
    IMP_VALIDATE_GPR(src);
    IMP_VALIDATE_GPR(filter_reg);

    IMP_REQUIRE_CONTEXT(vm_state);
    IMPULSE_ASSERT(vm_state->query_context != nullptr);
    IMPULSE_ASSERT(dst < IMP_VM_GPR_COUNT && src < IMP_VM_GPR_COUNT && filter_reg < IMP_VM_GPR_COUNT);
    IMPULSE_CHECK_REG_TYPE(vm_state, src, OP_8B_ALLOWED_SRC_TYPES);
    IMPULSE_CHECK_REG_TYPE(vm_state, filter_reg, OP_8B_ALLOWED_AUX1_TYPES);

    const bool src_is_null = (vm_state->register_types[src] == TYPE_NULL);
    const bool src_is_bitset = (vm_state->register_types[src] == TYPE_BITSET_HANDLE);
    const uint64_t scalar_src = vm_state->registers[src];
    const uint64_t* src_words = nullptr;
    if (src_is_bitset) {
        const int h = static_cast<int>(scalar_src);
        if (h < 0 || static_cast<size_t>(h) >= vm_state->query_context->bitsets.size()) {
            return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        }
        src_words = vm_state->query_context->bitsets[h].words;
    }

    const bool flt_is_null = (vm_state->register_types[filter_reg] == TYPE_NULL);
    const bool flt_is_bitset = (vm_state->register_types[filter_reg] == TYPE_BITSET_HANDLE);
    const uint64_t scalar_flt = vm_state->registers[filter_reg];
    const uint64_t* flt_words = nullptr;
    if (flt_is_bitset) {
        const int h = static_cast<int>(scalar_flt);
        if (h < 0 || static_cast<size_t>(h) >= vm_state->query_context->bitsets.size()) {
            return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        }
        flt_words = vm_state->query_context->bitsets[h].words;
    }

    IMPULSE_TESTCASE(src_is_bitset);
    IMPULSE_TESTCASE(flt_is_bitset);
    IMPULSE_TESTCASE(src_is_null);
    IMPULSE_TESTCASE(flt_is_null);

    int h_dst = -1;
    if (vm_state->register_types[dst] == TYPE_BITSET_HANDLE &&
        !(dst == src && src_is_bitset) && !(dst == filter_reg && flt_is_bitset) &&
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
        if (slot.offsets_ptr && slot.targets_ptr) {
            const size_t N = (slot.node_count > 0) ? slot.node_count : vm_state->query_context->max_nodes;
            if (src_is_bitset) {
                IMPULSE_ASSERT(src_words != nullptr);
                #pragma omp parallel for schedule(static)
                for (size_t u = 0; u < N; ++u) {
                    if ((src_words[IMP_WORD_INDEX(u)] & IMP_BIT_MASK64(u)) != 0ULL) {
                        const uint64_t start = slot.get_csr_offset(u);
                        const uint64_t end = slot.get_csr_offset(u + 1);
                        IMPULSE_WELL_FORMED_ASSERT(start <= end);
                        for (uint64_t e = start; e < end; ++e) {
                            const uint64_t v = slot.get_csr_target(e);
                            if (v >= vm_state->query_context->max_nodes) continue;
                            const bool match = flt_words
                                ? ((flt_words[IMP_WORD_INDEX(v)] & IMP_BIT_MASK64(v)) != 0ULL)
                                : (!flt_is_null && v == scalar_flt);
                            if (match) {
                                #pragma omp atomic
                                bs_dst.words[IMP_WORD_INDEX(v)] |= IMP_BIT_MASK64(v);
                            }
                        }
                    }
                }
            } else if (!src_is_null && scalar_src < N) {
                const uint64_t start = slot.get_csr_offset(scalar_src);
                const uint64_t end = slot.get_csr_offset(scalar_src + 1);
                IMPULSE_WELL_FORMED_ASSERT(start <= end);
                for (uint64_t e = start; e < end; ++e) {
                    const uint64_t v = slot.get_csr_target(e);
                    if (v >= vm_state->query_context->max_nodes) continue;
                    const bool match = flt_words
                        ? ((flt_words[IMP_WORD_INDEX(v)] & IMP_BIT_MASK64(v)) != 0ULL)
                        : (!flt_is_null && v == scalar_flt);
                    if (match) {
                        bitset_add(bs_dst, v, vm_state->query_context->max_nodes);
                    }
                }
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
