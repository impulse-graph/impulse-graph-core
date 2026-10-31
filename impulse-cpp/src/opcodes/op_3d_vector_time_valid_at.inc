// Opcode 0x3D: OP_VECTOR_TIME_VALID_AT
op_VECTOR_TIME_VALID_AT: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t time_reg = inst.payload & 0xFF;
    uint16_t rel_id = (inst.payload >> 8) & 0xFF;
    uint16_t attr_start = (inst.payload >> 16) & 0xFF;
    uint16_t attr_end = (inst.payload >> 24) & 0xFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(time_reg);

    int h_dst = acquire_bitset(vm_state->query_context);
    if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    auto& bs_dst = vm_state->query_context->bitsets[h_dst];
    bs_dst.clear();

    uint8_t time_type = vm_state->register_types[time_reg];
    uint64_t time_val = vm_state->registers[time_reg];

    uint32_t key_start = (rel_id > 0) ? ((rel_id << 16) | attr_start) : attr_start;
    uint32_t key_end = (rel_id > 0) ? ((rel_id << 16) | attr_end) : attr_end;

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
    if (node_count == 0) node_count = vm_state->query_context->max_nodes;

    bool include_u0 = (col_start != nullptr || col_end != nullptr) &&
        ((col_start && !col_start->int_data.empty() && col_start->int_data[0] != 0) ||
         (col_end && !col_end->int_data.empty() && col_end->int_data[0] != 0));

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
    if (is_empty) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
    else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

    vm_state->pc++;
    DISPATCH();
}

