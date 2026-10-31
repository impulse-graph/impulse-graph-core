/**
 * @file op_25_mask_or.inc
 * @brief Formal Contract for OP_MASK_OR (0x25).
 *
 * Mnemonic: OP_MASK_OR
 * Opcode: 0x25
 * Description: BitSet bitwise union (OR) between two mask frontiers.
 *
 * Operands:
 *  - dst = inst.dst_reg, src1 = payload & 0xFF, src2 = (payload >> 8) & 0xFF
 *
 * Inputs:
 *  - src1, src2: TYPE_BITSET_HANDLE
 *
 * Outputs:
 *  - dst: TYPE_BITSET_HANDLE
 *
 * Flags:
 *  - ZF: Set if union is empty
 *
 * Context Requirement:
 *  - Requires context (query_context bitset pool)
 */
#ifndef OP_25_CONTRACT_DEFINED
#define OP_25_CONTRACT_DEFINED 1
#define OP_25_ALLOWED_SRC_TYPES  (IMP_TYPE_BIT(TYPE_BITSET_HANDLE) | IMP_TYPE_BIT(TYPE_NULL) | IMP_TYPES_INT_OR_NODE)
#define OP_25_ALLOWED_AUX1_TYPES (IMP_TYPE_BIT(TYPE_BITSET_HANDLE) | IMP_TYPE_BIT(TYPE_NULL) | IMP_TYPES_INT_OR_NODE)
#endif

// Opcode 0x25: OP_MASK_OR
op_MASK_OR: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src1 = inst.payload & 0xFF;
    uint16_t src2 = (inst.payload >> 8) & 0xFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src1);
    VALIDATE_REG(src2);
    if (!vm_state->query_context) return IMPULSE_VM_ERR_NULL_SNAPSHOT;
    IMPULSE_CHECK_REG_TYPE(vm_state, src1, OP_25_ALLOWED_SRC_TYPES);
    IMPULSE_CHECK_REG_TYPE(vm_state, src2, OP_25_ALLOWED_AUX1_TYPES);

    int h1 = static_cast<int>(vm_state->registers[src1]);
    int h2 = static_cast<int>(vm_state->registers[src2]);
    int h_dst = -1;
    if (vm_state->register_types[dst] == TYPE_BITSET_HANDLE && dst != src1 && dst != src2) {
        h_dst = static_cast<int>(vm_state->registers[dst]);
    } else {
        h_dst = acquire_bitset(vm_state->query_context);
        if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        vm_state->registers[dst] = h_dst;
        vm_state->register_types[dst] = TYPE_BITSET_HANDLE;
    }
    auto& bs_dst = vm_state->query_context->bitsets[h_dst];
    const auto& bs1 = vm_state->query_context->bitsets[h1];
    const auto& bs2 = vm_state->query_context->bitsets[h2];
    size_t words = vm_state->query_context->words_per_bitset;
    bool is_empty = true;
    for (size_t w = 0; w < words; ++w) {
        uint64_t val = bs1.words[w] | bs2.words[w];
        bs_dst.words[w] = val;
        if (val != 0) is_empty = false;
    }
    if (is_empty) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
    else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;
    vm_state->pc++;
    DISPATCH();
}

