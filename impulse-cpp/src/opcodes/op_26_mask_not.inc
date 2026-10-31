/**
 * @file op_26_mask_not.inc
 * @brief Formal Contract for OP_MASK_NOT (0x26).
 *
 * Mnemonic: OP_MASK_NOT
 * Opcode: 0x26
 * Description: BitSet bitwise complement (NOT / universal node domain negation).
 *
 * Operands:
 *  - dst = inst.dst_reg, src = payload & 0xFF
 *
 * Inputs:
 *  - src: TYPE_BITSET_HANDLE
 *
 * Outputs:
 *  - dst: TYPE_BITSET_HANDLE
 *
 * Flags:
 *  - ZF: Set if inverted bitset is empty
 *
 * Context Requirement:
 *  - Requires context (query_context bitset pool)
 */
#ifndef OP_26_CONTRACT_DEFINED
#define OP_26_CONTRACT_DEFINED 1
#define OP_26_ALLOWED_SRC_TYPES (IMP_TYPE_BIT(TYPE_BITSET_HANDLE) | IMP_TYPE_BIT(TYPE_NULL) | IMP_TYPES_INT_OR_NODE)
#endif

// Opcode 0x26: OP_MASK_NOT
op_MASK_NOT: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src = inst.payload & 0xFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src);
    if (!vm_state->query_context) return IMPULSE_VM_ERR_NULL_SNAPSHOT;
    IMPULSE_CHECK_REG_TYPE(vm_state, src, OP_26_ALLOWED_SRC_TYPES);

    int h_src = static_cast<int>(vm_state->registers[src]);
    int h_dst = -1;
    if (vm_state->register_types[dst] == TYPE_BITSET_HANDLE && dst != src) {
        h_dst = static_cast<int>(vm_state->registers[dst]);
    } else {
        h_dst = acquire_bitset(vm_state->query_context);
        if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        vm_state->registers[dst] = h_dst;
        vm_state->register_types[dst] = TYPE_BITSET_HANDLE;
    }
    auto& bs_dst = vm_state->query_context->bitsets[h_dst];
    const auto& bs_src = vm_state->query_context->bitsets[h_src];
    size_t words = vm_state->query_context->words_per_bitset;
    size_t node_count = (!vm_state->query_context->slots.empty() && vm_state->query_context->slots[0].node_count > 0)
        ? std::max(vm_state->query_context->slots[0].node_count, vm_state->query_context->slots[0].csc_node_count)
        : vm_state->query_context->max_nodes;
    size_t full_words = node_count / 64;
    size_t rem_bits = node_count % 64;
    bool is_empty = true;
    for (size_t w = 0; w < words; ++w) {
        uint64_t val = 0;
        if (w < full_words) {
            val = ~bs_src.words[w];
        } else if (w == full_words && rem_bits > 0) {
            val = (~bs_src.words[w]) & ((1ULL << rem_bits) - 1ULL);
        }
        bs_dst.words[w] = val;
        if (val != 0) is_empty = false;
    }
    if (is_empty) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
    else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;
    vm_state->pc++;
    DISPATCH();
}

