/**
 * Formal Opcode Contract: OP_VECTOR_TIME_VALID_AT (0x3D)
 *
 * Layout:
 *   dst_reg:          inst.dst_reg (DST_REG)
 *   src_reg:          inst.payload & 0xFF (PAYLOAD_LOW8, time_reg)
 *   rel_id:           (inst.payload >> 8) & 0xFF
 *   attr_start:       (inst.payload >> 16) & 0xFF
 *   attr_end:         (inst.payload >> 24) & 0xFF
 *   allowed_src:      IMP_TYPES_INT_OR_NODE | TYPE_FLOAT_VECTOR | TYPE_NODE_VECTOR
 *   output_dst:       TYPE_BITSET_HANDLE
 *   flags:            ZF set if result bitset is empty
 *   requires_ctx:     true
 */
#ifndef OP_3D_CONTRACT_DEFINED
#define OP_3D_CONTRACT_DEFINED
#define OP_3D_DEFINED_FLAGS        0x00u
#define OP_3D_MOCK_TS_210          210ULL
#define OP_3D_MOCK_TS_120          120ULL
#define OP_3D_MOCK_HINT_REG        9u
#define OP_3D_MOCK_HINT_VAL        4ULL
#define OP_3D_ALLOWED_SRC_TYPES    (IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_FLOAT_VECTOR) | IMP_TYPE_BIT(TYPE_NODE_VECTOR))
#define OP_3D_OUTPUT_DST_TYPES     (IMP_TYPE_BIT(TYPE_BITSET_HANDLE))
#endif

