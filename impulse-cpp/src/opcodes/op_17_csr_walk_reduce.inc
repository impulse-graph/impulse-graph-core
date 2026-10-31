// Opcode 0x17: OP_CSR_WALK_REDUCE
op_CSR_WALK_REDUCE: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint8_t src = inst.payload & 0xFF;
    uint8_t reduce_op = (inst.payload >> 8) & 0xFF;
    uint8_t rel_id = (inst.payload >> 16) & 0xFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src);

    if (rel_id >= vm_state->query_context->slots.size()) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    const auto& slot = vm_state->query_context->slots[rel_id];

    bool src_is_double = (vm_state->register_types[src] == TYPE_DOUBLE_VECTOR);
    size_t max_nodes = vm_state->query_context->max_nodes;

    if (vm_state->register_types[src] == TYPE_NODE_ID || vm_state->register_types[src] == TYPE_INT64) {
        int h_dst = acquire_float_vector(vm_state->query_context);
        if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        vm_state->registers[dst] = h_dst;
        vm_state->register_types[dst] = TYPE_FLOAT_VECTOR;
        vm_state->flags |= IMPULSE_VM_FLAG_ZF;
        vm_state->pc++;
        DISPATCH();
    }
    int h_dst = acquire_float_vector(vm_state->query_context);
    if (h_dst < 0) {
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
    float* dst_vec = vm_state->query_context->float_vectors[h_dst].data();
    std::fill(dst_vec, dst_vec + max_nodes, 0.0f);

    if (slot.offsets_ptr && slot.targets_ptr) {
        if (src_is_double) {
            const double* src_vec = vm_state->query_context->double_vectors[vm_state->registers[src]].data();
            for (uint64_t u = 0; u < slot.node_count; ++u) {
                double src_val = src_vec[u];
                uint64_t start = slot.get_csr_offset(u);
                uint64_t end   = slot.get_csr_offset(u + 1);
                for (uint64_t idx = start; idx < end; ++idx) {
                    uint64_t target_node = slot.get_csr_target(idx);
                    if (target_node < max_nodes) {
                        float val = static_cast<float>(src_val);
                        if (reduce_op == 0) {
                            if (dst_vec[target_node] == 0.0f || val < dst_vec[target_node]) dst_vec[target_node] = val;
                        } else if (reduce_op == 1) {
                            if (val > dst_vec[target_node]) dst_vec[target_node] = val;
                        } else {
                            dst_vec[target_node] = val;
                        }
                    }
                }
            }
        } else if (vm_state->register_types[src] == TYPE_FLOAT_VECTOR) {
            const float* src_vec = vm_state->query_context->float_vectors[vm_state->registers[src]].data();
            for (uint64_t u = 0; u < slot.node_count; ++u) {
                float src_val = src_vec[u];
                uint64_t start = slot.get_csr_offset(u);
                uint64_t end   = slot.get_csr_offset(u + 1);
                for (uint64_t idx = start; idx < end; ++idx) {
                    uint64_t target_node = slot.get_csr_target(idx);
                    if (target_node < max_nodes) {
                        float val = src_val;
                        if (reduce_op == 0) {
                            if (dst_vec[target_node] == 0.0f || val < dst_vec[target_node]) dst_vec[target_node] = val;
                        } else if (reduce_op == 1) {
                            if (val > dst_vec[target_node]) dst_vec[target_node] = val;
                        } else {
                            dst_vec[target_node] = val;
                        }
                    }
                }
            }
        }
    }

    vm_state->registers[dst] = h_dst;
    vm_state->register_types[dst] = TYPE_FLOAT_VECTOR;

    bool is_empty = true;
    for (size_t i = 0; i < max_nodes; ++i) {
        if (dst_vec[i] != 0.0f) { is_empty = false; break; }
    }
    if (is_empty) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
    else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

    vm_state->pc++;
    DISPATCH();
}

