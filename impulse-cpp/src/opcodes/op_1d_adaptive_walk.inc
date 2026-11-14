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
#define OP_1D_PULL_EDGE_DIVISOR 20u
#define OP_1D_PULL_NODE_DIVISOR 20u
#endif

// Opcode 0x1D: OP_ADAPTIVE_WALK
op_ADAPTIVE_WALK: {
    const auto& inst = bytecode[vm_state->pc];
    uint32_t cur_pc = static_cast<uint32_t>(vm_state->pc);
    const uint16_t src = IMP_INST_LOW16(inst);
    VALIDATE_REG(src);
    IMPULSE_ASSERT(src < IMP_VM_GPR_COUNT);
    IMP_REQUIRE_CONTEXT(vm_state);
    IMPULSE_ASSERT(vm_state->query_context != nullptr);
    IMPULSE_CHECK_REG_TYPE(vm_state, src, OP_1D_ALLOWED_SRC_TYPES);
    uint16_t rel;
    IMPULSE_TESTCASE(IMP_INST_IS_EXTENDED(inst));
    if (IMP_INST_IS_EXTENDED(inst)) {
        if (vm_state->pc + 1 >= instruction_count) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        rel = IMP_INST_DST(bytecode[vm_state->pc + 1]);
    } else {
        rel = IMP_INST_HIGH16(inst);
    }

    bool pull_disabled = (impulse_test_get_opt_disable_mask() & IMPULSE_OPT_ADAPTIVE_WALK_PULL) != 0;
    IMPULSE_TESTCASE(pull_disabled);
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
            double avg_degree = static_cast<double>(total_edges) / static_cast<double>(std::max<uint64_t>(1, total_nodes));
            uint64_t est_edges = static_cast<uint64_t>(frontier_size * std::max<double>(1.0, avg_degree));

            // GraphIt Alpha=20, Beta=20
            bool use_pull = (est_edges > (total_edges / OP_1D_PULL_EDGE_DIVISOR) || frontier_size > (total_nodes / OP_1D_PULL_NODE_DIVISOR));
            IMPULSE_TESTCASE(use_pull);
            IMPULSE_TESTCASE(frontier_size == 0);
            if (use_pull) {
                impulse_vm_branch_coverage_record(cur_pc, OP_ADAPTIVE_WALK, 1u,
                                                  IMPULSE_COV_KIND_TWO_WAY, IMPULSE_COV_BOTH, 1u);
                const uint16_t dst = IMP_INST_DST(inst);
                VALIDATE_REG(dst);
                IMPULSE_ASSERT(dst < IMP_VM_GPR_COUNT);
                int h_dst = -1;
                const bool accum = IMP_INST_HAS_FLAG(inst, IMPULSE_VM_OP_FLAG_ACCUMULATE);
                IMPULSE_TESTCASE(dst == src);
                IMPULSE_TESTCASE(accum);
                if (vm_state->register_types[dst] == TYPE_BITSET_HANDLE) {
                    int h_existing = static_cast<int>(vm_state->registers[dst]);
                    if (dst == src) {
                        h_dst = acquire_bitset(vm_state->query_context);
                        if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                        IMPULSE_ASSERT(h_dst >= 0 && static_cast<size_t>(h_dst) < vm_state->query_context->bitsets.size());
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
                    IMPULSE_ASSERT(h_dst >= 0 && static_cast<size_t>(h_dst) < vm_state->query_context->bitsets.size());
                    auto& bs_dst = vm_state->query_context->bitsets[h_dst];
                    bs_dst.clear();
                    if (accum && (vm_state->register_types[dst] == TYPE_NODE_ID || vm_state->register_types[dst] == TYPE_INT64)) {
                        bitset_add(bs_dst, vm_state->registers[dst], vm_state->query_context->max_nodes);
                    }
                }
                auto& bs_dst = vm_state->query_context->bitsets[h_dst];

                size_t csc_n = (slot.csc_node_count > 0) ? slot.csc_node_count : slot.node_count;
                for (uint64_t u = 0; u < csc_n; ++u) {
                    uint64_t start = slot.get_csc_offset(u);
                    uint64_t end = slot.get_csc_offset(u + 1);
                    IMPULSE_WELL_FORMED_ASSERT(start <= end);
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
                IMP_VM_SET_ZF(vm_state, is_empty);
                vm_state->pc += IMP_INST_IS_EXTENDED(inst) ? 2u : 1u;
                DISPATCH();
            }
        }
    }
    impulse_vm_branch_coverage_record(cur_pc, OP_ADAPTIVE_WALK, 1u,
                                      IMPULSE_COV_KIND_TWO_WAY, IMPULSE_COV_BOTH, 0u);
    goto op_CSR_WALK;
}

