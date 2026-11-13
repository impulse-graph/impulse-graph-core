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
#define OP_22_ALLOWED_OPERAND_TYPES (IMP_TYPES_NUMERIC_VEC | IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_FLOAT) | IMP_TYPE_BIT(TYPE_DOUBLE) | IMP_TYPE_BIT(TYPE_NULL) | IMP_TYPE_BIT(TYPE_BITSET_HANDLE))
#endif

// Opcode 0x22: OP_VEC_CMP_LT
op_VEC_CMP_LT: {
    const auto& inst = bytecode[vm_state->pc];
    const uint16_t dst = IMP_INST_DST(inst);
    const uint16_t src1 = IMP_INST_U8_0(inst);
    const uint16_t src2 = IMP_INST_U8_1(inst);
    VALIDATE_REG(dst);
    VALIDATE_REG(src1);
    VALIDATE_REG(src2);
    IMPULSE_ASSERT(dst < IMP_VM_GPR_COUNT && src1 < IMP_VM_GPR_COUNT && src2 < IMP_VM_GPR_COUNT);
    IMPULSE_TESTCASE(vm_state->query_context == nullptr);
    if (!vm_state->query_context) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    IMPULSE_ASSERT(vm_state->query_context != nullptr);
    IMPULSE_CHECK_REG_TYPE(vm_state, src1, OP_22_ALLOWED_OPERAND_TYPES);
    IMPULSE_CHECK_REG_TYPE(vm_state, src2, OP_22_ALLOWED_OPERAND_TYPES);

    uint8_t type1 = vm_state->register_types[src1];
    uint64_t val1 = vm_state->registers[src1];
    uint8_t type2 = vm_state->register_types[src2];
    uint64_t val2 = vm_state->registers[src2];
    if (type1 == TYPE_NULL || type2 == TYPE_NULL) {
        return IMPULSE_VM_ERR_INVALID_REGISTER;
    }
    IMPULSE_TESTCASE(src1 == src2);
    IMPULSE_TESTCASE(type1 == TYPE_DOUBLE_VECTOR);
    IMPULSE_TESTCASE(type1 == type2);

    int h_dst = -1;
    if (vm_state->register_types[dst] == TYPE_BITSET_HANDLE) {
        h_dst = static_cast<int>(vm_state->registers[dst]);
    } else {
        h_dst = acquire_bitset(vm_state->query_context);
        if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        IMPULSE_ASSERT(h_dst >= 0 && static_cast<size_t>(h_dst) < vm_state->query_context->bitsets.size());
    }
    auto& bs = vm_state->query_context->bitsets[h_dst];
    bs.clear();

    if (type1 == TYPE_DOUBLE_VECTOR) {
        int h_src = static_cast<int>(val1);
        size_t N = (h_src >= 0 && h_src < static_cast<int>(VM_MAX_VECTOR_HANDLES))
            ? std::min(vm_state->query_context->double_vector_lengths[h_src], vm_state->query_context->max_nodes)
            : vm_state->query_context->max_nodes;
        const double* v = vm_state->query_context->double_vectors[h_src].data();
        if (type2 == TYPE_DOUBLE_VECTOR) {
            int h_src2 = static_cast<int>(val2);
            size_t N2 = (h_src2 >= 0 && h_src2 < static_cast<int>(VM_MAX_VECTOR_HANDLES))
                ? std::min(vm_state->query_context->double_vector_lengths[h_src2], vm_state->query_context->max_nodes)
                : vm_state->query_context->max_nodes;
            N = std::min(N, N2);
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
        size_t N = (h_src >= 0 && h_src < static_cast<int>(VM_MAX_VECTOR_HANDLES))
            ? std::min(vm_state->query_context->float_vector_lengths[h_src], vm_state->query_context->max_nodes)
            : vm_state->query_context->max_nodes;
        const float* v = vm_state->query_context->float_vectors[h_src].data();
        if (v) {
            if (type2 == TYPE_FLOAT_VECTOR) {
                int h_src2 = static_cast<int>(val2);
                size_t N2 = (h_src2 >= 0 && h_src2 < static_cast<int>(VM_MAX_VECTOR_HANDLES))
                    ? std::min(vm_state->query_context->float_vector_lengths[h_src2], vm_state->query_context->max_nodes)
                    : vm_state->query_context->max_nodes;
                N = std::min(N, N2);
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
    IMP_VM_SET_ZF(vm_state, is_empty);

    vm_state->pc++;
    DISPATCH();
}

