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
#endif

// Opcode 0x11: OP_CSR_WALK_FILTERED
op_CSR_WALK_FILTERED: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src = inst.payload & 0xFF;
    uint16_t filter_reg = (inst.payload >> 8) & 0xFF;
    uint16_t rel;
    if (inst.flags & 0x80) {
        if (vm_state->pc + 1 >= instruction_count) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        rel = bytecode[vm_state->pc + 1].dst_reg;
        vm_state->pc++;
    } else {
        rel = (inst.payload >> 16) & 0xFFFF;
    }
    if (!(inst.flags & 0x80) && filter_reg == 0 && rel > 0 && rel < 64 && vm_state->register_types[rel] == TYPE_BITSET_HANDLE) {
        filter_reg = rel;
        rel = 0;
    }
    VALIDATE_REG(dst);
    VALIDATE_REG(src);
    VALIDATE_REG(filter_reg);
    if (!vm_state->query_context) return IMPULSE_VM_ERR_NULL_SNAPSHOT;
    IMPULSE_CHECK_REG_TYPE(vm_state, src, IMP_TYPES_SOURCE_SET);
    IMPULSE_CHECK_REG_TYPE(vm_state, filter_reg, IMP_TYPE_BIT(TYPE_BITSET_HANDLE));

    if (rel >= vm_state->query_context->slots.size()) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    const auto& slot = vm_state->query_context->slots[rel];

    int h_dst = -1;
    if (vm_state->register_types[dst] == TYPE_BITSET_HANDLE && dst != src && dst != filter_reg) {
        h_dst = static_cast<int>(vm_state->registers[dst]);
        vm_state->query_context->bitsets[h_dst].clear();
    } else {
        h_dst = acquire_bitset(vm_state->query_context);
        if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        vm_state->query_context->bitsets[h_dst].clear();
    }
    auto& bs_dst = vm_state->query_context->bitsets[h_dst];
    
    bool src_is_null = (vm_state->register_types[src] == TYPE_NULL && !(inst.flags & IMPULSE_VM_OP_FLAG_INPUT_SEED));
    bool src_is_bitset = (vm_state->register_types[src] == TYPE_BITSET_HANDLE);
    int h_src = src_is_bitset ? static_cast<int>(vm_state->registers[src]) : -1;
    uint64_t scalar_src = !src_is_bitset ? vm_state->registers[src] : 0;
    
    const uint64_t* filter_words = nullptr;
    if (vm_state->register_types[filter_reg] == TYPE_BITSET_HANDLE) {
        int h_filt = static_cast<int>(vm_state->registers[filter_reg]);
        filter_words = vm_state->query_context->bitsets[h_filt].words;
    }
    uint64_t max_nodes = vm_state->query_context->max_nodes;

    if (!src_is_null && slot.offsets_ptr && slot.targets_ptr && filter_words) {
        if (src_is_bitset) {
            #pragma omp parallel for schedule(dynamic, 64)
            for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                uint64_t w = vm_state->query_context->bitsets[h_src].words[i];
                if (w == 0) continue;
                for (int b = 0; b < 64; ++b) {
                    if (w & (1ULL << b)) {
                        uint64_t u = i * 64 + b;
                        if (u < slot.node_count) {
                            uint64_t start = slot.get_csr_offset(u);
                            uint64_t end   = slot.get_csr_offset(u + 1);
                            for (uint64_t idx = start; idx < end; ++idx) {
                                uint64_t target_node = slot.get_csr_target(idx);
                                if (target_node != 0xFFFFFFFF && target_node != 0xFFFF && target_node != ~0ULL) {
                                    if (target_node < max_nodes && (filter_words[target_node / 64] & (1ULL << (target_node % 64)))) {
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
                for (uint64_t idx = start; idx < end; ++idx) {
                    uint64_t target_node = slot.get_csr_target(idx);
                    if (target_node != 0xFFFFFFFF && target_node != 0xFFFF && target_node != ~0ULL) {
                        if (target_node < max_nodes && (filter_words[target_node / 64] & (1ULL << (target_node % 64)))) {
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
    if (is_empty) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
    else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

    vm_state->pc++;
    DISPATCH();
}

