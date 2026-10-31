// Opcode 0x8B: OP_COO_WALK_FILTERED
op_COO_WALK_FILTERED: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src = inst.payload & 0xFF;
    uint16_t filter_reg = (inst.payload >> 8) & 0xFF;
    uint16_t rel = (inst.payload >> 16) & 0xFFFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src);
    VALIDATE_REG(filter_reg);

    bool src_is_bitset = (vm_state->register_types[src] == TYPE_BITSET_HANDLE);
    uint64_t scalar_src = vm_state->registers[src];
    const uint64_t* src_words = src_is_bitset ? vm_state->query_context->bitsets[static_cast<int>(scalar_src)].words : nullptr;

    bool flt_is_bitset = (vm_state->register_types[filter_reg] == TYPE_BITSET_HANDLE);
    uint64_t scalar_flt = vm_state->registers[filter_reg];
    const uint64_t* flt_words = flt_is_bitset ? vm_state->query_context->bitsets[static_cast<int>(scalar_flt)].words : nullptr;

    int h_dst = -1;
    if (vm_state->register_types[dst] == TYPE_BITSET_HANDLE && !(dst == src && src_is_bitset) && !(dst == filter_reg && flt_is_bitset)) {
        h_dst = static_cast<int>(vm_state->registers[dst]);
    } else {
        h_dst = acquire_bitset(vm_state->query_context);
        if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
    auto& bs_dst = vm_state->query_context->bitsets[h_dst];
    bs_dst.clear();

    if (rel < vm_state->query_context->slots.size()) {
        const auto& slot = vm_state->query_context->slots[rel];
        if (slot.offsets_ptr && slot.targets_ptr) {
            size_t N = (slot.node_count > 0) ? slot.node_count : vm_state->query_context->max_nodes;
            if (src_is_bitset) {
                #pragma omp parallel for schedule(static)
                for (size_t u = 0; u < N; ++u) {
                    if ((src_words[u >> 6] & (1ULL << (u & 63))) != 0) {
                        uint64_t start = slot.get_csr_offset(u);
                        uint64_t end = slot.get_csr_offset(u + 1);
                        for (uint64_t e = start; e < end; ++e) {
                            uint64_t v = slot.get_csr_target(e);
                            bool match = flt_words ? ((flt_words[v >> 6] & (1ULL << (v & 63))) != 0) : (v == scalar_flt);
                            if (match) {
                                #pragma omp atomic
                                bs_dst.words[v >> 6] |= (1ULL << (v & 63));
                            }
                        }
                    }
                }
            } else if (scalar_src < N) {
                uint64_t start = slot.get_csr_offset(scalar_src);
                uint64_t end = slot.get_csr_offset(scalar_src + 1);
                for (uint64_t e = start; e < end; ++e) {
                    uint64_t v = slot.get_csr_target(e);
                    bool match = flt_words ? ((flt_words[v >> 6] & (1ULL << (v & 63))) != 0) : (v == scalar_flt);
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
        if (bs_dst.words[i] != 0) {
            is_empty = false;
            break;
        }
    }
    if (is_empty) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
    else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

    vm_state->pc++;
    DISPATCH();
}

