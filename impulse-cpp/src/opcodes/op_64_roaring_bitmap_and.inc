/**
 * @file op_64_roaring_bitmap_and.inc
 * @brief Formal Contract for OP_ROARING_BITMAP_AND (0x64).
 *
 * Syntax: OP_ROARING_BITMAP_AND <dst_reg>, <src1_reg>, <src2_reg>
 * Inputs:
 *   - dst_reg: Destination bitset handle receiving bitwise AND result
 *   - src1_reg (payload_low16): First operand bitset handle
 *   - src2_reg (payload_high16): Second operand bitset handle
 * Preconditions:
 *   - Both src1_reg and src2_reg must hold TYPE_BITSET_HANDLE
 * Outputs:
 *   - dst_reg set to TYPE_BITSET_HANDLE
 *   - Flags: ZF set if intersection is empty
 */
#ifndef OP_64_CONTRACT_DEFINED
#define OP_64_CONTRACT_DEFINED
#define OP_64_DEFINED_FLAGS        0x00u
#define OP_64_ALLOWED_SRC_TYPES    (IMP_TYPE_BIT(TYPE_BITSET_HANDLE) | IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_NULL))
#define OP_64_ALLOWED_AUX1_TYPES   (IMP_TYPE_BIT(TYPE_BITSET_HANDLE) | IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_NULL))
#define OP_64_OUTPUT_DST_TYPES     (IMP_TYPE_BIT(TYPE_BITSET_HANDLE))
#endif

// Opcode 0x64: OP_ROARING_BITMAP_AND
op_ROARING_BITMAP_AND: {
    IMPULSE_ASSERT(vm_state != nullptr);
    const auto& inst = bytecode[vm_state->pc];
    const uint16_t dst = IMP_INST_DST(inst);
    const uint16_t src1 = IMP_INST_LOW16(inst);
    const uint16_t src2 = IMP_INST_HIGH16(inst);
    IMP_VALIDATE_GPR(dst);
    IMP_VALIDATE_GPR(src1);
    IMP_VALIDATE_GPR(src2);

    IMP_REQUIRE_CONTEXT(vm_state);
    IMPULSE_ASSERT(vm_state->query_context != nullptr);
    IMPULSE_ASSERT(dst < IMP_VM_GPR_COUNT && src1 < IMP_VM_GPR_COUNT && src2 < IMP_VM_GPR_COUNT);
    IMPULSE_CHECK_REG_TYPE(vm_state, src1, OP_64_ALLOWED_SRC_TYPES);
    IMPULSE_CHECK_REG_TYPE(vm_state, src2, OP_64_ALLOWED_AUX1_TYPES);

    IMPULSE_TESTCASE(dst == src1);
    IMPULSE_TESTCASE(dst == src2);
    IMPULSE_TESTCASE(src1 == src2);

    const int h1 = (vm_state->register_types[src1] == TYPE_BITSET_HANDLE) ? static_cast<int>(vm_state->registers[src1]) : -1;
    const int h2 = (vm_state->register_types[src2] == TYPE_BITSET_HANDLE) ? static_cast<int>(vm_state->registers[src2]) : -1;

    const int h_dst = acquire_bitset(vm_state->query_context);
    if (h_dst < 0) {
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
    IMPULSE_ASSERT(h_dst >= 0 && static_cast<size_t>(h_dst) < vm_state->query_context->bitsets.size());
    auto& bs_dst = vm_state->query_context->bitsets[h_dst];
    bs_dst.clear();

    if (h1 >= 0 && h2 >= 0 &&
        static_cast<size_t>(h1) < vm_state->query_context->bitsets.size() &&
        static_cast<size_t>(h2) < vm_state->query_context->bitsets.size()) {
        const auto& bs1 = vm_state->query_context->bitsets[h1];
        const auto& bs2 = vm_state->query_context->bitsets[h2];
        for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
            bs_dst.words[i] = bs1.words[i] & bs2.words[i];
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
        if (bs_dst.words[i] != 0ULL) {
            is_empty = false;
            break;
        }
    }
    IMP_VM_SET_ZF(vm_state, is_empty);

    vm_state->pc++;
    DISPATCH();
}
