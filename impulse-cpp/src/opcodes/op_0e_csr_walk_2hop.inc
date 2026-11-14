/**
 * @file op_0e_csr_walk_2hop.inc
 * @brief Formal Contract for OP_CSR_WALK_2HOP (0x0E).
 *
 * Mnemonic: OP_CSR_WALK_2HOP
 * Opcode: 0x0E
 * Description: Performs fused 2-hop CSR graph expansion across two relations without intermediate materialization.
 *
 * Operands:
 *  - dst = inst.dst_reg, rel1 = payload & 0xFFFF, rel2 = (payload >> 16) & 0xFFFF
 *
 * Inputs:
 *  - src (R0 / payload): TYPE_BITSET_HANDLE | TYPE_NODE_ID
 *
 * Outputs:
 *  - dst: TYPE_BITSET_HANDLE
 *
 * Flags:
 *  - ZF: Set if 2-hop reached frontier is empty
 *
 * Context Requirement:
 *  - Requires context (query_context snapshot and bitset pool)
 */
#ifndef OP_0E_CONTRACT_DEFINED
#define OP_0E_CONTRACT_DEFINED 1
#define OP_0E_DEFAULT_SRC_REG 0u
#endif

// Opcode 0x0E: OP_CSR_WALK_2HOP
op_CSR_WALK_2HOP: {
    const auto& inst = bytecode[vm_state->pc];
    const uint16_t dst = IMP_INST_DST(inst);
    uint16_t src = OP_0E_DEFAULT_SRC_REG;
    uint16_t rel1 = 0;
    uint16_t rel2 = 0;
    IMPULSE_TESTCASE(IMP_INST_IS_EXTENDED(inst));
    if (IMP_INST_IS_EXTENDED(inst)) {
        if (vm_state->pc + 1 >= instruction_count) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        src = IMP_INST_U8_0(inst);
        rel1 = IMP_INST_DST(bytecode[vm_state->pc + 1]);
        rel2 = IMP_INST_HIGH16(inst);
        if (rel2 == 0) rel2 = rel1;
        vm_state->pc++;
    } else {
        src = OP_0E_DEFAULT_SRC_REG;
        rel1 = IMP_INST_LOW16(inst);
        rel2 = IMP_INST_HIGH16(inst);
    }
    VALIDATE_REG(dst);
    VALIDATE_REG(src);
    IMPULSE_ASSERT(dst < IMP_VM_GPR_COUNT && src < IMP_VM_GPR_COUNT);
    IMP_REQUIRE_CONTEXT(vm_state);
    IMPULSE_ASSERT(vm_state->query_context != nullptr);

    IMP_REQUIRE_REL_SLOT(vm_state, rel1);
    IMP_REQUIRE_REL_SLOT(vm_state, rel2);
    IMPULSE_TESTCASE(rel1 == rel2);
    const auto& slot1 = vm_state->query_context->slots[rel1];
    const auto& slot2 = vm_state->query_context->slots[rel2];

    bool src_is_bitset = false;
    int h_src = -1;
    uint64_t scalar_src = 0;

    if (IMP_INST_HAS_FLAG(inst, IMPULSE_VM_OP_FLAG_INPUT_SEED)) {
        scalar_src = input_param;
        src_is_bitset = false;
    } else {
        IMPULSE_CHECK_REG_TYPE(vm_state, src,
            IMP_TYPE_BIT(TYPE_BITSET_HANDLE) | IMP_TYPES_INT_OR_NODE |
            IMP_TYPE_BIT(TYPE_NODE_VECTOR) | IMP_TYPE_BIT(TYPE_UINT64_VECTOR) |
            IMP_TYPE_BIT(TYPE_CSR_SPAN) | IMP_TYPE_BIT(TYPE_NULL));
        src_is_bitset = (vm_state->register_types[src] == TYPE_BITSET_HANDLE);
        h_src = src_is_bitset ? static_cast<int>(vm_state->registers[src]) : -1;
        scalar_src = !src_is_bitset ? vm_state->registers[src] : 0;
    }
    IMPULSE_TESTCASE(src_is_bitset);
    IMPULSE_TESTCASE(dst == src && src_is_bitset);
    if (src_is_bitset) {
        IMP_REQUIRE_BITSET_HANDLE(vm_state, h_src);
    }

    int h_dst = -1;
    if (vm_state->register_types[dst] == TYPE_BITSET_HANDLE && !(dst == src && src_is_bitset)) {
        h_dst = static_cast<int>(vm_state->registers[dst]);
        vm_state->query_context->bitsets[h_dst].clear();
    } else {
        h_dst = acquire_bitset(vm_state->query_context);
        if (h_dst < 0) {
            return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        }
        IMPULSE_ASSERT(h_dst >= 0 && static_cast<size_t>(h_dst) < vm_state->query_context->bitsets.size());
        vm_state->query_context->bitsets[h_dst].clear();
    }

    auto& bs_dst = vm_state->query_context->bitsets[h_dst];

    if (slot1.offsets_ptr && slot1.targets_ptr && slot2.offsets_ptr && slot2.targets_ptr) {
        if (src_is_bitset) {
            const auto& bs_src = vm_state->query_context->bitsets[h_src];
            for (size_t w = 0; w < vm_state->query_context->words_per_bitset; ++w) {
                uint64_t word = bs_src.words[w];
                while (word) {
                    int bit = std::countr_zero(word);
                    uint64_t u = IMP_WORD_BIT_TO_NODE(w, bit);
                    word = IMP_CLEAR_LOWEST_BIT(word);
                    if (u < slot1.node_count) {
                        uint64_t start1 = slot1.get_csr_offset(u);
                        uint64_t end1 = slot1.get_csr_offset(u + 1);
                        IMPULSE_WELL_FORMED_ASSERT(start1 <= end1);
                        for (uint64_t i = start1; i < end1; ++i) {
                            uint64_t v = slot1.get_csr_target(i);
                            if (v < slot2.node_count) {
                                uint64_t start2 = slot2.get_csr_offset(v);
                                uint64_t end2 = slot2.get_csr_offset(v + 1);
                                IMPULSE_WELL_FORMED_ASSERT(start2 <= end2);
                                for (uint64_t j = start2; j < end2; ++j) {
                                    bitset_add(bs_dst, slot2.get_csr_target(j), vm_state->query_context->max_nodes);
                                }
                            }
                        }
                    }
                }
            }
        } else {
            uint64_t u = scalar_src;
            if (u < slot1.node_count) {
                uint64_t start1 = slot1.get_csr_offset(u);
                uint64_t end1 = slot1.get_csr_offset(u + 1);
                IMPULSE_WELL_FORMED_ASSERT(start1 <= end1);
                IMPULSE_TESTCASE(start1 == end1);
                for (uint64_t i = start1; i < end1; ++i) {
                    uint64_t v = slot1.get_csr_target(i);
                    if (v < slot2.node_count) {
                        uint64_t start2 = slot2.get_csr_offset(v);
                        uint64_t end2 = slot2.get_csr_offset(v + 1);
                        IMPULSE_WELL_FORMED_ASSERT(start2 <= end2);
                        for (uint64_t j = start2; j < end2; ++j) {
                            bitset_add(bs_dst, slot2.get_csr_target(j), vm_state->query_context->max_nodes);
                        }
                    }
                }
            }
        }
    }

    if (dst == src && src_is_bitset) {
        release_bitset(vm_state->query_context, h_src);
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
    if (IMP_INST_HAS_FLAG(inst, IMPULSE_VM_OP_FLAG_HALT_ON_EMPTY)) {
        IMPULSE_TESTCASE(is_empty);
        impulse_vm_branch_coverage_record(static_cast<uint32_t>(vm_state->pc), OP_CSR_WALK_2HOP, 2u,
                                          IMPULSE_COV_KIND_TWO_WAY, IMPULSE_COV_BOTH,
                                          is_empty ? 0u : 1u);
    }
    IMP_VM_SET_ZF(vm_state, is_empty);
    if (is_empty && IMP_INST_HAS_FLAG(inst, IMPULSE_VM_OP_FLAG_HALT_ON_EMPTY)) {
        if (instruction_count > 0 && bytecode[instruction_count - 1].opcode == OP_HALT) {
            uint16_t halt_reg = IMP_INST_DST(bytecode[instruction_count - 1]);
            if (halt_reg < IMP_VM_GPR_COUNT && halt_reg != dst) {
                vm_state->registers[halt_reg] = static_cast<uint64_t>(h_dst);
                vm_state->register_types[halt_reg] = TYPE_BITSET_HANDLE;
            }
        }
        vm_state->pc = instruction_count;
        return IMPULSE_VM_OK;
    }

    vm_state->pc++;
    DISPATCH();
}

