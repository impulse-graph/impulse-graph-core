// Opcode 0x1D: OP_ADAPTIVE_WALK
op_ADAPTIVE_WALK: {
    const auto& inst = bytecode[vm_state->pc];
    uint32_t cur_pc = static_cast<uint32_t>(vm_state->pc);
    uint16_t src = inst.payload & 0xFFFF;
    uint16_t rel;
    if (inst.flags & 0x80) {
        if (vm_state->pc + 1 >= instruction_count) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        rel = bytecode[vm_state->pc + 1].dst_reg;
    } else {
        rel = (inst.payload >> 16) & 0xFFFF;
    }

    bool pull_disabled = (impulse_test_get_opt_disable_mask() & IMPULSE_OPT_ADAPTIVE_WALK_PULL) != 0;
    if (!pull_disabled && rel < vm_state->query_context->slots.size()) {
        const auto& slot = vm_state->query_context->slots[rel];
        if (slot.csc_offsets_ptr && slot.csc_targets_ptr && vm_state->register_types[src] == TYPE_BITSET_HANDLE) {
            int h_src = static_cast<int>(vm_state->registers[src]);
            const auto& bs = vm_state->query_context->bitsets[h_src];
            uint64_t frontier_size = 0;
            for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                frontier_size += std::popcount(bs.words[i]);
            }
            uint64_t total_edges = slot.edge_count;
            uint64_t total_nodes = slot.node_count;
            double avg_degree = (double)total_edges / (double)std::max<uint64_t>(1, total_nodes);
            uint64_t est_edges = static_cast<uint64_t>(frontier_size * std::max<double>(1.0, avg_degree));

            // GraphIt Alpha=20, Beta=20
            bool use_pull = (est_edges > (total_edges / 20) || frontier_size > (total_nodes / 20));
            IMPULSE_TESTCASE(use_pull);
            IMPULSE_TESTCASE(frontier_size == 0);
            if (use_pull) {
                impulse_vm_branch_coverage_record(cur_pc, OP_ADAPTIVE_WALK, 1u,
                                                  IMPULSE_COV_KIND_TWO_WAY, IMPULSE_COV_BOTH, 1u);
                goto op_CSC_WALK;
            }
        }
    }
    impulse_vm_branch_coverage_record(cur_pc, OP_ADAPTIVE_WALK, 1u,
                                      IMPULSE_COV_KIND_TWO_WAY, IMPULSE_COV_BOTH, 0u);
    goto op_CSR_WALK;
}

