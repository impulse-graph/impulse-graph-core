/**
 * @file op_16_csr_walk_reduce_sum.inc
 * @brief Formal Contract for OP_CSR_WALK_REDUCE_SUM (0x16).
 *
 * Mnemonic: OP_CSR_WALK_REDUCE_SUM
 * Opcode: 0x16
 * Description: Traverses CSR edges accumulating edge/node attributes via parallel reduction sum.
 *
 * Operands:
 *  - dst = inst.dst_reg, src = payload & 0xFF, attr_id = (payload >> 8) & 0xFF, rel = payload >> 16
 *
 * Inputs:
 *  - src: TYPE_FLOAT_VECTOR | TYPE_DOUBLE_VECTOR | TYPE_INT64 | TYPE_NODE_ID
 *
 * Outputs:
 *  - dst: TYPE_INT64 | TYPE_FLOAT_VECTOR | TYPE_DOUBLE_VECTOR
 *
 * Flags:
 *  - ZF: Set if sum is zero
 *
 * Context Requirement:
 *  - Requires context (query_context snapshot and vector pool)
 */
#ifndef OP_16_CONTRACT_DEFINED
#define OP_16_CONTRACT_DEFINED 1
#define OP_16_ALLOWED_SRC_TYPES (IMP_TYPES_FLOAT_OR_DBL | IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_BITSET_HANDLE))
#endif

// Opcode 0x16: OP_CSR_WALK_REDUCE_SUM
op_CSR_WALK_REDUCE_SUM: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint8_t src = inst.payload & 0xFF;
    uint8_t attr_id = (inst.payload >> 8) & 0xFF;
    uint16_t rel_id;
    if (inst.flags & 0x80) {
        if (vm_state->pc + 1 >= instruction_count) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        rel_id = bytecode[vm_state->pc + 1].dst_reg;
        vm_state->pc++;
    } else {
        rel_id = (inst.payload >> 16) & 0xFFFF;
    }
    VALIDATE_REG(dst);
    VALIDATE_REG(src);
    if (!vm_state->query_context) return IMPULSE_VM_ERR_NULL_SNAPSHOT;
    IMPULSE_CHECK_REG_TYPE(vm_state, src, OP_16_ALLOWED_SRC_TYPES);

    if (rel_id >= vm_state->query_context->slots.size()) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    const auto& slot = vm_state->query_context->slots[rel_id];

    bool src_is_double = (vm_state->register_types[src] == TYPE_DOUBLE_VECTOR);
    size_t max_nodes = vm_state->query_context->max_nodes;

    bool has_edge_attr = false;
    BoundAttributeSlot edge_attr{};
    if (attr_id < vm_state->query_context->attribute_slots[rel_id].size()) {
        edge_attr = vm_state->query_context->attribute_slots[rel_id][attr_id];
        if (edge_attr.data_ptr) has_edge_attr = true;
    }

    const impulse_vm_context_t::MockAttribute* mock_node_attr = find_mock_node_attr(vm_state->query_context, attr_id);
    if (!mock_node_attr && attr_id == 0) {
        mock_node_attr = find_mock_node_attr(vm_state->query_context, 0);
    }

    if (vm_state->register_types[src] == TYPE_NODE_ID || vm_state->register_types[src] == TYPE_INT64) {
        uint64_t u = vm_state->registers[src];
        int64_t sum = 0;
        if (slot.offsets_ptr && slot.targets_ptr && u < slot.node_count) {
            uint64_t start = slot.get_csr_offset(u);
            uint64_t end   = slot.get_csr_offset(u + 1);
            for (uint64_t idx = start; idx < end; ++idx) {
                uint64_t target_node = slot.get_csr_target(idx);
                if (mock_node_attr) {
                    if (mock_node_attr->has_mask && !mock_node_attr->mask.test(target_node)) {
                        // masked
                    } else if (target_node < mock_node_attr->int_data.size()) {
                        sum += static_cast<int64_t>(mock_node_attr->int_data[target_node]);
                    } else if (target_node < mock_node_attr->float_data.size()) {
                        sum += static_cast<int64_t>(mock_node_attr->float_data[target_node]);
                    }
                } else if (has_edge_attr) {
                    uint8_t base_type = edge_attr.type_code & 0x7F;
                    if (base_type == 0x03) sum += static_cast<const int32_t*>(edge_attr.data_ptr)[idx];
                    else if (base_type == 0x04) sum += static_cast<const int64_t*>(edge_attr.data_ptr)[idx];
                    else if (base_type == 0x08) sum += static_cast<int64_t>(static_cast<const float*>(edge_attr.data_ptr)[idx]);
                    else if (base_type == 0x09) sum += static_cast<int64_t>(static_cast<const double*>(edge_attr.data_ptr)[idx]);
                    else sum += target_node;
                } else {
                    sum += target_node;
                }
            }
        }
        vm_state->registers[dst] = static_cast<uint64_t>(sum);
        vm_state->register_types[dst] = TYPE_INT64;
        if (sum == 0) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
        else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;
        vm_state->pc++;
        DISPATCH();
    }
    int h_dst = acquire_float_vector(vm_state->query_context);
    if (h_dst < 0) {
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
    float* dst_vec = vm_state->query_context->float_vectors[h_dst].data();
    std::memset(dst_vec, 0, max_nodes * sizeof(float));

    if (slot.offsets_ptr && slot.targets_ptr) {
        if (src_is_double) {
            const double* src_vec = vm_state->query_context->double_vectors[vm_state->registers[src]].data();
            for (uint64_t u = 0; u < slot.node_count; ++u) {
                double src_val = src_vec[u];
                if (src_val == 0.0) continue;
                uint64_t start = slot.get_csr_offset(u);
                uint64_t end   = slot.get_csr_offset(u + 1);
                for (uint64_t idx = start; idx < end; ++idx) {
                    uint64_t target_node = slot.get_csr_target(idx);
                    if (target_node < max_nodes) {
                        float weight = 1.0f;
                        if (has_edge_attr) {
                            uint8_t base_type = edge_attr.type_code & 0x7F;
                            if (base_type == 0x08) weight = static_cast<const float*>(edge_attr.data_ptr)[idx];
                            else if (base_type == 0x09) weight = static_cast<float>(static_cast<const double*>(edge_attr.data_ptr)[idx]);
                        }
                        dst_vec[target_node] += static_cast<float>(src_val * weight);
                    }
                }
            }
        } else if (vm_state->register_types[src] == TYPE_FLOAT_VECTOR) {
            const float* src_vec = vm_state->query_context->float_vectors[vm_state->registers[src]].data();
            for (uint64_t u = 0; u < slot.node_count; ++u) {
                float src_val = src_vec[u];
                if (src_val == 0.0f) continue;
                uint64_t start = slot.get_csr_offset(u);
                uint64_t end   = slot.get_csr_offset(u + 1);
                for (uint64_t idx = start; idx < end; ++idx) {
                    uint64_t target_node = slot.get_csr_target(idx);
                    if (target_node < max_nodes) {
                        float weight = 1.0f;
                        if (has_edge_attr) {
                            uint8_t base_type = edge_attr.type_code & 0x7F;
                            if (base_type == 0x08) weight = static_cast<const float*>(edge_attr.data_ptr)[idx];
                            else if (base_type == 0x09) weight = static_cast<float>(static_cast<const double*>(edge_attr.data_ptr)[idx]);
                        }
                        dst_vec[target_node] += src_val * weight;
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

