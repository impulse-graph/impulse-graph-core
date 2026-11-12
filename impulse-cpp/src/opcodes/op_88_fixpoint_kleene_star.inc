/**
 * @file op_88_fixpoint_kleene_star.inc
 * @brief Formal Contract for OP_FIXPOINT_KLEENE_STAR (0x88).
 *
 * Syntax: OP_FIXPOINT_KLEENE_STAR <dst_reg>, <src_reg> (payload: [rel_id:16 | ... | src:8])
 * Inputs:
 *   - dst_reg: Destination bitset receiving transitive closure reachable set (R*)
 *   - src_reg (payload_low8): Seed node ID or frontier bitset
 * Preconditions:
 *   - src_reg must be TYPE_INT64, TYPE_NODE_ID, or TYPE_BITSET_HANDLE
 * Outputs:
 *   - dst_reg set to TYPE_BITSET_HANDLE
 *   - Flags: ZF set if reachable set is empty
 */
#ifndef OP_88_CONTRACT_DEFINED
#define OP_88_CONTRACT_DEFINED
#define OP_88_DEFINED_FLAGS        0x00u
#define OP_88_ALLOWED_SRC_TYPES    (IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_BITSET_HANDLE))
#define OP_88_OUTPUT_DST_TYPES     (IMP_TYPE_BIT(TYPE_BITSET_HANDLE))
#define OP_88_DEFAULT_MAX_HOPS     1000u
#endif

// Opcode 0x88: OP_FIXPOINT_KLEENE_STAR
op_FIXPOINT_KLEENE_STAR: {
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
    IMPULSE_CHECK_REG_TYPE(vm_state, src, OP_88_ALLOWED_SRC_TYPES);

    IMP_REQUIRE_REL_SLOT(vm_state, rel);
    const auto& slot = vm_state->query_context->slots[rel];
    if (!vm_state->query_context->snapshot && !slot.offsets_ptr && !slot.targets_ptr) {
        return IMPULSE_VM_ERR_NULL_SNAPSHOT;
    }

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
    auto& reached = vm_state->query_context->bitsets[h_dst];
    reached.clear();

    const size_t words = vm_state->query_context->words_per_bitset;
    const size_t N = (slot.node_count > 0) ? slot.node_count : vm_state->query_context->max_nodes;

    const int h_frontier = acquire_bitset(vm_state->query_context);
    const int h_next = acquire_bitset(vm_state->query_context);
    if (h_frontier >= 0 && h_next >= 0) {
        IMPULSE_ASSERT(static_cast<size_t>(h_frontier) < vm_state->query_context->bitsets.size());
        IMPULSE_ASSERT(static_cast<size_t>(h_next) < vm_state->query_context->bitsets.size());
        auto& frontier = vm_state->query_context->bitsets[h_frontier];
        auto& next_f = vm_state->query_context->bitsets[h_next];
        frontier.clear();

        if (src_type == TYPE_NODE_ID || src_type == TYPE_INT64) {
            const uint64_t start_node = src_val;
            reached.set(start_node);
            frontier.set(start_node);
        } else if (src_is_bitset) {
            const int h_src = static_cast<int>(src_val);
            IMPULSE_ASSERT(h_src >= 0 && static_cast<size_t>(h_src) < vm_state->query_context->bitsets.size());
            const auto& initial = vm_state->query_context->bitsets[h_src];
            for (size_t w = 0; w < words; ++w) {
                reached.words[w] = initial.words[w];
                frontier.words[w] = initial.words[w];
            }
            if (dst == src) release_bitset(vm_state->query_context, h_src);
        }

        bool changed = true;
        IMPULSE_TESTCASE(vm_state->query_context->has_fixpoint_max_hops);
        size_t max_iter = vm_state->query_context->has_fixpoint_max_hops
            ? static_cast<size_t>(vm_state->query_context->fixpoint_max_hops)
            : static_cast<size_t>(OP_88_DEFAULT_MAX_HOPS);
        IMPULSE_TESTCASE(max_iter == 0);

        while (changed && max_iter-- > 0) {
            if (vm_state->query_context && vm_state->query_context->fuel_enabled) {
                IMPULSE_TESTCASE(vm_state->query_context->fuel == 0);
                if (vm_state->query_context->fuel == 0) {
                    if (h_frontier >= 0) release_bitset(vm_state->query_context, h_frontier);
                    if (h_next >= 0) release_bitset(vm_state->query_context, h_next);
                    return IMPULSE_VM_ERR_GAS_EXHAUSTED;
                }
                vm_state->query_context->fuel--;
            }
            next_f.clear();
            changed = false;

            if (!slot.is_virtual && !slot.is_raw_coo && slot.offsets_ptr && slot.targets_ptr) {
                #pragma omp parallel for schedule(static)
                for (size_t u = 0; u < N; ++u) {
                    if (frontier.test(u)) {
                        const uint64_t start = slot.get_csr_offset(u);
                        const uint64_t end = slot.get_csr_offset(u + 1);
                        IMPULSE_WELL_FORMED_ASSERT(start <= end);
                        for (uint64_t e = start; e < end; ++e) {
                            const uint64_t v = slot.get_csr_target(e);
                            if (!reached.test(v) && v < vm_state->query_context->max_nodes) {
                                #pragma omp atomic
                                next_f.words[IMP_WORD_INDEX(v)] |= IMP_BIT_MASK64(v);
                            }
                        }
                    }
                }
            } else {
                walk_slot_forward_special(
                    vm_state->query_context,
                    rel,
                    [&](uint64_t u) { return frontier.test(static_cast<size_t>(u)); },
                    0,
                    next_f
                );
            }

            for (size_t w = 0; w < words; ++w) {
                const uint64_t diff = next_f.words[w] & ~reached.words[w];
                if (diff != 0ULL) {
                    changed = true;
                    reached.words[w] |= diff;
                    frontier.words[w] = diff;
                } else {
                    frontier.words[w] = 0ULL;
                }
            }
        }
    }
    if (h_frontier >= 0) release_bitset(vm_state->query_context, h_frontier);
    if (h_next >= 0) release_bitset(vm_state->query_context, h_next);

    vm_state->registers[dst] = static_cast<uint64_t>(h_dst);
    vm_state->register_types[dst] = TYPE_BITSET_HANDLE;

    bool is_empty = true;
    for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
        if (reached.words[i] != 0ULL) {
            is_empty = false;
            break;
        }
    }
    IMP_VM_SET_ZF(vm_state, is_empty);

    vm_state->pc++;
    DISPATCH();
}
