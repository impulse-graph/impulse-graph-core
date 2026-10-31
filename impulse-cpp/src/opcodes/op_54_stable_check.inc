// Opcode 0x54: OP_STABLE_CHECK
op_STABLE_CHECK: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src = inst.payload & 0xFFFF;
    if ((inst.payload >> 16) != 0) {
        src = inst.payload & 0xFFFF;
        dst = (inst.payload >> 16) & 0xFFFF;
    } else if (dst == 0 && src != 0) {
        dst = src;
    }
    VALIDATE_REG(dst);
    VALIDATE_REG(src);

    bool is_subset = true;
    bool dst_is_bitset = (vm_state->register_types[dst] == TYPE_BITSET_HANDLE);
    int h_dst = dst_is_bitset ? static_cast<int>(vm_state->registers[dst]) : -1;
    uint64_t scalar_dst = !dst_is_bitset ? vm_state->registers[dst] : 0;

    bool src_is_bitset = (vm_state->register_types[src] == TYPE_BITSET_HANDLE);
    int h_src = src_is_bitset ? static_cast<int>(vm_state->registers[src]) : -1;
    uint64_t scalar_src = !src_is_bitset ? vm_state->registers[src] : 0;

    if (src_is_bitset && dst_is_bitset) {
        const auto& bs_src = vm_state->query_context->bitsets[h_src];
        const auto& bs_dst = vm_state->query_context->bitsets[h_dst];
        for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
            if ((bs_src.words[i] & ~bs_dst.words[i]) != 0) {
                is_subset = false;
                break;
            }
        }
    } else if (!src_is_bitset && dst_is_bitset) {
        const auto& bs_dst = vm_state->query_context->bitsets[h_dst];
        is_subset = bitset_test(bs_dst, scalar_src, vm_state->query_context->max_nodes);
    } else if (!src_is_bitset && !dst_is_bitset) {
        is_subset = (scalar_src == scalar_dst);
    } else {
        is_subset = false;
    }

    IMPULSE_TESTCASE(is_subset);
    IMPULSE_TESTCASE(dst == src);
    impulse_vm_branch_coverage_record(static_cast<uint32_t>(vm_state->pc), OP_STABLE_CHECK, 1u,
                                      IMPULSE_COV_KIND_TWO_WAY, IMPULSE_COV_BOTH,
                                      is_subset ? 0u : 1u);
    if (is_subset) {
        vm_state->flags |= IMPULSE_VM_FLAG_ST;
        vm_state->flags |= IMPULSE_VM_FLAG_ZF;
    } else {
        vm_state->flags &= ~IMPULSE_VM_FLAG_ST;
        vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;
    }

    vm_state->pc++;
    DISPATCH();
}

