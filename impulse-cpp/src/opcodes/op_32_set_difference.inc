/**
 * Formal Opcode Contract: OP_SET_DIFFERENCE (0x32)
 *
 * Layout:
 *   dst_reg:          inst.dst_reg (DST_REG)
 *   src_reg:          inst.payload & 0xFFFF (PAYLOAD_LOW16)
 *   aux1_reg:         (inst.payload >> 16) & 0xFFFF (PAYLOAD_HIGH16)
 *   allowed_src:      IMP_TYPES_SOURCE_SET | TYPE_LANE_FRONTIER
 *   allowed_aux1:     IMP_TYPES_SOURCE_SET | TYPE_LANE_FRONTIER
 *   output_dst:       TYPE_BITSET_HANDLE | TYPE_LANE_FRONTIER
 *   flags:            ZF set if empty
 *   requires_ctx:     true
 */
#ifndef OP_32_CONTRACT_DEFINED
#define OP_32_CONTRACT_DEFINED
#define OP_32_DEFINED_FLAGS        0x00u
#define OP_32_ALLOWED_SRC_TYPES    (IMP_TYPES_SOURCE_SET | IMP_TYPE_BIT(TYPE_NULL) | IMP_TYPES_FLOAT_OR_DBL | IMP_TYPE_BIT(TYPE_FLOAT) | IMP_TYPE_BIT(TYPE_DOUBLE))
#define OP_32_ALLOWED_AUX1_TYPES   (IMP_TYPES_SOURCE_SET | IMP_TYPE_BIT(TYPE_NULL) | IMP_TYPES_FLOAT_OR_DBL | IMP_TYPE_BIT(TYPE_FLOAT) | IMP_TYPE_BIT(TYPE_DOUBLE))
#define OP_32_OUTPUT_DST_TYPES     (IMP_TYPE_BIT(TYPE_BITSET_HANDLE))
#endif

// Opcode 0x32: OP_SET_DIFFERENCE
op_SET_DIFFERENCE: {
    const auto& inst = bytecode[vm_state->pc];
    const uint16_t dst = IMP_INST_DST(inst);
    const uint16_t src1 = IMP_INST_LOW16(inst);
    const uint16_t src2 = IMP_INST_HIGH16(inst);
    VALIDATE_REG(dst);
    VALIDATE_REG(src1);
    VALIDATE_REG(src2);
    IMPULSE_ASSERT(dst < IMP_VM_GPR_COUNT && src1 < IMP_VM_GPR_COUNT && src2 < IMP_VM_GPR_COUNT);
    IMP_REQUIRE_CONTEXT(vm_state);
    IMPULSE_ASSERT(vm_state->query_context != nullptr);
    IMPULSE_CHECK_REG_TYPE(vm_state, src1, OP_32_ALLOWED_SRC_TYPES);
    IMPULSE_CHECK_REG_TYPE(vm_state, src2, OP_32_ALLOWED_AUX1_TYPES);

    uint8_t type1 = vm_state->register_types[src1];
    uint64_t val1 = vm_state->registers[src1];
    uint8_t type2 = vm_state->register_types[src2];
    uint64_t val2 = vm_state->registers[src2];
    if (type1 == TYPE_LANE_FRONTIER || type2 == TYPE_LANE_FRONTIER) {
        IMPULSE_TESTCASE(true);
        return IMPULSE_VM_ERR_INVALID_REGISTER;
    }
    bool dst_was_bitset = (vm_state->register_types[dst] == TYPE_BITSET_HANDLE);
    IMPULSE_TESTCASE(dst == src1);
    IMPULSE_TESTCASE(dst == src2);
    IMPULSE_TESTCASE(src1 == src2);

    int h_dst = -1;
    if (dst_was_bitset) {
        h_dst = static_cast<int>(vm_state->registers[dst]);
    } else {
        h_dst = acquire_bitset(vm_state->query_context);
        if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        vm_state->registers[dst] = static_cast<uint64_t>(h_dst);
        vm_state->register_types[dst] = TYPE_BITSET_HANDLE;
    }
    IMPULSE_ASSERT(h_dst >= 0 && static_cast<size_t>(h_dst) < vm_state->query_context->bitsets.size());
    auto& bs_dst = vm_state->query_context->bitsets[h_dst];

    auto load_into = [&](VmBitSet& bs, uint8_t rtype, uint64_t rval) {
        bs.clear();
        if (rtype == TYPE_BITSET_HANDLE) {
            int h_s = static_cast<int>(rval);
            IMPULSE_ASSERT(h_s >= 0 && static_cast<size_t>(h_s) < vm_state->query_context->bitsets.size());
            const auto& bs_s = vm_state->query_context->bitsets[h_s];
            for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                bs.words[i] = bs_s.words[i];
            }
        } else if (rtype == TYPE_NODE_ID || rtype == TYPE_INT64) {
            bitset_add(bs, rval, vm_state->query_context->max_nodes);
        } else if (rtype == TYPE_NODE_VECTOR || rtype == TYPE_UINT64_VECTOR) {
            const uint64_t* ptr = nullptr;
            size_t len = 0;
            if (get_node_vector_slice(vm_state->query_context, rval, &ptr, &len)) {
                for (size_t i = 0; i < len; ++i) {
                    bitset_add(bs, ptr[i], vm_state->query_context->max_nodes);
                }
            }
        }
    };

    auto diff_into = [&](VmBitSet& bs, uint8_t rtype, uint64_t rval) {
        if (rtype == TYPE_BITSET_HANDLE) {
            int h_s = static_cast<int>(rval);
            IMPULSE_ASSERT(h_s >= 0 && static_cast<size_t>(h_s) < vm_state->query_context->bitsets.size());
            const auto& bs_s = vm_state->query_context->bitsets[h_s];
            for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                bs.words[i] &= ~bs_s.words[i];
            }
        } else if (rtype == TYPE_NODE_ID || rtype == TYPE_INT64) {
            if (rval < bs.word_count * IMP_BITS_PER_WORD) {
                bs.words[IMP_WORD_INDEX(rval)] &= ~IMP_BIT_MASK64(IMP_BIT_IN_WORD(rval));
            }
        } else if (rtype == TYPE_NODE_VECTOR || rtype == TYPE_UINT64_VECTOR) {
            const uint64_t* ptr = nullptr;
            size_t len = 0;
            if (get_node_vector_slice(vm_state->query_context, rval, &ptr, &len)) {
                for (size_t i = 0; i < len; ++i) {
                    uint64_t u = ptr[i];
                    if (u < bs.word_count * IMP_BITS_PER_WORD) {
                        bs.words[IMP_WORD_INDEX(u)] &= ~IMP_BIT_MASK64(IMP_BIT_IN_WORD(u));
                    }
                }
            }
        }
    };

    if (dst == src1 && dst_was_bitset) {
        diff_into(bs_dst, type2, val2);
    } else if (dst == src2 && dst_was_bitset) {
        auto& tmp_bs = vm_state->query_context->private_bitsets[0];
        load_into(tmp_bs, type1, val1);
        for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
            bs_dst.words[i] = tmp_bs.words[i] & ~bs_dst.words[i];
        }
    } else {
        load_into(bs_dst, type1, val1);
        diff_into(bs_dst, type2, val2);
    }

    bool is_empty = true;
    for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
        if (bs_dst.words[i] != 0) {
            is_empty = false;
            break;
        }
    }

    IMP_VM_SET_ZF(vm_state, is_empty);

    vm_state->pc++;
    DISPATCH();
}

