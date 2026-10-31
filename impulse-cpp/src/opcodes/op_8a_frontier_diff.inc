/**
 * @file op_8a_frontier_diff.inc
 * @brief Formal Contract for OP_FRONTIER_DIFF (0x8A).
 *
 * Syntax: OP_FRONTIER_DIFF <dst_reg>, <r_curr>, <r_prev>
 * Inputs:
 *   - dst_reg: Target register receiving bitset difference (curr AND NOT prev)
 *   - r_curr (payload_low8): Current iteration frontier bitset
 *   - r_prev (payload_byte1): Previous iteration frontier bitset
 * Preconditions:
 *   - Both r_curr and r_prev must hold TYPE_BITSET_HANDLE
 * Outputs:
 *   - dst_reg set to TYPE_BITSET_HANDLE
 *   - Flags: ZF set if difference bitset is empty
 */
#ifndef OP_8A_CONTRACT_DEFINED
#define OP_8A_CONTRACT_DEFINED
#define OP_8A_DEFINED_FLAGS        0x00u
#define OP_8A_ALLOWED_SRC_TYPES    (IMP_TYPE_BIT(TYPE_BITSET_HANDLE))
#define OP_8A_ALLOWED_AUX1_TYPES   (IMP_TYPE_BIT(TYPE_BITSET_HANDLE))
#define OP_8A_OUTPUT_DST_TYPES     (IMP_TYPE_BIT(TYPE_BITSET_HANDLE))
#endif

// Opcode 0x8A: OP_FRONTIER_DIFF
op_FRONTIER_DIFF: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t r_curr = inst.payload & 0xFF;
    uint16_t r_prev = (inst.payload >> 8) & 0xFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(r_curr);
    VALIDATE_REG(r_prev);

    if (!vm_state->query_context) return IMPULSE_VM_ERR_NULL_SNAPSHOT;
    IMPULSE_CHECK_REG_TYPE(vm_state, r_curr, OP_8A_ALLOWED_SRC_TYPES);
    IMPULSE_CHECK_REG_TYPE(vm_state, r_prev, OP_8A_ALLOWED_AUX1_TYPES);

    int h_curr = static_cast<int>(vm_state->registers[r_curr]);
    int h_prev = static_cast<int>(vm_state->registers[r_prev]);
    if (h_curr < 0 || h_curr >= static_cast<int>(vm_state->query_context->bitsets.size()) ||
        h_prev < 0 || h_prev >= static_cast<int>(vm_state->query_context->bitsets.size())) {
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }

    int h_dst = -1;
    if (vm_state->register_types[dst] == TYPE_BITSET_HANDLE && dst != r_curr && dst != r_prev) {
        h_dst = static_cast<int>(vm_state->registers[dst]);
    } else {
        h_dst = acquire_bitset(vm_state->query_context);
        if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
    auto& bs_dst = vm_state->query_context->bitsets[h_dst];
    const auto& bs_curr = vm_state->query_context->bitsets[h_curr];
    const auto& bs_prev = vm_state->query_context->bitsets[h_prev];

    size_t words = vm_state->query_context->words_per_bitset;
    bool has_bits = false;
    for (size_t w = 0; w < words; ++w) {
        uint64_t d = bs_curr.words[w] & ~bs_prev.words[w];
        bs_dst.words[w] = d;
        if (d != 0) has_bits = true;
    }

    if (dst == r_curr) {
        release_bitset(vm_state->query_context, h_curr);
    } else if (dst == r_prev) {
        release_bitset(vm_state->query_context, h_prev);
    }

    vm_state->registers[dst] = static_cast<uint64_t>(h_dst);
    vm_state->register_types[dst] = TYPE_BITSET_HANDLE;

    if (!has_bits) {
        vm_state->flags |= IMPULSE_VM_FLAG_ZF;
    } else {
        vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;
    }

    vm_state->pc++;
    DISPATCH();
}

