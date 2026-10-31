/**
 * @file op_23_vec_cmp_between.inc
 * @brief Formal Contract for OP_VEC_CMP_BETWEEN (0x23).
 *
 * Mnemonic: OP_VEC_CMP_BETWEEN
 * Opcode: 0x23
 * Description: Vector elementwise range check (low <= x <= high) returning BitSet mask.
 *
 * Operands:
 *  - dst = inst.dst_reg, src_vec = payload & 0xFF, r_min = (payload >> 8) & 0xFF, r_max = (payload >> 16) & 0xFF
 *
 * Inputs:
 *  - src_vec: NUMERIC_VEC, r_min, r_max: NUMERIC_VEC | INT_OR_NODE | TYPE_FLOAT
 *
 * Outputs:
 *  - dst: TYPE_BITSET_HANDLE
 *
 * Flags:
 *  - ZF: Set if range match mask is empty
 *
 * Context Requirement:
 *  - Requires context (query_context bitset pool)
 */
#ifndef OP_23_CONTRACT_DEFINED
#define OP_23_CONTRACT_DEFINED 1
#endif

// Opcode 0x23: OP_VEC_CMP_BETWEEN
op_VEC_CMP_BETWEEN: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src_vec = inst.payload & 0xFF;
    uint16_t r_min = (inst.payload >> 8) & 0xFF;
    uint16_t r_max = (inst.payload >> 16) & 0xFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src_vec);
    VALIDATE_REG(r_min);
    VALIDATE_REG(r_max);
    if (!vm_state->query_context) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    IMPULSE_CHECK_REG_TYPE(vm_state, src_vec, (IMP_TYPES_NUMERIC_VEC | IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_FLOAT) | IMP_TYPE_BIT(TYPE_NULL)));
    IMPULSE_CHECK_REG_TYPE(vm_state, r_min, (IMP_TYPES_NUMERIC_VEC | IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_FLOAT) | IMP_TYPE_BIT(TYPE_NULL)));
    IMPULSE_CHECK_REG_TYPE(vm_state, r_max, (IMP_TYPES_NUMERIC_VEC | IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_FLOAT) | IMP_TYPE_BIT(TYPE_NULL)));

    uint8_t type_vec = vm_state->register_types[src_vec];
    uint64_t val_vec = vm_state->registers[src_vec];
    uint8_t type_min = vm_state->register_types[r_min];
    uint64_t val_min = vm_state->registers[r_min];
    uint8_t type_max = vm_state->register_types[r_max];
    uint64_t val_max = vm_state->registers[r_max];

    int h_dst = -1;
    if (vm_state->register_types[dst] == TYPE_BITSET_HANDLE) {
        h_dst = static_cast<int>(vm_state->registers[dst]);
    } else {
        h_dst = acquire_bitset(vm_state->query_context);
        if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
    auto& bs = vm_state->query_context->bitsets[h_dst];
    bs.clear();

    if (type_vec == TYPE_DOUBLE_VECTOR) {
        int h_src = static_cast<int>(val_vec);
        size_t N = (h_src >= 0 && h_src < static_cast<int>(VM_MAX_VECTOR_HANDLES) && vm_state->query_context->double_vector_lengths[h_src] > 0)
            ? vm_state->query_context->double_vector_lengths[h_src] : vm_state->query_context->max_nodes;
        const double* v = vm_state->query_context->double_vectors[h_src].data();
        double min_val = (type_min == TYPE_DOUBLE) ? std::bit_cast<double>(val_min) : static_cast<double>(static_cast<int64_t>(val_min));
        double max_val = (type_max == TYPE_DOUBLE) ? std::bit_cast<double>(val_max) : static_cast<double>(static_cast<int64_t>(val_max));
        for (size_t i = 0; i < N; ++i) {
            if (v[i] >= min_val && v[i] <= max_val) bs.set(i);
        }
    } else if (type_vec == TYPE_FLOAT_VECTOR) {
        int h_src = static_cast<int>(val_vec);
        size_t N = (h_src >= 0 && h_src < static_cast<int>(VM_MAX_VECTOR_HANDLES) && vm_state->query_context->float_vector_lengths[h_src] > 0)
            ? vm_state->query_context->float_vector_lengths[h_src] : vm_state->query_context->max_nodes;
        const float* v = vm_state->query_context->float_vectors[h_src].data();
        float min_val = (type_min == TYPE_FLOAT) ? std::bit_cast<float>(static_cast<uint32_t>(val_min)) : static_cast<float>(static_cast<int64_t>(val_min));
        float max_val = (type_max == TYPE_FLOAT) ? std::bit_cast<float>(static_cast<uint32_t>(val_max)) : static_cast<float>(static_cast<int64_t>(val_max));
        for (size_t i = 0; i < N; ++i) {
            if (v[i] >= min_val && v[i] <= max_val) bs.set(i);
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