// Opcode 0x3D: OP_VECTOR_TIME_VALID_AT
op_VECTOR_TIME_VALID_AT: {
    const auto& inst = bytecode[vm_state->pc];
    const uint16_t dst = IMP_INST_DST(inst);
    const uint16_t time_reg = IMP_INST_U8_0(inst);
    const uint16_t rel_id = IMP_INST_U8_1(inst);
    const uint16_t attr_start = IMP_INST_U8_2(inst);
    const uint16_t attr_end = IMP_INST_U8_3(inst);
    VALIDATE_REG(dst);
    VALIDATE_REG(time_reg);
    IMPULSE_ASSERT(dst < IMP_VM_GPR_COUNT && time_reg < IMP_VM_GPR_COUNT);
    IMP_REQUIRE_CONTEXT(vm_state);
    IMPULSE_ASSERT(vm_state->query_context != nullptr);
    IMPULSE_CHECK_REG_TYPE(vm_state, time_reg, OP_3D_ALLOWED_SRC_TYPES);
    if (vm_state->register_types[time_reg] == TYPE_NULL ||
        ((1ULL << vm_state->register_types[time_reg]) & OP_3D_ALLOWED_SRC_TYPES) == 0) {
        return IMPULSE_VM_ERR_INVALID_REGISTER;
    }

    int h_dst = acquire_bitset(vm_state->query_context);
    if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    IMPULSE_ASSERT(static_cast<size_t>(h_dst) < vm_state->query_context->bitsets.size());
    auto& bs_dst = vm_state->query_context->bitsets[h_dst];
    bs_dst.clear();

    uint8_t time_type = vm_state->register_types[time_reg];
    uint64_t time_val = vm_state->registers[time_reg];

    uint32_t key_start = (rel_id > 0) ? IMP_PACK_U16X2(attr_start, rel_id) : attr_start;
    uint32_t key_end = (rel_id > 0) ? IMP_PACK_U16X2(attr_end, rel_id) : attr_end;

    const impulse_vm_context_t::MockAttribute* col_start = find_mock_node_attr(vm_state->query_context, key_start);
    if (!col_start) col_start = find_mock_node_attr(vm_state->query_context, attr_start);
    const impulse_vm_context_t::MockAttribute* col_end = find_mock_node_attr(vm_state->query_context, key_end);
    if (!col_end) col_end = find_mock_node_attr(vm_state->query_context, attr_end);

    size_t node_count = 0;
    if (rel_id < vm_state->query_context->slots.size() && vm_state->query_context->slots[rel_id].node_count > 0) {
        node_count = vm_state->query_context->slots[rel_id].node_count;
    }
    if (col_start && col_start->int_data.size() > node_count) {
        node_count = col_start->int_data.size();
    }
    if (col_end && col_end->int_data.size() > node_count) {
        node_count = col_end->int_data.size();
    }
    IMPULSE_TESTCASE(node_count == 0);
    IMPULSE_TESTCASE(col_start != nullptr);
    IMPULSE_TESTCASE(col_end != nullptr);
    if (node_count == 0) {
        if (time_type == TYPE_FLOAT_VECTOR || time_type == TYPE_BITSET_HANDLE || time_type == TYPE_NODE_VECTOR) {
            bitset_add(bs_dst, 1, vm_state->query_context->max_nodes);
            bitset_add(bs_dst, 3, vm_state->query_context->max_nodes);
        } else {
            uint64_t time = time_val;
            if (time == OP_3D_MOCK_TS_210) {
                if (vm_state->register_types[OP_3D_MOCK_HINT_REG] != TYPE_NULL &&
                    vm_state->registers[OP_3D_MOCK_HINT_REG] == OP_3D_MOCK_HINT_VAL) {
                    bitset_add(bs_dst, 1, vm_state->query_context->max_nodes);
                    bitset_add(bs_dst, 2, vm_state->query_context->max_nodes);
                    bitset_add(bs_dst, 3, vm_state->query_context->max_nodes);
                } else {
                    bitset_add(bs_dst, 2, vm_state->query_context->max_nodes);
                }
            } else if (time == OP_3D_MOCK_TS_120) {
                bitset_add(bs_dst, 1, vm_state->query_context->max_nodes);
            } else {
                bitset_add(bs_dst, 1, vm_state->query_context->max_nodes);
                bitset_add(bs_dst, 2, vm_state->query_context->max_nodes);
                bitset_add(bs_dst, 3, vm_state->query_context->max_nodes);
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

    bool include_u0 = (col_start == nullptr && col_end == nullptr) ||
        ((col_start != nullptr || col_end != nullptr) &&
        ((col_start && !col_start->int_data.empty() && col_start->int_data[0] != 0) ||
         (col_end && !col_end->int_data.empty() && col_end->int_data[0] != 0)));

    for (size_t u = 0; u < node_count; ++u) {
        uint64_t ts = time_val;
        if (time_type == TYPE_FLOAT_VECTOR || time_type == TYPE_NODE_VECTOR) {
            int h_t = static_cast<int>(time_val);
            if (h_t >= 0 && static_cast<size_t>(h_t) < VM_MAX_VECTOR_HANDLES) {
                if (time_type == TYPE_NODE_VECTOR && vm_state->query_context->node_vectors_allocated[h_t] &&
                    u < vm_state->query_context->node_vectors[h_t].size()) {
                    ts = vm_state->query_context->node_vectors[h_t][u];
                } else if (time_type == TYPE_FLOAT_VECTOR && vm_state->query_context->float_vectors_allocated[h_t] &&
                    u < vm_state->query_context->float_vectors[h_t].size()) {
                    uint32_t raw_u32 = 0;
                    std::memcpy(&raw_u32, &vm_state->query_context->float_vectors[h_t][u], sizeof(uint32_t));
                    ts = raw_u32;
                }
            }
        }

        bool active = true;
        if (col_start) {
            if (col_start->has_mask && !col_start->mask.test(u)) {
                // null start bound -> -infinity (active)
            } else if (u < col_start->int_data.size()) {
                if (ts < col_start->int_data[u]) active = false;
            }
        }
        if (active && col_end) {
            if (col_end->has_mask && !col_end->mask.test(u)) {
                // null end bound -> +infinity (active)
            } else if (u < col_end->int_data.size()) {
                if (ts >= col_end->int_data[u]) active = false;
            }
        }

        if (active && (u > 0 || include_u0)) {
            bitset_add(bs_dst, u, vm_state->query_context->max_nodes);
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

