/**
 * @file op_85_csr_walk_dense_stream.inc
 * @brief Formal Contract for OP_CSR_WALK_DENSE_STREAM (0x85).
 *
 * Syntax: OP_CSR_WALK_DENSE_STREAM <dst_reg>, <src_reg> (payload: [rel_id:16 | ... | src:8])
 * Inputs:
 *   - dst_reg: Target register receiving uncompressed dense frontier
 *   - src_reg (payload_low8): Source frontier bitset or scalar node ID
 * Preconditions:
 *   - src_reg must be TYPE_INT64, TYPE_NODE_ID, or TYPE_BITSET_HANDLE
 * Outputs:
 *   - dst_reg set to TYPE_BITSET_HANDLE
 *   - Flags: ZF set if output frontier is empty
 */
#ifndef OP_85_CONTRACT_DEFINED
#define OP_85_CONTRACT_DEFINED
#define OP_85_DEFINED_FLAGS        0x00u
#define OP_85_ALLOWED_SRC_TYPES    (IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_BITSET_HANDLE))
#define OP_85_OUTPUT_DST_TYPES     (IMP_TYPE_BIT(TYPE_BITSET_HANDLE))
#endif

// Opcode 0x85: OP_CSR_WALK_DENSE_STREAM
op_CSR_WALK_DENSE_STREAM: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src = inst.payload & 0xFF;
    uint16_t rel = (inst.payload >> 16) & 0xFFFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src);

    if (!vm_state->query_context) return IMPULSE_VM_ERR_NULL_SNAPSHOT;
    IMPULSE_CHECK_REG_TYPE(vm_state, src, OP_85_ALLOWED_SRC_TYPES);

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
    auto& bs_dst = vm_state->query_context->bitsets[h_dst];
    bs_dst.clear();

    if (src_type == TYPE_NODE_ID || src_type == TYPE_INT64) {
        uint64_t u = src_val;
        if (u < slot.node_count || slot.node_count == 0) {
            uint64_t start = slot.get_csr_offset(u);
            uint64_t end = slot.get_csr_offset(u + 1);
            for (uint64_t e = start; e < end; ++e) {
                uint64_t v = slot.get_csr_target(e);
                bs_dst.set(v);
            }
        }
    } else if (src_is_bitset) {
        int h_src = static_cast<int>(src_val);
        const auto& bs_src = vm_state->query_context->bitsets[h_src];
        size_t N = (slot.node_count > 0) ? slot.node_count : vm_state->query_context->max_nodes;
        #pragma omp parallel for schedule(static)
        for (size_t u = 0; u < N; ++u) {
            if (bs_src.test(u)) {
                uint64_t start = slot.get_csr_offset(u);
                uint64_t end = slot.get_csr_offset(u + 1);
                for (uint64_t e = start; e < end; ++e) {
                    uint64_t v = slot.get_csr_target(e);
                    if (v < vm_state->query_context->max_nodes) {
                        #pragma omp atomic
                        bs_dst.words[v >> 6] |= (1ULL << (v & 63));
                    }
                }
            }
        }
        if (dst == src) release_bitset(vm_state->query_context, h_src);
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

