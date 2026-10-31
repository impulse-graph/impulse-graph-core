// Opcode 0x87: OP_CSC_WALK_DIRECT_STORE
op_CSC_WALK_DIRECT_STORE: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src = inst.payload & 0xFF;
    uint16_t rel = (inst.payload >> 16) & 0xFFFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src);

    if (rel >= vm_state->query_context->slots.size()) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    const auto& slot = vm_state->query_context->slots[rel];
    if (!slot.csc_offsets_ptr || !slot.csc_targets_ptr) return IMPULSE_VM_ERR_NULL_SNAPSHOT;

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

    size_t csc_bound = (slot.csc_node_count > 0) ? slot.csc_node_count : slot.node_count;
    size_t N = (csc_bound > 0) ? csc_bound : vm_state->query_context->max_nodes;
    if (src_type == TYPE_NODE_ID || src_type == TYPE_INT64) {
        uint64_t target_u = src_val;
        #pragma omp parallel for schedule(static)
        for (size_t v = 0; v < N; ++v) {
            uint64_t start = slot.get_csc_offset(v);
            uint64_t end = slot.get_csc_offset(v + 1);
            for (uint64_t e = start; e < end; ++e) {
                uint64_t u = slot.get_csc_target(e);
                if (u == target_u) {
                    #pragma omp atomic
                    bs_dst.words[v >> 6] |= (1ULL << (v & 63));
                    break;
                }
            }
        }
    } else if (src_is_bitset) {
        int h_src = static_cast<int>(src_val);
        const auto& bs_src = vm_state->query_context->bitsets[h_src];
        #pragma omp parallel for schedule(static)
        for (size_t v = 0; v < N; ++v) {
            uint64_t start = slot.get_csc_offset(v);
            uint64_t end = slot.get_csc_offset(v + 1);
            for (uint64_t e = start; e < end; ++e) {
                uint64_t u = slot.get_csc_target(e);
                if (bs_src.test(u)) {
                    #pragma omp atomic
                    bs_dst.words[v >> 6] |= (1ULL << (v & 63));
                    break;
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

