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
#endif

// Opcode 0x10: OP_CSR_WALK
op_CSR_WALK: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src = inst.payload & 0xFFFF;
    uint16_t rel;
    if (inst.flags & 0x80) {
        if (vm_state->pc + 1 >= instruction_count) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        rel = bytecode[vm_state->pc + 1].dst_reg;
        vm_state->pc++;
    } else {
        rel = (inst.payload >> 16) & 0xFFFF;
    }
    VALIDATE_REG(dst);
    VALIDATE_REG(src);
    if (!vm_state->query_context) return IMPULSE_VM_ERR_NULL_SNAPSHOT;
    IMPULSE_CHECK_REG_TYPE(vm_state, src, (IMP_TYPE_BIT(TYPE_NULL) | IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_BITSET_HANDLE) | IMP_TYPES_NODE_VEC));

    if (rel >= vm_state->query_context->slots.size()) {
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
    const auto& slot = vm_state->query_context->slots[rel];

    bool src_is_null = (vm_state->register_types[src] == TYPE_NULL && !(inst.flags & IMPULSE_VM_OP_FLAG_INPUT_SEED));
    bool src_is_bitset = (vm_state->register_types[src] == TYPE_BITSET_HANDLE);
    bool src_is_vec = (!(inst.flags & IMPULSE_VM_OP_FLAG_INPUT_SEED) &&
                       (vm_state->register_types[src] == TYPE_NODE_VECTOR || vm_state->register_types[src] == TYPE_UINT64_VECTOR));
    int h_src = src_is_bitset ? static_cast<int>(vm_state->registers[src]) : -1;
    uint64_t scalar_src = (!src_is_bitset && !src_is_vec) ? vm_state->registers[src] : 0;

    if (inst.flags & IMPULSE_VM_OP_FLAG_INPUT_SEED) {
        scalar_src = input_param;
        src_is_bitset = false;
        src_is_vec = false;
        h_src = -1;
    }

    int h_dst = -1;
    bool accum = (inst.flags & IMPULSE_VM_OP_FLAG_ACCUMULATE);

    if (vm_state->register_types[dst] == TYPE_BITSET_HANDLE) {
        int h_existing = static_cast<int>(vm_state->registers[dst]);
        if (dst == src && src_is_bitset) {
            h_dst = acquire_bitset(vm_state->query_context);
            if (h_dst < 0) {
                return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
            }
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
        auto& bs_dst = vm_state->query_context->bitsets[h_dst];
        bs_dst.clear();
        if (accum && (vm_state->register_types[dst] == TYPE_NODE_ID || vm_state->register_types[dst] == TYPE_INT64)) {
            bitset_add(bs_dst, vm_state->registers[dst], vm_state->query_context->max_nodes);
        }
    }

    auto& bs_dst = vm_state->query_context->bitsets[h_dst];

    if (!src_is_null && slot.offsets_ptr && slot.targets_ptr) {
        if (src_is_bitset) {
            const auto& bs_src = vm_state->query_context->bitsets[h_src];
            size_t frontier_size = 0;
            for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                frontier_size += std::popcount(bs_src.words[i]);
            }

            bool use_parallel = (vm_state->query_context->max_threads > 1 && frontier_size > 15000);
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
                        uint64_t u = w * 64 + bit;
                        word &= word - 1;
                        if (u < slot.node_count) {
                            uint64_t start = slot.get_csr_offset(u);
                            uint64_t end = slot.get_csr_offset(u + 1);
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
                        uint64_t u = w * 64 + bit;
                        word &= word - 1;
                        if (u < slot.node_count) {
                            uint64_t start = slot.get_csr_offset(u);
                            uint64_t end = slot.get_csr_offset(u + 1);
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
                for (uint64_t i = start; i < end; ++i) {
                    bitset_add(bs_dst, slot.get_csr_target(i), vm_state->query_context->max_nodes);
                }
            }
        }
    } else if (!src_is_null && !slot.offsets_ptr && slot.targets_ptr) {
        if (src_is_bitset) {
            const auto& bs_src = vm_state->query_context->bitsets[h_src];
            for (size_t w = 0; w < vm_state->query_context->words_per_bitset; ++w) {
                uint64_t word = bs_src.words[w];
                while (word) {
                    int bit = std::countr_zero(word);
                    uint64_t u = w * 64 + bit;
                    word &= word - 1;
                    if (u < slot.node_count) {
                        uint64_t target_node = slot.get_csr_target(u);
                        if (target_node != 0xFFFFFFFF && target_node != 0xFFFF && target_node != ~0ULL) {
                            bitset_add(bs_dst, target_node, vm_state->query_context->max_nodes);
                        }
                    }
                }
            }
        } else {
            uint64_t u = scalar_src;
            if (u < slot.node_count) {
                uint64_t v = slot.get_csr_target(u);
                if (v != 0xFFFFFFFF && v != 0xFFFF && v != ~0ULL) {
                    bitset_add(bs_dst, v, vm_state->query_context->max_nodes);
                }
            }
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
    if (inst.flags & IMPULSE_VM_OP_FLAG_HALT_ON_EMPTY) {
        IMPULSE_TESTCASE(is_empty);
        impulse_vm_branch_coverage_record(static_cast<uint32_t>(vm_state->pc), OP_CSR_WALK_2HOP, 2u,
                                          IMPULSE_COV_KIND_TWO_WAY, IMPULSE_COV_BOTH,
                                          is_empty ? 0u : 1u);
    }
    if (is_empty) {
        vm_state->flags |= IMPULSE_VM_FLAG_ZF;
        if (inst.flags & IMPULSE_VM_OP_FLAG_HALT_ON_EMPTY) {
            if (instruction_count > 0 && bytecode[instruction_count - 1].opcode == OP_HALT) {
                uint16_t halt_reg = bytecode[instruction_count - 1].dst_reg;
                if (halt_reg < 64 && halt_reg != dst) {
                    vm_state->registers[halt_reg] = static_cast<uint64_t>(h_dst);
                    vm_state->register_types[halt_reg] = TYPE_BITSET_HANDLE;
                }
            }
            vm_state->pc = instruction_count;
            return IMPULSE_VM_OK;
        }
    } else {
        vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;
    }

    vm_state->pc++;
    DISPATCH();
}

