/**
 * @file op_12_csr_degree.inc
 * @brief Formal Contract for OP_CSR_DEGREE (0x12).
 *
 * Mnemonic: OP_CSR_DEGREE
 * Opcode: 0x12
 * Description: Computes out-degree sum of input scalar node ID or BitSet active frontier.
 *
 * Operands:
 *  - dst = inst.dst_reg, src = payload & 0xFFFF, rel = (payload >> 16) & 0xFFFF
 *
 * Inputs:
 *  - src: TYPE_NODE_ID | TYPE_INT64 | TYPE_BITSET_HANDLE
 *
 * Outputs:
 *  - dst: TYPE_INT64
 *
 * Flags:
 *  - ZF: Set if computed degree is 0
 *
 * Context Requirement:
 *  - Requires context (query_context snapshot slots)
 */
#ifndef OP_12_CONTRACT_DEFINED
#define OP_12_CONTRACT_DEFINED 1
#define OP_12_ALLOWED_SRC_TYPES (IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_BITSET_HANDLE) | IMP_TYPE_BIT(TYPE_NULL))
#endif

// Opcode 0x12: OP_CSR_DEGREE
op_CSR_DEGREE: {
    const auto& inst = bytecode[vm_state->pc];
    const uint16_t dst = IMP_INST_DST(inst);
    const uint16_t src = IMP_INST_LOW16(inst);
    uint16_t rel;
    IMPULSE_TESTCASE(IMP_INST_IS_EXTENDED(inst));
    if (IMP_INST_IS_EXTENDED(inst)) {
        if (vm_state->pc + 1 >= instruction_count) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        rel = IMP_INST_DST(bytecode[vm_state->pc + 1]);
        vm_state->pc++;
    } else {
        rel = IMP_INST_HIGH16(inst);
    }
    VALIDATE_REG(dst);
    VALIDATE_REG(src);
    IMPULSE_ASSERT(dst < IMP_VM_GPR_COUNT && src < IMP_VM_GPR_COUNT);
    IMP_REQUIRE_CONTEXT(vm_state);
    IMPULSE_ASSERT(vm_state->query_context != nullptr);
    IMPULSE_CHECK_REG_TYPE(vm_state, src, OP_12_ALLOWED_SRC_TYPES);

    IMP_REQUIRE_REL_SLOT(vm_state, rel);
    const auto& slot = vm_state->query_context->slots[rel];

    uint64_t degree = 0;
    uint8_t stype = vm_state->register_types[src];
    IMPULSE_TESTCASE(stype == TYPE_BITSET_HANDLE);
    IMPULSE_TESTCASE(stype == TYPE_NULL);
    if (stype == TYPE_NODE_ID || stype == TYPE_INT64) {
        uint64_t u = vm_state->registers[src];
        if (slot.offsets_ptr && u < slot.node_count) {
            uint64_t start = slot.get_csr_offset(u);
            uint64_t end = slot.get_csr_offset(u + 1);
            IMPULSE_WELL_FORMED_ASSERT(start <= end);
            degree = end - start;
        }
    } else if (stype == TYPE_BITSET_HANDLE && slot.offsets_ptr) {
        int h_src = static_cast<int>(vm_state->registers[src]);
        const auto& bs_src = vm_state->query_context->bitsets[h_src];
        for (size_t w = 0; w < vm_state->query_context->words_per_bitset; ++w) {
            uint64_t word = bs_src.words[w];
            while (word) {
                int bit = std::countr_zero(word);
                uint64_t u = IMP_WORD_BIT_TO_NODE(w, bit);
                word = IMP_CLEAR_LOWEST_BIT(word);
                if (u < slot.node_count) {
                    uint64_t start = slot.get_csr_offset(u);
                    uint64_t end = slot.get_csr_offset(u + 1);
                    IMPULSE_WELL_FORMED_ASSERT(start <= end);
                    degree += (end - start);
                }
            }
        }
    } else if ((stype == TYPE_NODE_VECTOR || stype == TYPE_UINT64_VECTOR) && slot.offsets_ptr) {
        const uint64_t* vec_ptr = nullptr;
        size_t vec_len = 0;
        if (get_node_vector_slice(vm_state->query_context, vm_state->registers[src], &vec_ptr, &vec_len)) {
            for (size_t k = 0; k < vec_len; ++k) {
                uint64_t u = vec_ptr[k];
                if (u < slot.node_count) {
                    uint64_t start = slot.get_csr_offset(u);
                    uint64_t end = slot.get_csr_offset(u + 1);
                    IMPULSE_WELL_FORMED_ASSERT(start <= end);
                    degree += (end - start);
                }
            }
        }
    }

    vm_state->registers[dst] = degree;
    vm_state->register_types[dst] = TYPE_INT64;
    IMP_VM_SET_ZF(vm_state, degree == 0u);

    vm_state->pc++;
    DISPATCH();
}

