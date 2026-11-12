/**
 * Formal Opcode Contract: OP_SET_CARDINALITY (0x33)
 *
 * Layout:
 *   dst_reg:          inst.dst_reg (DST_REG)
 *   src_reg:          inst.payload & 0xFFFF (PAYLOAD_LOW16)
 *   allowed_src:      IMP_TYPES_SOURCE_SET
 *   output_dst:       TYPE_INT64
 *   flags:            ZF set if count == 0
 *   requires_ctx:     true
 */
#ifndef OP_33_CONTRACT_DEFINED
#define OP_33_CONTRACT_DEFINED
#define OP_33_DEFINED_FLAGS        0x00u
#define OP_33_ALLOWED_SRC_TYPES    (IMP_TYPES_SOURCE_SET | IMP_TYPE_BIT(TYPE_LANE_FRONTIER) | IMP_TYPE_BIT(TYPE_NULL))
#define OP_33_OUTPUT_DST_TYPES     (IMP_TYPE_BIT(TYPE_INT64))
#endif

// Opcode 0x33: OP_SET_CARDINALITY
op_SET_CARDINALITY: {
    const auto& inst = bytecode[vm_state->pc];
    const uint16_t dst = IMP_INST_DST(inst);
    const uint16_t src = IMP_INST_LOW16(inst);
    VALIDATE_REG(dst);
    VALIDATE_REG(src);
    IMPULSE_ASSERT(dst < IMP_VM_GPR_COUNT && src < IMP_VM_GPR_COUNT);
    IMP_REQUIRE_CONTEXT(vm_state);
    IMPULSE_ASSERT(vm_state->query_context != nullptr);
    IMPULSE_CHECK_REG_TYPE(vm_state, src, OP_33_ALLOWED_SRC_TYPES);

    uint64_t count = 0;
    uint8_t stype = vm_state->register_types[src];
    IMPULSE_TESTCASE(stype == TYPE_BITSET_HANDLE);
    IMPULSE_TESTCASE(stype == TYPE_NULL);
    IMPULSE_TESTCASE(dst == src);
    if (stype == TYPE_BITSET_HANDLE) {
        int handle = static_cast<int>(vm_state->registers[src]);
        IMPULSE_ASSERT(handle >= 0 && static_cast<size_t>(handle) < vm_state->query_context->bitsets.size());
        const auto& bs = vm_state->query_context->bitsets[handle];
        for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
            count += std::popcount(bs.words[i]);
        }
    } else if (stype == TYPE_NODE_ID || stype == TYPE_INT64) {
        count = 1;
    } else if (stype == TYPE_NODE_VECTOR || stype == TYPE_UINT64_VECTOR) {
        const uint64_t* ptr = nullptr;
        size_t len = 0;
        if (get_node_vector_slice(vm_state->query_context, vm_state->registers[src], &ptr, &len)) {
            auto& tmp_bs = vm_state->query_context->private_bitsets[0];
            tmp_bs.clear();
            bool has_oob = false;
            for (size_t i = 0; i < len; ++i) {
                uint64_t u = ptr[i];
                if (u < tmp_bs.word_count * IMP_BITS_PER_WORD) {
                    bitset_add(tmp_bs, u, vm_state->query_context->max_nodes);
                } else {
                    has_oob = true;
                }
            }
            IMPULSE_TESTCASE(has_oob);
            for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                count += std::popcount(tmp_bs.words[i]);
            }
            if (has_oob) count++;
        }
    }

    vm_state->registers[dst] = count;
    vm_state->register_types[dst] = TYPE_INT64;

    IMP_VM_SET_ZF(vm_state, count == 0u);

    vm_state->pc++;
    DISPATCH();
}

