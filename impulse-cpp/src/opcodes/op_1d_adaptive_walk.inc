/**
 * @file op_1d_adaptive_walk.inc
 * @brief Formal Contract for OP_ADAPTIVE_WALK (0x1D).
 *
 * Mnemonic: OP_ADAPTIVE_WALK
 * Opcode: 0x1D
 * Description: Direction-optimizing adaptive graph walk (switches dynamically between push and pull).
 *
 * Operands:
 *  - src = payload & 0xFFFF, rel = (payload >> 16) & 0xFFFF
 *
 * Inputs:
 *  - src: TYPE_BITSET_HANDLE | TYPE_NODE_ID
 *
 * Outputs:
 *  - None (control branch / internal dispatch)
 *
 * Flags:
 *  - Unmodified
 *
 * Context Requirement:
 *  - Requires context (query_context snapshot slots)
 */
#ifndef OP_1D_CONTRACT_DEFINED
#define OP_1D_CONTRACT_DEFINED 1
#define OP_1D_ALLOWED_SRC_TYPES (IMP_TYPE_BIT(TYPE_NULL) | IMP_TYPE_BIT(TYPE_BITSET_HANDLE) | IMP_TYPES_INT_OR_NODE | IMP_TYPES_NODE_VEC)
#endif

// Opcode 0x1D: OP_ADAPTIVE_WALK
op_ADAPTIVE_WALK: {
    const auto& inst = bytecode[vm_state->pc];
    uint32_t cur_pc = static_cast<uint32_t>(vm_state->pc);
    uint16_t src = inst.payload & 0xFFFF;
    VALIDATE_REG(src);
    if (!vm_state->query_context) return IMPULSE_VM_ERR_NULL_SNAPSHOT;
    IMPULSE_CHECK_REG_TYPE(vm_state, src, OP_1D_ALLOWED_SRC_TYPES);
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
                uint16_t dst = inst.dst_reg;
                VALIDATE_REG(dst);
                int h_dst = -1;
                bool accum = (inst.flags & IMPULSE_VM_OP_FLAG_ACCUMULATE);
                if (vm_state->register_types[dst] == TYPE_BITSET_HANDLE) {
                    int h_existing = static_cast<int>(vm_state->registers[dst]);
                    if (dst == src) {
                        h_dst = acquire_bitset(vm_state->query_context);
                        if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                        if (accum) {
                            const auto& bs_existing = vm_state->query_context->bitsets[h_existing];
                            auto& bs_new = vm_state->query_context->bitsets[h_dst];
                            for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                                bs_new.words[i] = bs_existing.words[i];
                            }
                        } else {
                            vm_state->query_context->bitsets[h_dst].clear();
                        }
                    } else if (accum) {
                        h_dst = h_existing;
                    } else {
                        h_dst = h_existing;
                        vm_state->query_context->bitsets[h_dst].clear();
                    }
                } else {
                    h_dst = acquire_bitset(vm_state->query_context);
                    if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                    auto& bs_dst = vm_state->query_context->bitsets[h_dst];
                    bs_dst.clear();
                    if (accum && (vm_state->register_types[dst] == TYPE_NODE_ID || vm_state->register_types[dst] == TYPE_INT64)) {
                        bitset_add(bs_dst, vm_state->registers[dst], vm_state->query_context->max_nodes);
                    }
                }
                auto& bs_dst = vm_state->query_context->bitsets[h_dst];

                for (uint64_t u = 0; u < slot.node_count; ++u) {
                    uint64_t start = slot.get_csc_offset(u);
                    uint64_t end = slot.get_csc_offset(u + 1);
                    for (uint64_t e = start; e < end; ++e) {
                        uint64_t parent = slot.get_csc_target(e);
                        if (bitset_test(bs, parent, vm_state->query_context->max_nodes)) {
                            bitset_add(bs_dst, u, vm_state->query_context->max_nodes);
                            break;
                        }
                    }
                }

                if (dst == src) {
                    release_bitset(vm_state->query_context, h_src);
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
        }
    }
    impulse_vm_branch_coverage_record(cur_pc, OP_ADAPTIVE_WALK, 1u,
                                      IMPULSE_COV_KIND_TWO_WAY, IMPULSE_COV_BOTH, 0u);
    goto op_CSR_WALK;
}

