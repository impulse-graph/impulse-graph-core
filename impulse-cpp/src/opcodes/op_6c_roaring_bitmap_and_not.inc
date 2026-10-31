/**
 * @file op_6c_roaring_bitmap_and_not.inc
 * @brief Formal Contract for OP_ROARING_BITMAP_AND_NOT (0x6C).
 *
 * Syntax: OP_ROARING_BITMAP_AND_NOT <dst_reg>, <src1_reg>, <src2_reg>
 * Inputs:
 *   - dst_reg: Destination bitset handle receiving bitwise difference (src1 AND NOT src2)
 *   - src1_reg (payload_low16): Base bitset handle
 *   - src2_reg (payload_high16): Mask bitset handle to subtract
 * Preconditions:
 *   - Both src1_reg and src2_reg must hold TYPE_BITSET_HANDLE
 * Outputs:
 *   - dst_reg set to TYPE_BITSET_HANDLE
 *   - Flags: ZF set if difference is empty
 */
#ifndef OP_6C_CONTRACT_DEFINED
#define OP_6C_CONTRACT_DEFINED
#define OP_6C_DEFINED_FLAGS        0x00u
#define OP_6C_ALLOWED_SRC_TYPES    (IMP_TYPE_BIT(TYPE_BITSET_HANDLE) | IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_NULL))
#define OP_6C_ALLOWED_AUX1_TYPES   (IMP_TYPE_BIT(TYPE_BITSET_HANDLE) | IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_NULL))
#define OP_6C_OUTPUT_DST_TYPES     (IMP_TYPE_BIT(TYPE_BITSET_HANDLE))
#endif

// Opcode 0x6C: OP_ROARING_BITMAP_AND_NOT
op_ROARING_BITMAP_AND_NOT: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src1 = inst.payload & 0xFFFF;
    uint16_t src2 = (inst.payload >> 16) & 0xFFFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src1);
    VALIDATE_REG(src2);

    if (!vm_state->query_context) return IMPULSE_VM_ERR_NULL_SNAPSHOT;
    IMPULSE_CHECK_REG_TYPE(vm_state, src1, OP_6C_ALLOWED_SRC_TYPES);
    IMPULSE_CHECK_REG_TYPE(vm_state, src2, OP_6C_ALLOWED_AUX1_TYPES);

    int h1 = (vm_state->register_types[src1] == TYPE_BITSET_HANDLE) ? static_cast<int>(vm_state->registers[src1]) : -1;
    int h2 = (vm_state->register_types[src2] == TYPE_BITSET_HANDLE) ? static_cast<int>(vm_state->registers[src2]) : -1;

    int h_dst = acquire_bitset(vm_state->query_context);
    if (h_dst < 0) {
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
    auto& bs_dst = vm_state->query_context->bitsets[h_dst];
    bs_dst.clear();

    if (h1 >= 0 && h1 < static_cast<int>(vm_state->query_context->bitsets.size())) {
        const auto& bs1 = vm_state->query_context->bitsets[h1];
        if (h2 >= 0 && h2 < static_cast<int>(vm_state->query_context->bitsets.size())) {
            const auto& bs2 = vm_state->query_context->bitsets[h2];
            for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                bs_dst.words[i] = bs1.words[i] & ~bs2.words[i];
            }
        } else {
            for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                bs_dst.words[i] = bs1.words[i];
            }
        }
    }

    if (dst == src1 && h1 >= 0) {
        release_bitset(vm_state->query_context, h1);
    } else if (dst == src2 && h2 >= 0) {
        release_bitset(vm_state->query_context, h2);
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

