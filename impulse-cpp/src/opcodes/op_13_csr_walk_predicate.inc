/**
 * @file op_13_csr_walk_predicate.inc
 * @brief Formal Contract for OP_CSR_WALK_PREDICATE (0x13).
 *
 * Mnemonic: OP_CSR_WALK_PREDICATE
 * Opcode: 0x13
 * Description: CSR forward traversal guarded by target predicate evaluation.
 *
 * Operands:
 *  - dst = inst.dst_reg, src = payload & 0xFF, rel = (payload >> 16) & 0xFFFF
 *
 * Inputs:
 *  - src: SOURCE_SET
 *
 * Outputs:
 *  - dst: TYPE_BITSET_HANDLE
 *
 * Flags:
 *  - ZF: Set if predicate frontier is empty
 *
 * Context Requirement:
 *  - Requires context (query_context snapshot slots and bitset pool)
 */
#ifndef OP_13_CONTRACT_DEFINED
#define OP_13_CONTRACT_DEFINED 1
#define OP_13_IS_VALID_TARGET(t) ((t) != IMP_SENTINEL_U32 && (t) != IMP_SENTINEL_U16 && (t) != IMP_INVALID_NODE_U64)
#endif

// Opcode 0x13: OP_CSR_WALK_PREDICATE
op_CSR_WALK_PREDICATE: {
    const auto& inst = bytecode[vm_state->pc];
    const uint16_t dst = IMP_INST_DST(inst);
    const uint16_t src = IMP_INST_U8_0(inst);
    uint16_t rel;
    IMPULSE_TESTCASE(IMP_INST_IS_EXTENDED(inst));
    if (IMP_INST_IS_EXTENDED(inst)) {
        if (vm_state->pc + 1 >= instruction_count) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        rel = IMP_INST_DST(bytecode[vm_state->pc + 1]);
        vm_state->pc++;
    } else {
        rel = IMP_INST_HIGH16(inst);
        if (rel == 0 && IMP_INST_U8_1(inst) != 0u) {
            rel = IMP_INST_U8_1(inst);
        }
    }
    VALIDATE_REG(dst);
    VALIDATE_REG(src);
    IMPULSE_ASSERT(dst < IMP_VM_GPR_COUNT && src < IMP_VM_GPR_COUNT);
    IMP_REQUIRE_CONTEXT(vm_state);
    IMPULSE_ASSERT(vm_state->query_context != nullptr);
    IMPULSE_CHECK_REG_TYPE(vm_state, src, IMP_TYPES_SOURCE_SET);

    IMP_REQUIRE_REL_SLOT(vm_state, rel);
    const auto& slot = vm_state->query_context->slots[rel];

    int h_dst = -1;
    IMPULSE_TESTCASE(dst == src);
    if (vm_state->register_types[dst] == TYPE_BITSET_HANDLE && dst != src) {
        h_dst = static_cast<int>(vm_state->registers[dst]);
        vm_state->query_context->bitsets[h_dst].clear();
    } else {
        h_dst = acquire_bitset(vm_state->query_context);
        if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        IMPULSE_ASSERT(h_dst >= 0 && static_cast<size_t>(h_dst) < vm_state->query_context->bitsets.size());
        vm_state->query_context->bitsets[h_dst].clear();
    }
    auto& bs_dst = vm_state->query_context->bitsets[h_dst];

    bool src_is_bitset = (vm_state->register_types[src] == TYPE_BITSET_HANDLE);
    IMPULSE_TESTCASE(src_is_bitset);
    int h_src = src_is_bitset ? static_cast<int>(vm_state->registers[src]) : -1;
    if (src_is_bitset &&
        (h_src < 0 || static_cast<size_t>(h_src) >= vm_state->query_context->bitsets.size() ||
         vm_state->query_context->bitsets[h_src].words == nullptr)) {
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
    uint64_t scalar_src = !src_is_bitset ? vm_state->registers[src] : 0;
    uint64_t max_nodes = vm_state->query_context->max_nodes;

    if (slot.offsets_ptr && slot.targets_ptr) {
        if (src_is_bitset) {
            #pragma omp parallel for schedule(dynamic, 64)
            for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                uint64_t w = vm_state->query_context->bitsets[h_src].words[i];
                if (w == 0) continue;
                for (uint32_t b = 0; b < IMP_BITS_PER_WORD; ++b) {
                    if (w & IMP_BIT_MASK64(b)) {
                        uint64_t u = IMP_WORD_BIT_TO_NODE(i, b);
                        if (u < slot.node_count) {
                            uint64_t start = slot.get_csr_offset(u);
                            uint64_t end   = slot.get_csr_offset(u + 1);
                            IMPULSE_WELL_FORMED_ASSERT(start <= end);
                            for (uint64_t idx = start; idx < end; ++idx) {
                                uint64_t target_node = slot.get_csr_target(idx);
                                if (OP_13_IS_VALID_TARGET(target_node)) {
                                    bitset_add_atomic(bs_dst, target_node, max_nodes);
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
                IMPULSE_WELL_FORMED_ASSERT(start <= end);
                IMPULSE_TESTCASE(start == end);
                for (uint64_t idx = start; idx < end; ++idx) {
                    uint64_t target_node = slot.get_csr_target(idx);
                    if (OP_13_IS_VALID_TARGET(target_node)) {
                        bitset_add(bs_dst, target_node, max_nodes);
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
    IMP_VM_SET_ZF(vm_state, is_empty);

    vm_state->pc++;
    DISPATCH();
}

