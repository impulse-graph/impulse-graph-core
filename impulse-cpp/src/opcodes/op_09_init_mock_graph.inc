/**
 * @file op_09_init_mock_graph.inc
 * @brief Formal Contract for OP_INIT_MOCK_GRAPH (0x09).
 *
 * Mnemonic: OP_INIT_MOCK_GRAPH
 * Opcode: 0x09
 * Description: Initializes mock CSR/CSC graph topology from inline data buffer into internal slot.
 *
 * Operands:
 *  - slot_id = inst.dst_reg, offset = payload & 0xFFFF, node_count = payload >> 16
 *
 * Inputs:
 *  - query_context->inline_data_ptr
 *
 * Outputs:
 *  - Internal query_context->slots[slot_id]
 *
 * Flags:
 *  - Unmodified
 *
 * Context Requirement:
 *  - Requires context (query_context inline data and mock slots)
 */
#ifndef OP_09_CONTRACT_DEFINED
#define OP_09_CONTRACT_DEFINED 1
#endif

// Opcode 0x09: OP_INIT_MOCK_GRAPH
op_INIT_MOCK_GRAPH: {
    const impulse_instruction_t& inst = bytecode[vm_state->pc];
    uint16_t slot_id = inst.dst_reg;
    uint16_t offset_bytes = inst.payload & 0xFFFF;
    uint16_t node_count = (inst.payload >> 16) & 0xFFFF;

    if (!vm_state->query_context || !vm_state->query_context->inline_data_ptr) {
        return IMPULSE_VM_ERR_NULL_SNAPSHOT;
    }
    if (slot_id >= 16) {
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
    uint64_t req_offsets_bytes = static_cast<uint64_t>(offset_bytes) + (static_cast<uint64_t>(node_count) + 1) * sizeof(uint32_t);
    if (req_offsets_bytes > vm_state->query_context->inline_data_bytes) {
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }

    {
        const uint32_t* offsets = reinterpret_cast<const uint32_t*>(vm_state->query_context->inline_data_ptr + offset_bytes);
        uint32_t total_edges = offsets[node_count];
        uint64_t req_total_bytes = req_offsets_bytes + static_cast<uint64_t>(total_edges) * sizeof(uint32_t);
        if (req_total_bytes > vm_state->query_context->inline_data_bytes) {
            return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        }
        const uint32_t* targets = offsets + (node_count + 1);

        uint32_t max_target_id = 0;
        for (uint32_t e = 0; e < total_edges; ++e) {
            if (targets[e] > max_target_id) max_target_id = targets[e];
        }
        uint32_t csc_node_count = (total_edges > 0) ? std::max(static_cast<uint32_t>(node_count), max_target_id + 1) : node_count;

        auto& slot = vm_state->query_context->slots[slot_id];
        auto& in_degrees = vm_state->query_context->scratch_u32_a;
        in_degrees.clear();
        in_degrees.resize(csc_node_count, 0);
        for (uint32_t e = 0; e < total_edges; ++e) {
            in_degrees[targets[e]]++;
        }

        slot.dynamic_csc_offsets.clear();
        slot.dynamic_csc_offsets.resize(csc_node_count + 1, 0);
        for (uint32_t i = 0; i < csc_node_count; ++i) {
            slot.dynamic_csc_offsets[i + 1] = slot.dynamic_csc_offsets[i] + in_degrees[i];
        }

        slot.dynamic_csc_targets.clear();
        slot.dynamic_csc_targets.resize(total_edges, 0);
        auto& cur_pos = vm_state->query_context->scratch_u32_b;
        cur_pos.clear();
        cur_pos.resize(csc_node_count + 1, 0);
        std::memcpy(cur_pos.data(), slot.dynamic_csc_offsets.data(), (csc_node_count + 1) * sizeof(uint32_t));
        for (uint32_t u = 0; u < node_count; ++u) {
            uint32_t start = offsets[u];
            uint32_t end = offsets[u + 1];
            for (uint32_t e = start; e < end; ++e) {
                uint32_t v = targets[e];
                slot.dynamic_csc_targets[cur_pos[v]++] = u;
            }
        }

        slot.csc_offsets_ptr = slot.dynamic_csc_offsets.data();
        slot.csc_targets_ptr = slot.dynamic_csc_targets.data();
        slot.node_count = static_cast<uint64_t>(node_count);
        slot.csc_node_count = static_cast<uint64_t>(csc_node_count);
        slot.edge_count = total_edges;
        slot.offsets_ptr = offsets;
        slot.targets_ptr = targets;

        uint64_t max_slot_nodes = std::max(slot.node_count, slot.csc_node_count);
        if (max_slot_nodes > vm_state->query_context->max_nodes) {
            vm_state->query_context->max_nodes = max_slot_nodes;
        }
    }

    vm_state->pc++;
    DISPATCH();
}

