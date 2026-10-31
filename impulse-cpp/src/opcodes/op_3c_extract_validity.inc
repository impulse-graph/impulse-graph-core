/**
 * Formal Opcode Contract: OP_EXTRACT_VALIDITY (0x3C)
 *
 * Layout:
 *   dst_reg:          inst.dst_reg (DST_REG)
 *   src_reg:          inst.payload & 0xFFFF (PAYLOAD_LOW16)
 *   allowed_src:      IMP_TYPES_NUMERIC_VEC
 *   output_dst:       TYPE_BITSET_HANDLE
 *   flags:            ZF set if validity bitset is empty
 *   requires_ctx:     true
 */
#ifndef OP_3C_CONTRACT_DEFINED
#define OP_3C_CONTRACT_DEFINED
#define OP_3C_DEFINED_FLAGS        0x00u
#define OP_3C_ALLOWED_SRC_TYPES    (IMP_TYPES_NUMERIC_VEC)
#define OP_3C_OUTPUT_DST_TYPES     (IMP_TYPE_BIT(TYPE_BITSET_HANDLE))
#endif

// Opcode 0x3C: OP_EXTRACT_VALIDITY
op_EXTRACT_VALIDITY: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src = inst.payload & 0xFFFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src);
    if (!vm_state->query_context) return IMPULSE_VM_ERR_NULL_SNAPSHOT;
    IMPULSE_CHECK_REG_TYPE(vm_state, src, OP_3C_ALLOWED_SRC_TYPES);
    uint8_t stype = vm_state->register_types[src];

    int hDst = acquire_bitset(vm_state->query_context);
    if (hDst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    auto& bDst = vm_state->query_context->bitsets[hDst];
    bDst.clear();

    if (stype == TYPE_FLOAT_VECTOR) {
        int h1 = static_cast<int>(vm_state->registers[src]);
        auto& v1 = vm_state->query_context->float_vectors[h1];
        size_t len = vm_state->query_context->float_vector_lengths[h1];
        if (len == 0 || len > v1.size()) len = v1.size();
        for (size_t i = 0; i < len; ++i) {
            if (!std::isnan(v1[i])) {
                bitset_add(bDst, i, vm_state->query_context->max_nodes);
            }
        }
    } else if (stype == TYPE_DOUBLE_VECTOR) {
        int h1 = static_cast<int>(vm_state->registers[src]);
        auto& v1 = vm_state->query_context->double_vectors[h1];
        size_t len = vm_state->query_context->double_vector_lengths[h1];
        if (len == 0 || len > v1.size()) len = v1.size();
        for (size_t i = 0; i < len; ++i) {
            if (!std::isnan(v1[i])) {
                bitset_add(bDst, i, vm_state->query_context->max_nodes);
            }
        }
    } else {
        const uint64_t* ptr = nullptr;
        size_t len = 0;
        if (get_node_vector_slice(vm_state->query_context, vm_state->registers[src], &ptr, &len)) {
            for (size_t i = 0; i < len; ++i) {
                bitset_add(bDst, i, vm_state->query_context->max_nodes);
            }
        }
    }

    vm_state->registers[dst] = hDst;
    vm_state->register_types[dst] = TYPE_BITSET_HANDLE;

    bool is_empty = true;
    for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
        if (bDst.words[i] != 0) { is_empty = false; break; }
    }
    if (is_empty) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
    else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;
    vm_state->pc++;
    DISPATCH();
}

