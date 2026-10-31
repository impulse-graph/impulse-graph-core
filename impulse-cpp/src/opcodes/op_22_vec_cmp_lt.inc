/**
 * @file op_22_vec_cmp_lt.inc
 * @brief Formal Contract for OP_VEC_CMP_LT (0x22).
 *
 * Mnemonic: OP_VEC_CMP_LT
 * Opcode: 0x22
 * Description: Vector elementwise less-than comparison returning BitSet mask.
 *
 * Operands:
 *  - dst = inst.dst_reg, src1 = payload & 0xFF, src2 = (payload >> 8) & 0xFF
 *
 * Inputs:
 *  - src1, src2: NUMERIC_VEC | INT_OR_NODE | TYPE_FLOAT
 *
 * Outputs:
 *  - dst: TYPE_BITSET_HANDLE
 *
 * Flags:
 *  - ZF: Set if comparison mask is empty
 *
 * Context Requirement:
 *  - Requires context (query_context bitset pool)
 */
#ifndef OP_22_CONTRACT_DEFINED
#define OP_22_CONTRACT_DEFINED 1
#endif

// Opcode 0x22: OP_VEC_CMP_LT
op_VEC_CMP_LT: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src1 = inst.payload & 0xFF;
    uint16_t src2 = (inst.payload >> 8) & 0xFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src1);
    VALIDATE_REG(src2);
    if (!vm_state->query_context) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    IMPULSE_CHECK_REG_TYPE(vm_state, src1, (IMP_TYPES_NUMERIC_VEC | IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_FLOAT)));
    IMPULSE_CHECK_REG_TYPE(vm_state, src2, (IMP_TYPES_NUMERIC_VEC | IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_FLOAT)));

    uint8_t type1 = vm_state->register_types[src1];
    uint64_t val1 = vm_state->registers[src1];
    uint8_t type2 = vm_state->register_types[src2];
    uint64_t val2 = vm_state->registers[src2];

    int h_dst = -1;
    if (vm_state->register_types[dst] == TYPE_BITSET_HANDLE) {
        h_dst = static_cast<int>(vm_state->registers[dst]);
    } else {
        h_dst = acquire_bitset(vm_state->query_context);
        if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
    auto& bs = vm_state->query_context->bitsets[h_dst];
    bs.clear();

    if (type1 == TYPE_DOUBLE_VECTOR) {
        int h_src = static_cast<int>(val1);
        size_t N = (h_src >= 0 && h_src < static_cast<int>(VM_MAX_VECTOR_HANDLES) && vm_state->query_context->double_vector_lengths[h_src] > 0)
            ? vm_state->query_context->double_vector_lengths[h_src] : vm_state->query_context->max_nodes;
        const double* v = vm_state->query_context->double_vectors[h_src].data();
        if (type2 == TYPE_DOUBLE_VECTOR) {
            int h_src2 = static_cast<int>(val2);
            const double* v2 = vm_state->query_context->double_vectors[h_src2].data();
            for (size_t i = 0; i < N; ++i) {
                if (v[i] < v2[i]) bs.set(i);
            }
        } else {
            double threshold = (type2 == TYPE_DOUBLE) ?
                std::bit_cast<double>(val2) : static_cast<double>(static_cast<int64_t>(val2));
            for (size_t i = 0; i < N; ++i) {
                if (v[i] < threshold) bs.set(i);
            }
        }
    } else if (type1 == TYPE_FLOAT_VECTOR) {
        int h_src = static_cast<int>(val1);
        size_t N = (h_src >= 0 && h_src < static_cast<int>(VM_MAX_VECTOR_HANDLES) && vm_state->query_context->float_vector_lengths[h_src] > 0)
            ? vm_state->query_context->float_vector_lengths[h_src] : vm_state->query_context->max_nodes;
        const float* v = vm_state->query_context->float_vectors[h_src].data();
        if (v) {
            if (type2 == TYPE_FLOAT_VECTOR) {
                int h_src2 = static_cast<int>(val2);
                const float* v2 = vm_state->query_context->float_vectors[h_src2].data();
                for (size_t i = 0; i < N; ++i) {
                    if (v[i] < v2[i]) bs.set(i);
                }
            } else {
                float threshold = (type2 == TYPE_FLOAT) ?
                    std::bit_cast<float>(static_cast<uint32_t>(val2)) : static_cast<float>(static_cast<int64_t>(val2));
                for (size_t i = 0; i < N; ++i) {
                    if (v[i] < threshold) bs.set(i);
                }
            }
        }
    }

    vm_state->registers[dst] = static_cast<uint64_t>(h_dst);
    vm_state->register_types[dst] = TYPE_BITSET_HANDLE;

    bool is_empty = true;
    for (size_t w = 0; w < vm_state->query_context->words_per_bitset; ++w) {
        if (bs.words[w] != 0) { is_empty = false; break; }
    }
    if (is_empty) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
    else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

    vm_state->pc++;
    DISPATCH();
}

