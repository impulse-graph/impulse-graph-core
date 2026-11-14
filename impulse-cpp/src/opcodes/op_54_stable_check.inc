/**
 * Formal Opcode Contract: OP_STABLE_CHECK (0x54)
 *
 * Layout:
 *   dst_reg:          inst.dst_reg (DST_REG, target/previous set)
 *   src_reg:          IMP_INST_LOW16(inst) (PAYLOAD_LOW16, candidate/delta set)
 *   allowed_src:      TYPE_BITSET_HANDLE | IMP_TYPES_INT_OR_NODE
 *   flags:            ST and ZF set if src is subset of dst
 *   requires_ctx:     true
 */
#ifndef OP_54_CONTRACT_DEFINED
#define OP_54_CONTRACT_DEFINED
#define OP_54_DEFINED_FLAGS        0x00u
#define OP_54_ALLOWED_SRC_TYPES    (IMP_TYPE_BIT(TYPE_BITSET_HANDLE) | IMP_TYPES_INT_OR_NODE)
#define OP_54_OUTPUT_DST_TYPES     IMP_TYPES_NONE
#define OP_54_COV_SITE_ID          1u
#endif

// Opcode 0x54: OP_STABLE_CHECK
op_STABLE_CHECK: {
    IMPULSE_ASSERT(vm_state != nullptr);
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = IMP_INST_DST(inst);
    uint16_t src = IMP_INST_LOW16(inst);
    const uint16_t high16 = IMP_INST_HIGH16(inst);
    if (high16 != 0u) {
        src = IMP_INST_LOW16(inst);
        dst = high16;
    } else if (dst == 0u && src != 0u) {
        dst = src;
    }
    IMP_VALIDATE_GPR(dst);
    IMP_VALIDATE_GPR(src);
    IMP_REQUIRE_CONTEXT(vm_state);
    IMPULSE_ASSERT(vm_state->query_context != nullptr);
    IMPULSE_ASSERT(dst < IMP_VM_GPR_COUNT && src < IMP_VM_GPR_COUNT);
    IMPULSE_CHECK_REG_TYPE(vm_state, dst, OP_54_ALLOWED_SRC_TYPES);
    IMPULSE_CHECK_REG_TYPE(vm_state, src, OP_54_ALLOWED_SRC_TYPES);

    bool is_subset = true;
    const bool dst_is_bitset = (vm_state->register_types[dst] == TYPE_BITSET_HANDLE);
    const int h_dst = dst_is_bitset ? static_cast<int>(vm_state->registers[dst]) : -1;
    const uint64_t scalar_dst = !dst_is_bitset ? vm_state->registers[dst] : 0ULL;

    const bool src_is_bitset = (vm_state->register_types[src] == TYPE_BITSET_HANDLE);
    const int h_src = src_is_bitset ? static_cast<int>(vm_state->registers[src]) : -1;
    const uint64_t scalar_src = !src_is_bitset ? vm_state->registers[src] : 0ULL;

    IMPULSE_TESTCASE(src_is_bitset && dst_is_bitset);
    IMPULSE_TESTCASE(!src_is_bitset && dst_is_bitset);
    IMPULSE_TESTCASE(!src_is_bitset && !dst_is_bitset);
    IMPULSE_TESTCASE(src_is_bitset && !dst_is_bitset);

    if (src_is_bitset && dst_is_bitset) {
        if (h_src < 0 || static_cast<size_t>(h_src) >= vm_state->query_context->bitsets.size() ||
            vm_state->query_context->bitsets[h_src].words == nullptr ||
            h_dst < 0 || static_cast<size_t>(h_dst) >= vm_state->query_context->bitsets.size() ||
            vm_state->query_context->bitsets[h_dst].words == nullptr) {
            return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        }
        IMPULSE_ASSERT(h_src >= 0 && static_cast<size_t>(h_src) < VM_MAX_BITSET_HANDLES);
        IMPULSE_ASSERT(h_dst >= 0 && static_cast<size_t>(h_dst) < VM_MAX_BITSET_HANDLES);
        const auto& bs_src = vm_state->query_context->bitsets[h_src];
        const auto& bs_dst = vm_state->query_context->bitsets[h_dst];
        for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
            if ((bs_src.words[i] & ~bs_dst.words[i]) != 0ULL) {
                is_subset = false;
                break;
            }
        }
    } else if (!src_is_bitset && dst_is_bitset) {
        if (h_dst < 0 || static_cast<size_t>(h_dst) >= VM_MAX_BITSET_HANDLES) {
            return IMPULSE_VM_ERR_INVALID_REGISTER;
        }
        IMPULSE_ASSERT(h_dst >= 0 && static_cast<size_t>(h_dst) < VM_MAX_BITSET_HANDLES);
        const auto& bs_dst = vm_state->query_context->bitsets[h_dst];
        is_subset = bitset_test(bs_dst, scalar_src, vm_state->query_context->max_nodes);
    } else if (!src_is_bitset && !dst_is_bitset) {
        is_subset = (scalar_src == scalar_dst);
    } else {
        is_subset = false;
    }

    IMPULSE_TESTCASE(is_subset);
    IMPULSE_TESTCASE(dst == src);
    IMP_VM_COV_BRANCH(vm_state, OP_STABLE_CHECK, OP_54_COV_SITE_ID, is_subset);
    IMP_VM_SET_ST(vm_state, is_subset);
    IMP_VM_SET_ZF(vm_state, is_subset);

    vm_state->pc++;
    DISPATCH();
}
