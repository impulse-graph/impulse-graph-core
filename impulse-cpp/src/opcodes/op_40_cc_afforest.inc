/**
 * Formal Opcode Contract: OP_CC_AFFOREST (0x40)
 *
 * Layout:
 *   dst_reg:          inst.dst_reg (DST_REG)
 *   rel_id:           inst.payload & 0xFFFF
 *   allowed_src:      none
 *   output_dst:       TYPE_UINT64_VECTOR
 *   requires_ctx:     true
 */
#ifndef OP_40_CONTRACT_DEFINED
#define OP_40_CONTRACT_DEFINED
#define OP_40_DEFINED_FLAGS        0x00u
#define OP_40_OUTPUT_DST_TYPES     (IMP_TYPE_BIT(TYPE_NODE_VECTOR) | IMP_TYPE_BIT(TYPE_UINT64_VECTOR))
#endif

// Opcode 0x40: OP_CC_AFFOREST
op_CC_AFFOREST: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t rel = inst.payload & 0xFFFF;
    VALIDATE_REG(dst);
    if (!vm_state->query_context) return IMPULSE_VM_ERR_NULL_SNAPSHOT;

    if (rel >= vm_state->query_context->slots.size()) {
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
    const auto& slot = vm_state->query_context->slots[rel];
    if (!slot.offsets_ptr || !slot.targets_ptr) {
        return IMPULSE_VM_ERR_NULL_SNAPSHOT;
    }

    if (vm_state->register_types[dst] != TYPE_NODE_VECTOR && vm_state->register_types[dst] != TYPE_UINT64_VECTOR) {
        int h_dst = acquire_node_vector(vm_state->query_context);
        if (h_dst < 0) {
            return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        }
        vm_state->registers[dst] = h_dst;
    }
    vm_state->register_types[dst] = TYPE_NODE_VECTOR;

    int handle = static_cast<int>(vm_state->registers[dst]);
    auto& comp = vm_state->query_context->node_vectors[handle];
    size_t csc_bound = (slot.csc_node_count > 0) ? slot.csc_node_count : slot.node_count;
    size_t N = (csc_bound > 0) ? csc_bound : vm_state->query_context->max_nodes;
    if (slot.csc_node_count > N) N = slot.csc_node_count;
    N = std::min(N, comp.size());
    vm_state->query_context->node_vector_lengths[handle] = N;

#if defined(_OPENMP)
    #pragma omp parallel for schedule(static)
#endif
    for (size_t u = 0; u < N; ++u) {
        comp[u] = u;
    }

    auto find_root_u64 = [](uint64_t u, std::vector<uint64_t>& comp_vec) -> uint64_t {
        uint64_t curr = u;
        while (curr != comp_vec[curr]) {
            uint64_t parent = comp_vec[curr];
            uint64_t grandparent = comp_vec[parent];
            comp_vec[curr] = grandparent;
            curr = parent;
        }
        return curr;
    };

    auto union_u64 = [&](uint64_t u, uint64_t v, std::vector<uint64_t>& comp_vec) {
        uint64_t p1 = find_root_u64(u, comp_vec);
        uint64_t p2 = find_root_u64(v, comp_vec);
        while (p1 != p2) {
            uint64_t high = std::min(p1, p2);
            uint64_t low = std::max(p1, p2);
            uint64_t old_val = comp_vec[low];
            if (old_val == high) break;
            if (old_val > high) {
#if defined(_OPENMP)
                if (__sync_bool_compare_and_swap(&comp_vec[low], old_val, high)) {
                    if (old_val == low) break;
                }
#else
                comp_vec[low] = high;
                if (old_val == low) break;
#endif
            }
            p1 = find_root_u64(old_val, comp_vec);
            p2 = find_root_u64(high, comp_vec);
        }
    };

    for (size_t r = 0; r < 2; ++r) {
#if defined(_OPENMP)
        #pragma omp parallel for schedule(static, 2048)
#endif
        for (size_t u = 0; u < N; ++u) {
            if (u < slot.node_count) {
                uint64_t start = slot.get_csr_offset(u);
                uint64_t end = slot.get_csr_offset(u + 1);
                uint64_t deg = end - start;
                if (r < deg) {
                    uint64_t v = slot.get_csr_target(start + r);
                    if (v < N) {
                        union_u64(u, v, comp);
                    }
                }
            }
        }
    }

#if defined(_OPENMP)
    #pragma omp parallel for schedule(dynamic, 2048)
#endif
    for (size_t u = 0; u < N; ++u) {
        if (u < slot.node_count) {
            uint64_t start = slot.get_csr_offset(u);
            uint64_t end = slot.get_csr_offset(u + 1);
            uint64_t deg = end - start;
            for (uint64_t i = 0; i < deg; ++i) {
                uint64_t v = slot.get_csr_target(start + i);
                if (v < N) {
                    union_u64(u, v, comp);
                }
            }
        }
    }

#if defined(_OPENMP)
    #pragma omp parallel for schedule(static, 4096)
#endif
    for (size_t u = 0; u < N; ++u) {
        comp[u] = find_root_u64(u, comp);
    }

    vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;
    vm_state->pc++;
    DISPATCH();
}

