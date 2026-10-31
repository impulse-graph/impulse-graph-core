/**
 * Formal Opcode Contract: OP_MXV (0x41)
 *
 * Layout:
 *   dst_reg:          inst.dst_reg (DST_REG)
 *   src_reg:          inst.payload & 0xFF (PAYLOAD_LOW8, src_vec)
 *   rel_id:           (inst.payload >> 8) & 0xFF
 *   semiring_id:      (inst.payload >> 16) & 0xFFFF
 *   flags:            optional mask register index + 1 (0 = unmasked)
 *   allowed_src:      IMP_TYPES_FLOAT_OR_DBL
 *   output_dst:       IMP_TYPES_FLOAT_OR_DBL
 *   requires_ctx:     true
 */
#ifndef OP_41_CONTRACT_DEFINED
#define OP_41_CONTRACT_DEFINED
#define OP_41_DEFINED_FLAGS        0xFFu
#define OP_41_ALLOWED_SRC_TYPES    (IMP_TYPES_FLOAT_OR_DBL)
#define OP_41_OUTPUT_DST_TYPES     (IMP_TYPES_FLOAT_OR_DBL)
#endif

// Opcode 0x41: OP_MXV
op_MXV: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    int mask_reg_idx = inst.flags > 0 ? (inst.flags - 1) : -1;
    uint16_t src_vec = inst.payload & 0xFF;
    uint16_t rel = (inst.payload >> 8) & 0xFF;
    uint16_t semiring_id = (inst.payload >> 16) & 0xFFFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src_vec);
    if (!vm_state->query_context) return IMPULSE_VM_ERR_NULL_SNAPSHOT;
    IMPULSE_CHECK_REG_TYPE(vm_state, src_vec, OP_41_ALLOWED_SRC_TYPES);

    if (rel >= vm_state->query_context->slots.size()) {
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
    const auto& slot = vm_state->query_context->slots[rel];
    if (!slot.offsets_ptr || !slot.targets_ptr) {
        return IMPULSE_VM_ERR_NULL_SNAPSHOT;
    }

    size_t N = (slot.node_count > 0) ? slot.node_count : vm_state->query_context->max_nodes;
    size_t target_bound = (slot.csc_node_count > 0) ? std::max<size_t>(N, slot.csc_node_count) : vm_state->query_context->max_nodes;
    bool is_double = (vm_state->register_types[src_vec] == TYPE_DOUBLE_VECTOR);
    int h_src = static_cast<int>(vm_state->registers[src_vec]);

    if (is_double) {
        int h_dst = -1;
        if (vm_state->register_types[dst] == TYPE_DOUBLE_VECTOR && dst != src_vec) {
            h_dst = static_cast<int>(vm_state->registers[dst]);
        } else {
            h_dst = acquire_double_vector(vm_state->query_context);
            if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        }
        vm_state->query_context->double_vector_lengths[h_dst] = N;
        double* dst_data = vm_state->query_context->double_vectors[h_dst].data();
        const double* src_data = vm_state->query_context->double_vectors[h_src].data();

        #pragma omp parallel for schedule(static)
        for (size_t u = 0; u < N; ++u) {
            if (mask_reg_idx >= 0 && mask_reg_idx < 64) {
                size_t mask_handle = vm_state->registers[mask_reg_idx];
                if (!impulse_vm_context_bitset_test(vm_state->query_context, mask_handle, u)) {
                    dst_data[u] = 0.0;
                    continue;
                }
            }

            if (u < slot.node_count) {
                uint64_t start = slot.get_csr_offset(u);
                uint64_t end = slot.get_csr_offset(u + 1);
                if (semiring_id == SEMIRING_PLUS_TIMES) {
                    double sum = 0.0;
                    for (uint64_t i = start; i < end; ++i) {
                        uint64_t v = slot.get_csr_target(i);
                        if (v < target_bound) sum += src_data[v];
                    }
                    dst_data[u] = sum;
                } else if (semiring_id == SEMIRING_MIN_PLUS) {
                    double min_val = std::numeric_limits<double>::infinity();
                    for (uint64_t i = start; i < end; ++i) {
                        uint64_t v = slot.get_csr_target(i);
                        if (v < target_bound) min_val = std::min(min_val, src_data[v] + 1.0);
                    }
                    dst_data[u] = min_val;
                } else if (semiring_id == SEMIRING_MAX_MIN) {
                    double max_val = -std::numeric_limits<double>::infinity();
                    for (uint64_t i = start; i < end; ++i) {
                        uint64_t v = slot.get_csr_target(i);
                        if (v < target_bound) max_val = std::max(max_val, std::min(src_data[v], 1.0));
                    }
                    dst_data[u] = (start == end) ? 0.0 : max_val;
                } else if (semiring_id == SEMIRING_BOOL) {
                    double any = 0.0;
                    for (uint64_t i = start; i < end; ++i) {
                        uint64_t v = slot.get_csr_target(i);
                        if (v < target_bound && src_data[v] != 0.0) { any = 1.0; break; }
                    }
                    dst_data[u] = any;
                } else {
                    dst_data[u] = 0.0;
                }
            } else {
                dst_data[u] = 0.0;
            }
        }
        vm_state->registers[dst] = h_dst;
        vm_state->register_types[dst] = TYPE_DOUBLE_VECTOR;
    } else {
        int h_dst = -1;
        if (vm_state->register_types[dst] == TYPE_FLOAT_VECTOR && dst != src_vec) {
            h_dst = static_cast<int>(vm_state->registers[dst]);
        } else {
            h_dst = acquire_float_vector(vm_state->query_context);
            if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        }
        N = std::min(N, vm_state->query_context->float_vectors[h_dst].size());
        vm_state->query_context->float_vector_lengths[h_dst] = N;
        float* dst_data = vm_state->query_context->float_vectors[h_dst].data();
        const float* src_data = vm_state->query_context->float_vectors[h_src].data();

        #pragma omp parallel for schedule(static)
        for (size_t u = 0; u < N; ++u) {
            if (mask_reg_idx >= 0 && mask_reg_idx < 64) {
                size_t mask_handle = vm_state->registers[mask_reg_idx];
                if (!impulse_vm_context_bitset_test(vm_state->query_context, mask_handle, u)) {
                    dst_data[u] = 0.0f;
                    continue;
                }
            }

            if (u < slot.node_count) {
                uint64_t start = slot.get_csr_offset(u);
                uint64_t end = slot.get_csr_offset(u + 1);
                if (semiring_id == SEMIRING_PLUS_TIMES) {
                    float sum = 0.0f;
                    for (uint64_t i = start; i < end; ++i) {
                        uint64_t v = slot.get_csr_target(i);
                        if (v < target_bound) sum += src_data[v];
                    }
                    dst_data[u] = sum;
                } else if (semiring_id == SEMIRING_MIN_PLUS) {
                    float min_val = std::numeric_limits<float>::infinity();
                    for (uint64_t i = start; i < end; ++i) {
                        uint64_t v = slot.get_csr_target(i);
                        if (v < target_bound) min_val = std::min(min_val, src_data[v] + 1.0f);
                    }
                    dst_data[u] = min_val;
                } else if (semiring_id == SEMIRING_MAX_MIN) {
                    float max_val = -std::numeric_limits<float>::infinity();
                    for (uint64_t i = start; i < end; ++i) {
                        uint64_t v = slot.get_csr_target(i);
                        if (v < target_bound) max_val = std::max(max_val, std::min(src_data[v], 1.0f));
                    }
                    dst_data[u] = (start == end) ? 0.0f : max_val;
                } else if (semiring_id == SEMIRING_BOOL) {
                    float any = 0.0f;
                    for (uint64_t i = start; i < end; ++i) {
                        uint64_t v = slot.get_csr_target(i);
                        if (v < target_bound && src_data[v] != 0.0f) { any = 1.0f; break; }
                    }
                    dst_data[u] = any;
                } else {
                    dst_data[u] = 0.0f;
                }
            } else {
                dst_data[u] = 0.0f;
            }
        }
        vm_state->registers[dst] = h_dst;
        vm_state->register_types[dst] = TYPE_FLOAT_VECTOR;
    }

    vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;
    vm_state->pc++;
    DISPATCH();
}

