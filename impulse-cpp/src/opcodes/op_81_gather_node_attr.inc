// Opcode 0x81: OP_GATHER_NODE_ATTR
op_GATHER_NODE_ATTR: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t frontier_reg;
    uint16_t attr_idx;
    uint16_t domain;
    if (inst.flags & 0x80) {
        if (vm_state->pc + 1 >= instruction_count) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        frontier_reg = inst.payload & 0xFFFF;
        attr_idx = (inst.payload >> 16) & 0xFFFF;
        domain = bytecode[vm_state->pc + 1].dst_reg;
        vm_state->pc++;
    } else {
        frontier_reg = inst.payload & 0xFF;
        attr_idx = (inst.payload >> 8) & 0xFF;
        domain = (inst.payload >> 16) & 0xFFFF;
    }
    VALIDATE_REG(dst);
    VALIDATE_REG(frontier_reg);

    bool front_is_bs = (vm_state->register_types[frontier_reg] == TYPE_BITSET_HANDLE);
    int h_front = front_is_bs ? static_cast<int>(vm_state->registers[frontier_reg]) : -1;
    uint64_t front_scalar = !front_is_bs ? vm_state->registers[frontier_reg] : 0;

    size_t N = vm_state->query_context->max_nodes;
    if (vm_state->register_types[dst] != TYPE_FLOAT_VECTOR) {
        int h = acquire_float_vector(vm_state->query_context);
        if (h < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        vm_state->registers[dst] = h;
        vm_state->register_types[dst] = TYPE_FLOAT_VECTOR;
    }
    int h_dst = static_cast<int>(vm_state->registers[dst]);
    float* dst_vec = vm_state->query_context->float_vectors[h_dst].data();

    uint32_t mock_key = (domain != 0) ? domain : attr_idx;
    const auto* ma_ptr = find_mock_node_attr(vm_state->query_context, mock_key);
    if (ma_ptr) {
        const auto& ma = *ma_ptr;
        size_t len = ma.float_data.size();
        if (len > 0) {
            vm_state->query_context->float_vector_lengths[h_dst] = len;
        }
        size_t limit = (len > 0) ? len : N;
        for (size_t i = 0; i < limit; ++i) {
            bool in_frontier = front_is_bs ? vm_state->query_context->bitsets[h_front].test(i) : true;
            if (in_frontier && (!ma.has_mask || ma.mask.test(i)) && i < ma.float_data.size()) {
                dst_vec[i] = ma.float_data[i];
            } else {
                dst_vec[i] = std::numeric_limits<float>::quiet_NaN();
            }
        }
    } else if (domain < vm_state->query_context->attribute_slots.size() &&
        attr_idx < vm_state->query_context->attribute_slots[domain].size()) {
        const auto& slot = vm_state->query_context->attribute_slots[domain][attr_idx];
        if (slot.data_ptr) {
            const float* src_attr = static_cast<const float*>(slot.data_ptr);
            size_t attr_n = std::min(N, static_cast<size_t>(slot.data_bytes / sizeof(float)));
            vm_state->query_context->float_vector_lengths[h_dst] = attr_n;
            if (front_is_bs) {
                const auto& bs = vm_state->query_context->bitsets[h_front];
                #pragma omp parallel for schedule(static)
                for (size_t i = 0; i < attr_n; ++i) {
                    dst_vec[i] = bs.test(i) ? src_attr[i] : std::numeric_limits<float>::quiet_NaN();
                }
            } else {
                for (size_t i = 0; i < attr_n; ++i) {
                    dst_vec[i] = (i == front_scalar) ? src_attr[i] : std::numeric_limits<float>::quiet_NaN();
                }
            }
        }
    }
    vm_state->pc++;
    DISPATCH();
}

