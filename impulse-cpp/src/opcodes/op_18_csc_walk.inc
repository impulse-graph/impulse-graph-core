/**
 * @file op_18_csc_walk.inc
 * @brief Formal Contract for OP_CSC_WALK (0x18).
 *
 * Mnemonic: OP_CSC_WALK
 * Opcode: 0x18
 * Description: SIMD/parallel Compressed Sparse Column (CSC) backward/incoming traversal step.
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
 *  - ZF: Set if incoming frontier is empty
 *
 * Context Requirement:
 *  - Requires context (query_context CSC slots and bitset pool)
 */
#ifndef OP_18_CONTRACT_DEFINED
#define OP_18_CONTRACT_DEFINED 1
#endif

// Opcode 0x18: OP_CSC_WALK
op_CSC_WALK: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src = inst.payload & 0xFFFF;
    uint16_t unv = (inst.payload >> 16) & 0xFF;
    uint16_t rel = (inst.payload >> 24) & 0xFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src);
    if (!vm_state->query_context) return IMPULSE_VM_ERR_NULL_SNAPSHOT;
    IMPULSE_CHECK_REG_TYPE(vm_state, src, (IMP_TYPE_BIT(TYPE_NULL) | IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_BITSET_HANDLE) | IMP_TYPES_NODE_VEC));

    if (inst.flags & 0x80) {
        if (vm_state->pc + 1 >= instruction_count) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        rel = bytecode[vm_state->pc + 1].dst_reg;
        unv = 0;
        vm_state->pc++;
    } else {
        bool unv_is_valid_rel = (unv < vm_state->query_context->slots.size()) &&
            vm_state->query_context->slots[unv].has_any_topology();
        if (rel == 0 && (inst.payload >> 24) == 0 && (unv_is_valid_rel || vm_state->register_types[unv] != TYPE_BITSET_HANDLE)) {
            rel = (inst.payload >> 16) & 0xFFFF;
            unv = 0;
        }
    }

    if (rel >= vm_state->query_context->slots.size()) {
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
    const auto& slot = vm_state->query_context->slots[rel];
    if (!vm_state->query_context->snapshot &&
        !slot.csc_offsets_ptr && slot.dynamic_csc_offsets.empty() && !slot.csc_targets_ptr) {
        return IMPULSE_VM_ERR_NULL_SNAPSHOT;
    }

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

    const uint64_t* unv_words = (unv > 0 && vm_state->register_types[unv] == TYPE_BITSET_HANDLE)
        ? vm_state->query_context->bitsets[vm_state->registers[unv]].words : nullptr;

    int h_dst = -1;
    bool accum = (inst.flags & IMPULSE_VM_OP_FLAG_ACCUMULATE) != 0;
    if (vm_state->register_types[dst] == TYPE_BITSET_HANDLE) {
        int h_existing = static_cast<int>(vm_state->registers[dst]);
        if ((dst == src && src_is_bitset) || (unv > 0 && dst == unv && vm_state->register_types[unv] == TYPE_BITSET_HANDLE)) {
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
        vm_state->query_context->bitsets[h_dst].clear();
        if (accum && (vm_state->register_types[dst] == TYPE_NODE_ID || vm_state->register_types[dst] == TYPE_INT64)) {
            bitset_add(vm_state->query_context->bitsets[h_dst], vm_state->registers[dst], vm_state->query_context->max_nodes);
        }
    }

    auto& bs_dst = vm_state->query_context->bitsets[h_dst];
    const uint64_t* src_words = src_is_bitset ? vm_state->query_context->bitsets[h_src].words : nullptr;
#if defined(_OPENMP)
    int num_threads = vm_state->query_context->max_threads;
    (void)num_threads;
#endif
    uint64_t csc_bound = (slot.csc_node_count > 0)
        ? slot.csc_node_count
        : ((slot.tgt_node_count > 0) ? slot.tgt_node_count : slot.node_count);

    if (!src_is_null && !slot.is_virtual &&
        ((slot.csc_offsets_ptr && slot.csc_targets_ptr) || !slot.dynamic_csc_offsets.empty())) {
        if (unv_words != nullptr) {
            // GraphBLAS Bottom-Up BFS: unv is the candidate target set v, src is the source frontier u
            for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                uint64_t w_unv = unv_words[i];
                if (w_unv == 0) {
                    bs_dst.words[i] = 0;
                    continue;
                }
                uint64_t w_dst = 0;
                for (int b = 0; b < 64; ++b) {
                    if (w_unv & (1ULL << b)) {
                        uint64_t v = i * 64 + b;
                        if (v < csc_bound) {
                            uint64_t start = slot.get_csc_offset(v);
                            uint64_t end   = slot.get_csc_offset(v + 1);
                            for (uint64_t idx = start; idx < end; ++idx) {
                                uint64_t u = slot.get_csc_target(idx);
                                bool hit = src_words ? ((src_words[u >> 6] & (1ULL << (u & 63))) != 0) : (u == scalar_src);
                                if (hit) {
                                    w_dst |= (1ULL << b);
                                    break;
                                }
                            }
                        }
                    }
                }
                bs_dst.words[i] = w_dst;
            }
        } else {
            // Standard Reverse Metapath Walk (<-[:Rel]-): src is target set v, dst is incoming sources u
            if (src_is_bitset) {
                const auto& bs_src = vm_state->query_context->bitsets[h_src];
                for (size_t w = 0; w < vm_state->query_context->words_per_bitset; ++w) {
                    uint64_t word = bs_src.words[w];
                    while (word) {
                        int bit = std::countr_zero(word);
                        uint64_t v = w * 64 + bit;
                        word &= word - 1;
                        if (v < csc_bound) {
                            uint64_t start = slot.get_csc_offset(v);
                            uint64_t end   = slot.get_csc_offset(v + 1);
                            for (uint64_t idx = start; idx < end; ++idx) {
                                uint64_t u = slot.get_csc_target(idx);
                                bitset_add(bs_dst, u, vm_state->query_context->max_nodes);
                            }
                        }
                    }
                }
            } else if (src_is_vec) {
                const uint64_t* vec_ptr = nullptr;
                size_t vec_len = 0;
                if (get_node_vector_slice(vm_state->query_context, vm_state->registers[src], &vec_ptr, &vec_len)) {
                    for (size_t k = 0; k < vec_len; ++k) {
                        uint64_t v = vec_ptr[k];
                        if (v < csc_bound) {
                            uint64_t start = slot.get_csc_offset(v);
                            uint64_t end   = slot.get_csc_offset(v + 1);
                            for (uint64_t idx = start; idx < end; ++idx) {
                                uint64_t u = slot.get_csc_target(idx);
                                bitset_add(bs_dst, u, vm_state->query_context->max_nodes);
                            }
                        }
                    }
                }
            } else {
                uint64_t v = scalar_src;
                if (v < csc_bound) {
                    uint64_t start = slot.get_csc_offset(v);
                    uint64_t end   = slot.get_csc_offset(v + 1);
                    for (uint64_t idx = start; idx < end; ++idx) {
                        uint64_t u = slot.get_csc_target(idx);
                        bitset_add(bs_dst, u, vm_state->query_context->max_nodes);
                    }
                }
            }
        }
    } else if (!slot.csc_offsets_ptr && slot.csc_targets_ptr) {
        if (unv_words != nullptr) {
            for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
                uint64_t w_dst = 0;
                for (int b = 0; b < 64; ++b) {
                    uint64_t v = i * 64 + b;
                    if (v < csc_bound) {
                        uint64_t u = slot.get_csc_target(v);
                        if (u != 0xFFFFFFFF && u != 0xFFFF && u != ~0ULL) {
                            bool hit = src_words ? ((src_words[u >> 6] & (1ULL << (u & 63))) != 0) : (u == scalar_src);
                            if (hit) {
                                w_dst |= (1ULL << b);
                            }
                        }
                    }
                }
                bs_dst.words[i] = w_dst & unv_words[i];
            }
        } else {
            if (src_is_bitset) {
                const auto& bs_src = vm_state->query_context->bitsets[h_src];
                for (size_t w = 0; w < vm_state->query_context->words_per_bitset; ++w) {
                    uint64_t word = bs_src.words[w];
                    while (word) {
                        int bit = std::countr_zero(word);
                        uint64_t v = w * 64 + bit;
                        word &= word - 1;
                        if (v < csc_bound) {
                            uint64_t u = slot.get_csc_target(v);
                            if (u != slot.csc_sentinel()) {
                                bitset_add(bs_dst, u, vm_state->query_context->max_nodes);
                            }
                        }
                    }
                }
            } else if (src_is_vec) {
                const uint64_t* vec_ptr = nullptr;
                size_t vec_len = 0;
                uint64_t sent = slot.csc_sentinel();
                if (get_node_vector_slice(vm_state->query_context, vm_state->registers[src], &vec_ptr, &vec_len) && vec_ptr) {
                    for (size_t k = 0; k < vec_len; ++k) {
                        uint64_t v = vec_ptr[k];
                        if (v != ~0ULL && v < csc_bound) {
                            uint64_t u = slot.get_csc_target(v);
                            if (u != sent) {
                                bitset_add(bs_dst, u, vm_state->query_context->max_nodes);
                            }
                        }
                    }
                }
            } else {
                uint64_t v = scalar_src;
                if (v < csc_bound) {
                    uint64_t u = slot.get_csc_target(v);
                    if (u != slot.csc_sentinel()) {
                        bitset_add(bs_dst, u, vm_state->query_context->max_nodes);
                    }
                }
            }
        }
    } else if (!src_is_null) {
        const uint64_t* vec_ptr = nullptr;
        size_t vec_len = 0;
        if (src_is_vec) {
            get_node_vector_slice(vm_state->query_context, vm_state->registers[src], &vec_ptr, &vec_len);
        }
        walk_slot_reverse_special(
            vm_state->query_context,
            rel,
            [&](uint64_t v) -> bool {
                if (src_is_bitset) {
                    return bitset_test(vm_state->query_context->bitsets[h_src], v, vm_state->query_context->max_nodes);
                }
                if (src_is_vec) {
                    if (!vec_ptr) return false;
                    for (size_t k = 0; k < vec_len; ++k) {
                        if (vec_ptr[k] == v) return true;
                    }
                    return false;
                }
                return (v == scalar_src);
            },
            0,
            bs_dst,
            unv_words
        );
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

