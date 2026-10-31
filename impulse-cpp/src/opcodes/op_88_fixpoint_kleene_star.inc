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
#endif

// Opcode 0x88: OP_FIXPOINT_KLEENE_STAR
op_FIXPOINT_KLEENE_STAR: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src = inst.payload & 0xFF;
    uint16_t rel = (inst.payload >> 16) & 0xFFFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src);

    if (!vm_state->query_context) return IMPULSE_VM_ERR_NULL_SNAPSHOT;
    IMPULSE_CHECK_REG_TYPE(vm_state, src, OP_88_ALLOWED_SRC_TYPES);

    if (rel >= vm_state->query_context->slots.size()) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    const auto& slot = vm_state->query_context->slots[rel];
    if (!slot.offsets_ptr || !slot.targets_ptr) return IMPULSE_VM_ERR_NULL_SNAPSHOT;

    uint8_t src_type = vm_state->register_types[src];
    uint64_t src_val = vm_state->registers[src];
    bool src_is_bitset = (src_type == TYPE_BITSET_HANDLE);

    int h_dst = -1;
    if (vm_state->register_types[dst] == TYPE_BITSET_HANDLE && !(dst == src && src_is_bitset)) {
        h_dst = static_cast<int>(vm_state->registers[dst]);
    } else {
        h_dst = acquire_bitset(vm_state->query_context);
        if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
    auto& reached = vm_state->query_context->bitsets[h_dst];
    reached.clear();

    size_t words = vm_state->query_context->words_per_bitset;
    size_t N = (slot.node_count > 0) ? slot.node_count : vm_state->query_context->max_nodes;

    int h_frontier = acquire_bitset(vm_state->query_context);
    int h_next = acquire_bitset(vm_state->query_context);
    if (h_frontier >= 0 && h_next >= 0) {
        auto& frontier = vm_state->query_context->bitsets[h_frontier];
        auto& next_f = vm_state->query_context->bitsets[h_next];
        frontier.clear();

        if (src_type == TYPE_NODE_ID || src_type == TYPE_INT64) {
            uint64_t start_node = src_val;
            reached.set(start_node);
            frontier.set(start_node);
        } else if (src_is_bitset) {
            int h_src = static_cast<int>(src_val);
            const auto& initial = vm_state->query_context->bitsets[h_src];
            for (size_t w = 0; w < words; ++w) {
                reached.words[w] = initial.words[w];
                frontier.words[w] = initial.words[w];
            }
            if (dst == src) release_bitset(vm_state->query_context, h_src);
        }

        bool changed = true;
        size_t max_iter = 1000;
        while (changed && max_iter-- > 0) {
            next_f.clear();
            changed = false;

            #pragma omp parallel for schedule(static)
            for (size_t u = 0; u < N; ++u) {
                if (frontier.test(u)) {
                    uint64_t start = slot.get_csr_offset(u);
                    uint64_t end = slot.get_csr_offset(u + 1);
                    for (uint64_t e = start; e < end; ++e) {
                        uint64_t v = slot.get_csr_target(e);
                        if (!reached.test(v) && v < vm_state->query_context->max_nodes) {
                            #pragma omp atomic
                            next_f.words[v >> 6] |= (1ULL << (v & 63));
                        }
                    }
                }
            }

            for (size_t w = 0; w < words; ++w) {
                uint64_t diff = next_f.words[w] & ~reached.words[w];
                if (diff != 0) {
                    changed = true;
                    reached.words[w] |= diff;
                    frontier.words[w] = diff;
                } else {
                    frontier.words[w] = 0;
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
        if (reached.words[i] != 0) { is_empty = false; break; }
    }
    if (is_empty) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
    else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

    vm_state->pc++;
    DISPATCH();
}

