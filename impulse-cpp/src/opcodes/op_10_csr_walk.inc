/**
 * @file op_10_csr_walk.inc
 * @brief Formal Contract for OP_CSR_WALK (0x10).
 *
 * Mnemonic: OP_CSR_WALK
 * Opcode: 0x10
 * Description: Standard SIMD/parallel Compressed Sparse Row (CSR) forward traversal step.
 *
 * Operands:
 *  - dst = inst.dst_reg, src = payload & 0xFFFF, rel = (payload >> 16) & 0xFFFF
 *
 * Inputs:
 *  - src: TYPE_NULL | TYPE_NODE_ID | TYPE_INT64 | TYPE_BITSET_HANDLE | TYPE_NODE_VECTOR
 *
 * Outputs:
 *  - dst: TYPE_BITSET_HANDLE
 *
 * Flags:
 *  - ZF: Set if output frontier is empty
 *
 * Context Requirement:
 *  - Requires context (query_context snapshot slots and bitset pool)
 */
#ifndef OP_10_CONTRACT_DEFINED
#define OP_10_CONTRACT_DEFINED 1
#define OP_10_PARALLEL_FRONTIER_THRESHOLD 15000u
#define OP_10_ALLOWED_SRC_TYPES (IMP_TYPE_BIT(TYPE_NULL) | IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_BITSET_HANDLE) | IMP_TYPES_NODE_VEC)
#endif

