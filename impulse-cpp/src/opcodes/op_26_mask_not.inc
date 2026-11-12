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
#define OP_26_ALLOWED_SRC_TYPES  (IMP_TYPE_BIT(TYPE_BITSET_HANDLE) | IMP_TYPE_BIT(TYPE_NULL) | IMP_TYPES_INT_OR_NODE)
#define OP_26_DEFAULT_MOCK_NODES 1024u
#endif

// Opcode 0x26: OP_MASK_NOT
op_MASK_NOT: {
    const auto& inst = bytecode[vm_state->pc];
    const uint16_t dst = IMP_INST_DST(inst);
    const uint16_t src = IMP_INST_U8_0(inst);
    VALIDATE_REG(dst);
    VALIDATE_REG(src);
    IMPULSE_ASSERT(dst < IMP_VM_GPR_COUNT && src < IMP_VM_GPR_COUNT);
    IMP_REQUIRE_CONTEXT(vm_state);
    IMPULSE_ASSERT(vm_state->query_context != nullptr);
    IMPULSE_CHECK_REG_TYPE(vm_state, src, OP_26_ALLOWED_SRC_TYPES);
    IMPULSE_TESTCASE(dst == src);

    int h_src = static_cast<int>(vm_state->registers[src]);
    int h_dst = -1;
    if (vm_state->register_types[dst] == TYPE_BITSET_HANDLE && dst != src) {
        h_dst = static_cast<int>(vm_state->registers[dst]);
    } else {
        h_dst = acquire_bitset(vm_state->query_context);
        if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        IMPULSE_ASSERT(h_dst >= 0 && static_cast<size_t>(h_dst) < vm_state->query_context->bitsets.size());
        vm_state->registers[dst] = static_cast<uint64_t>(h_dst);
        vm_state->register_types[dst] = TYPE_BITSET_HANDLE;
    }
    auto& bs_dst = vm_state->query_context->bitsets[h_dst];
    const auto& bs_src = vm_state->query_context->bitsets[h_src];
    size_t words = vm_state->query_context->words_per_bitset;
    size_t node_count = (!vm_state->query_context->slots.empty() && vm_state->query_context->slots[0].node_count > 0)
        ? std::max(vm_state->query_context->slots[0].node_count, vm_state->query_context->slots[0].csc_node_count)
        : ((vm_state->query_context->snapshot != nullptr && impulse_snapshot_max_node_count(vm_state->query_context->snapshot) > 0)
            ? impulse_snapshot_max_node_count(vm_state->query_context->snapshot)
            : OP_26_DEFAULT_MOCK_NODES);
    size_t full_words = IMP_WORD_INDEX(node_count);
    uint32_t rem_bits = IMP_BIT_IN_WORD(node_count);
    IMPULSE_TESTCASE(rem_bits == 0u);
    bool is_empty = true;
    for (size_t w = 0; w < words; ++w) {
        uint64_t val = 0;
        if (w < full_words) {
            val = ~bs_src.words[w];
        } else if (w == full_words && rem_bits > 0) {
            val = (~bs_src.words[w]) & IMP_LANE_MASK_FOR_COUNT(rem_bits);
        }
        bs_dst.words[w] = val;
        if (val != 0) is_empty = false;
    }
    IMP_VM_SET_ZF(vm_state, is_empty);
    vm_state->pc++;
    DISPATCH();
}

