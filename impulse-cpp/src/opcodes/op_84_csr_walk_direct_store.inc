/**
 * @file op_84_csr_walk_direct_store.inc
 * @brief Formal Contract for OP_CSR_WALK_DIRECT_STORE (0x84).
 *
 * Syntax: OP_CSR_WALK_DIRECT_STORE <dst_reg>, <src_reg> (payload: [rel_id:16 | ... | src:8])
 * Inputs:
 *   - dst_reg: Destination bitset register directly storing visited target bits
 *   - src_reg (payload_low8): Source frontier bitset or scalar node ID
 * Preconditions:
 *   - src_reg must be TYPE_INT64, TYPE_NODE_ID, or TYPE_BITSET_HANDLE
 * Outputs:
 *   - dst_reg set to TYPE_BITSET_HANDLE
 *   - Flags: ZF set if traversal reached zero target nodes
 */
#ifndef OP_84_CONTRACT_DEFINED
#define OP_84_CONTRACT_DEFINED
#define OP_84_DEFINED_FLAGS        0x80u
#define OP_84_ALLOWED_SRC_TYPES    (IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_BITSET_HANDLE))
#define OP_84_OUTPUT_DST_TYPES     (IMP_TYPE_BIT(TYPE_BITSET_HANDLE))
#endif

// Opcode 0x84: OP_CSR_WALK_DIRECT_STORE
op_CSR_WALK_DIRECT_STORE: {
    IMPULSE_ASSERT(vm_state != nullptr);
    const auto& inst = bytecode[vm_state->pc];
    const uint16_t dst = IMP_INST_DST(inst);
    const uint16_t src = IMP_INST_U8_0(inst);
    uint16_t rel;
    const bool is_ext = IMP_INST_IS_EXTENDED(inst);
    IMPULSE_TESTCASE(is_ext);
    if (is_ext) {
        if (vm_state->pc + 1 >= instruction_count) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        rel = IMP_INST_DST(bytecode[vm_state->pc + 1]);
        vm_state->pc++;
    } else {
        rel = IMP_INST_HIGH16(inst);
    }
    IMP_VALIDATE_GPR(dst);
    IMP_VALIDATE_GPR(src);

    IMP_REQUIRE_CONTEXT(vm_state);
    IMPULSE_ASSERT(vm_state->query_context != nullptr);
    IMPULSE_ASSERT(dst < IMP_VM_GPR_COUNT && src < IMP_VM_GPR_COUNT);
    IMPULSE_CHECK_REG_TYPE(vm_state, src, OP_84_ALLOWED_SRC_TYPES);

    IMP_REQUIRE_REL_SLOT(vm_state, rel);
    const auto& slot = vm_state->query_context->slots[rel];
    if (!slot.offsets_ptr || !slot.targets_ptr) return IMPULSE_VM_ERR_NULL_SNAPSHOT;

    const uint8_t src_type = vm_state->register_types[src];
    const uint64_t src_val = vm_state->registers[src];
    const bool src_is_bitset = (src_type == TYPE_BITSET_HANDLE);
    IMPULSE_TESTCASE(src_is_bitset);
    IMPULSE_TESTCASE(dst == src);
    if (src_is_bitset && static_cast<size_t>(src_val) >= vm_state->query_context->bitsets.size()) {
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }

    int h_dst = -1;
    if (vm_state->register_types[dst] == TYPE_BITSET_HANDLE && !(dst == src && src_is_bitset) &&
        vm_state->registers[dst] < vm_state->query_context->bitsets.size()) {
        h_dst = static_cast<int>(vm_state->registers[dst]);
    } else {
        h_dst = acquire_bitset(vm_state->query_context);
        if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
    IMPULSE_ASSERT(h_dst >= 0 && static_cast<size_t>(h_dst) < vm_state->query_context->bitsets.size());
    auto& bs_dst = vm_state->query_context->bitsets[h_dst];
    bs_dst.clear();

    if (src_type == TYPE_NODE_ID || src_type == TYPE_INT64) {
        const uint64_t u = src_val;
        if (u < slot.node_count || slot.node_count == 0) {
            const uint64_t start = slot.get_csr_offset(u);
            const uint64_t end = slot.get_csr_offset(u + 1);
            IMPULSE_WELL_FORMED_ASSERT(start <= end);
            for (uint64_t e = start; e < end; ++e) {
                const uint64_t v = slot.get_csr_target(e);
                bs_dst.set(v);
            }
        }
    } else if (src_is_bitset) {
        const int h_src = static_cast<int>(src_val);
        IMPULSE_ASSERT(h_src >= 0 && static_cast<size_t>(h_src) < vm_state->query_context->bitsets.size());
        const auto& bs_src = vm_state->query_context->bitsets[h_src];
        const size_t N = (slot.node_count > 0) ? slot.node_count : vm_state->query_context->max_nodes;
        #pragma omp parallel for schedule(static)
        for (size_t u = 0; u < N; ++u) {
            if (bs_src.test(u)) {
                const uint64_t start = slot.get_csr_offset(u);
                const uint64_t end = slot.get_csr_offset(u + 1);
                IMPULSE_WELL_FORMED_ASSERT(start <= end);
                for (uint64_t e = start; e < end; ++e) {
                    const uint64_t v = slot.get_csr_target(e);
                    if (v < vm_state->query_context->max_nodes) {
                        #pragma omp atomic
                        bs_dst.words[IMP_WORD_INDEX(v)] |= IMP_BIT_MASK64(v);
                    }
                }
            }
        }
        if (dst == src) release_bitset(vm_state->query_context, h_src);
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