// Opcode 0x10: OP_CSR_WALK
op_CSR_WALK: {
    const auto& inst = bytecode[vm_state->pc];
    const uint16_t dst = IMP_INST_DST(inst);
    const uint16_t src = IMP_INST_LOW16(inst);
    uint16_t rel;
    IMPULSE_TESTCASE(IMP_INST_IS_EXTENDED(inst));
    if (IMP_INST_IS_EXTENDED(inst)) {
        if (vm_state->pc + 1 >= instruction_count) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        rel = IMP_INST_DST(bytecode[vm_state->pc + 1]);
        vm_state->pc++;
    } else {
        rel = IMP_INST_HIGH16(inst);
    }
    VALIDATE_REG(dst);
    VALIDATE_REG(src);
    IMPULSE_ASSERT(dst < IMP_VM_GPR_COUNT && src < IMP_VM_GPR_COUNT);
    IMP_REQUIRE_CONTEXT(vm_state);
    IMPULSE_ASSERT(vm_state->query_context != nullptr);
    IMPULSE_CHECK_REG_TYPE(vm_state, src, OP_10_ALLOWED_SRC_TYPES);

    IMP_REQUIRE_REL_SLOT(vm_state, rel);
    const auto& slot = vm_state->query_context->slots[rel];

    const bool has_input_seed = IMP_INST_HAS_FLAG(inst, IMPULSE_VM_OP_FLAG_INPUT_SEED);
    bool src_is_null = (vm_state->register_types[src] == TYPE_NULL && !has_input_seed);
    bool src_is_bitset = (vm_state->register_types[src] == TYPE_BITSET_HANDLE);
    bool src_is_vec = (!has_input_seed &&
                       (vm_state->register_types[src] == TYPE_NODE_VECTOR || vm_state->register_types[src] == TYPE_UINT64_VECTOR));
    int h_src = src_is_bitset ? static_cast<int>(vm_state->registers[src]) : -1;
    uint64_t scalar_src = (!src_is_bitset && !src_is_vec) ? vm_state->registers[src] : 0;

    if (has_input_seed) {
        scalar_src = input_param;
        src_is_bitset = false;
        src_is_vec = false;
        h_src = -1;
    }

    int h_dst = -1;
    const bool accum = IMP_INST_HAS_FLAG(inst, IMPULSE_VM_OP_FLAG_ACCUMULATE);
    IMPULSE_TESTCASE(src_is_null);
    IMPULSE_TESTCASE(src_is_bitset);
    IMPULSE_TESTCASE(src_is_vec);
    IMPULSE_TESTCASE(accum);
    IMPULSE_TESTCASE(dst == src && src_is_bitset);

    if (vm_state->register_types[dst] == TYPE_BITSET_HANDLE) {
        int h_existing = static_cast<int>(vm_state->registers[dst]);
        if (dst == src && src_is_bitset) {
            h_dst = acquire_bitset(vm_state->query_context);
            if (h_dst < 0) {
                return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
            }
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
        if (h_dst < 0) {
            return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        }
        IMPULSE_ASSERT(h_dst >= 0 && static_cast<size_t>(h_dst) < vm_state->query_context->bitsets.size());
        auto& bs_dst = vm_state->query_context->bitsets[h_dst];
        bs_dst.clear();
        if (accum && (vm_state->register_types[dst] == TYPE_NODE_ID || vm_state->register_types[dst] == TYPE_INT64)) {
            bitset_add(bs_dst, vm_state->registers[dst], vm_state->query_context->max_nodes);
        }
    }

    auto& bs_dst = vm_state->query_context->bitsets[h_dst];

    if (!src_is_null && !slot.is_virtual && !slot.is_raw_coo && slot.offsets_ptr && slot.targets_ptr) {
        if (src_is_bitset) {
            const auto& bs_src = vm_state->query_context->bitsets[h_src];
            size_t frontier_size = 0;
            for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                frontier_size += std::popcount(bs_src.words[i]);
            }
            IMPULSE_TESTCASE(frontier_size == 0u);

            bool use_parallel = (vm_state->query_context->max_threads > 1 && frontier_size > OP_10_PARALLEL_FRONTIER_THRESHOLD);
            IMPULSE_TESTCASE(use_parallel);
            if (use_parallel) {
#if defined(_OPENMP)
                int num_threads = vm_state->query_context->max_threads;
                auto* ctx = vm_state->query_context;
                size_t max_nodes = ctx->max_nodes;
                size_t words = ctx->words_per_bitset;

                #pragma omp parallel for schedule(static) num_threads(num_threads)
                for (int t = 0; t < num_threads; ++t) {
                    ctx->private_bitsets[t].clear();
                }

                #pragma omp parallel for schedule(dynamic, 1024) num_threads(num_threads)
                for (size_t w = 0; w < words; ++w) {
                    uint64_t word = bs_src.words[w];
                    if (word == 0) continue;
                    int tid = omp_get_thread_num();
                    auto& priv_bs = ctx->private_bitsets[tid];
                    while (word) {
                        int bit = std::countr_zero(word);
                        uint64_t u = IMP_WORD_BIT_TO_NODE(w, bit);
                        word = IMP_CLEAR_LOWEST_BIT(word);
                        if (u < slot.node_count) {
                            uint64_t start = slot.get_csr_offset(u);
                            uint64_t end = slot.get_csr_offset(u + 1);
                            IMPULSE_WELL_FORMED_ASSERT(start <= end);
                            for (uint64_t i = start; i < end; ++i) {
                                bitset_add(priv_bs, slot.get_csr_target(i), max_nodes);
                            }
                        }
                    }
                }

                #pragma omp parallel for schedule(static) num_threads(num_threads)
                for (size_t w = 0; w < words; ++w) {
                    uint64_t merged = 0;
                    for (int t = 0; t < num_threads; ++t) {
                        merged |= ctx->private_bitsets[t].words[w];
                    }
                    if (accum) {
                        bs_dst.words[w] |= merged;
                    } else {
                        bs_dst.words[w] = merged;
                    }
                }
#endif
            } else {
                for (size_t w = 0; w < vm_state->query_context->words_per_bitset; ++w) {
                    uint64_t word = bs_src.words[w];
                    while (word) {
                        int bit = std::countr_zero(word);
                        uint64_t u = IMP_WORD_BIT_TO_NODE(w, bit);
                        word = IMP_CLEAR_LOWEST_BIT(word);
                        if (u < slot.node_count) {
                            uint64_t start = slot.get_csr_offset(u);
                            uint64_t end = slot.get_csr_offset(u + 1);
                            IMPULSE_WELL_FORMED_ASSERT(start <= end);
                            for (uint64_t i = start; i < end; ++i) {
                                bitset_add(bs_dst, slot.get_csr_target(i), vm_state->query_context->max_nodes);
                            }
                        }
                    }
                }
            }
        } else if (src_is_vec) {
            const uint64_t* vec_ptr = nullptr;
            size_t vec_len = 0;
            if (get_node_vector_slice(vm_state->query_context, vm_state->registers[src], &vec_ptr, &vec_len)) {
                for (size_t k = 0; k < vec_len; ++k) {
                    uint64_t u = vec_ptr[k];
                    if (u < slot.node_count) {
                        uint64_t start = slot.get_csr_offset(u);
                        uint64_t end = slot.get_csr_offset(u + 1);
                        IMPULSE_WELL_FORMED_ASSERT(start <= end);
                        for (uint64_t i = start; i < end; ++i) {
                            bitset_add(bs_dst, slot.get_csr_target(i), vm_state->query_context->max_nodes);
                        }
                    }
                }
            }
        } else {
            uint64_t u = scalar_src;
            if (u < slot.node_count) {
                uint64_t start = slot.get_csr_offset(u);
                uint64_t end = slot.get_csr_offset(u + 1);
                IMPULSE_WELL_FORMED_ASSERT(start <= end);
                IMPULSE_TESTCASE(start == end);
                for (uint64_t i = start; i < end; ++i) {
                    bitset_add(bs_dst, slot.get_csr_target(i), vm_state->query_context->max_nodes);
                }
            }
        }
    } else if (!src_is_null && (slot.is_virtual || slot.is_raw_coo || (!slot.offsets_ptr && slot.targets_ptr))) {
        if (src_is_bitset) {
            const auto& bs_src = vm_state->query_context->bitsets[h_src];
            if (!slot.is_virtual && !slot.is_raw_coo && !slot.offsets_ptr && slot.targets_ptr) {
                uint64_t sent = slot.csr_sentinel();
                for (size_t w = 0; w < vm_state->query_context->words_per_bitset; ++w) {
                    uint64_t word = bs_src.words[w];
                    while (word) {
                        int bit = std::countr_zero(word);
                        uint64_t u = IMP_WORD_BIT_TO_NODE(w, bit);
                        word = IMP_CLEAR_LOWEST_BIT(word);
                        if (u < slot.node_count) {
                            uint64_t target_node = slot.get_csr_target(u);
                            if (target_node != sent) {
                                bitset_add(bs_dst, target_node, vm_state->query_context->max_nodes);
                            }
                        }
                    }
                }
            } else {
                walk_slot_forward_special(
                    vm_state->query_context,
                    rel,
                    [&](uint64_t u) { return bitset_test(bs_src, u, vm_state->query_context->max_nodes); },
                    0,
                    bs_dst
                );
            }
        } else if (src_is_vec) {
            const uint64_t* vec_ptr = nullptr;
            size_t vec_len = 0;
            if (get_node_vector_slice(vm_state->query_context, vm_state->registers[src], &vec_ptr, &vec_len) && vec_ptr) {
                for (size_t k = 0; k < vec_len; ++k) {
                    uint64_t u = vec_ptr[k];
                    if (u != IMP_INVALID_NODE_U64) {
                        for_each_forward_target_of_node(
                            vm_state->query_context,
                            rel,
                            u,
                            [&](uint64_t v) { bitset_add(bs_dst, v, vm_state->query_context->max_nodes); }
                        );
                    }
                }
            }
        } else {
            for_each_forward_target_of_node(
                vm_state->query_context,
                rel,
                scalar_src,
                [&](uint64_t v) { bitset_add(bs_dst, v, vm_state->query_context->max_nodes); }
            );
        }
    }

    if (dst == src && src_is_bitset) {
        release_bitset(vm_state->query_context, h_src);
    }
    vm_state->registers[dst] = static_cast<uint64_t>(h_dst);
    vm_state->register_types[dst] = TYPE_BITSET_HANDLE;

    bool is_empty = true;
    for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
        if (bs_dst.words[i] != 0) {
            is_empty = false;
            break;
        }
    }
    if (IMP_INST_HAS_FLAG(inst, IMPULSE_VM_OP_FLAG_HALT_ON_EMPTY)) {
        IMPULSE_TESTCASE(is_empty);
        impulse_vm_branch_coverage_record(static_cast<uint32_t>(vm_state->pc), OP_CSR_WALK, 2u,
                                          IMPULSE_COV_KIND_TWO_WAY, IMPULSE_COV_BOTH,
                                          is_empty ? 0u : 1u);
    }
    IMP_VM_SET_ZF(vm_state, is_empty);
    if (is_empty && IMP_INST_HAS_FLAG(inst, IMPULSE_VM_OP_FLAG_HALT_ON_EMPTY)) {
        if (instruction_count > 0 && bytecode[instruction_count - 1].opcode == OP_HALT) {
            uint16_t halt_reg = IMP_INST_DST(bytecode[instruction_count - 1]);
            if (halt_reg < IMP_VM_GPR_COUNT && halt_reg != dst) {
                vm_state->registers[halt_reg] = static_cast<uint64_t>(h_dst);
                vm_state->register_types[halt_reg] = TYPE_BITSET_HANDLE;
            }
        }
        vm_state->pc = instruction_count;
        return IMPULSE_VM_OK;
    }

    vm_state->pc++;
    DISPATCH();
}

