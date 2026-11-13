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
#define OP_41_UNMASKED_FLAGS       0u
#define OP_41_WEIGHT_ATTR_SLOT     0u
#define OP_41_DECODE_MASK_REG(f)   ((f) > OP_41_UNMASKED_FLAGS ? static_cast<int>((f) - 1u) : -1)
#define OP_41_ALLOWED_SRC_TYPES    (IMP_TYPES_FLOAT_OR_DBL)
#define OP_41_OUTPUT_DST_TYPES     (IMP_TYPES_FLOAT_OR_DBL)
#endif

// Opcode 0x41: OP_MXV
op_MXV: {
    const auto& inst = bytecode[vm_state->pc];
    const uint16_t dst = IMP_INST_DST(inst);
    const int mask_reg_idx = OP_41_DECODE_MASK_REG(IMP_INST_FLAGS(inst));
    const uint16_t src_vec = IMP_INST_U8_0(inst);
    const uint16_t rel = IMP_INST_U8_1(inst);
    const uint16_t semiring_id = IMP_INST_HIGH16(inst);
    VALIDATE_REG(dst);
    VALIDATE_REG(src_vec);
    IMPULSE_ASSERT(dst < IMP_VM_GPR_COUNT && src_vec < IMP_VM_GPR_COUNT);
    IMP_REQUIRE_CONTEXT(vm_state);
    IMPULSE_ASSERT(vm_state->query_context != nullptr);
    IMPULSE_CHECK_REG_TYPE(vm_state, src_vec, OP_41_ALLOWED_SRC_TYPES);
    if (((1ULL << vm_state->register_types[src_vec]) & OP_41_ALLOWED_SRC_TYPES) == 0) {
        return IMPULSE_VM_ERR_INVALID_REGISTER;
    }

    IMP_REQUIRE_REL_SLOT(vm_state, rel);
    const auto& slot = vm_state->query_context->slots[rel];
    if (!slot.offsets_ptr || !slot.targets_ptr) {
        IMPULSE_TESTCASE(true);
        return IMPULSE_VM_ERR_NULL_SNAPSHOT;
    }

    size_t N = (slot.node_count > 0) ? slot.node_count : vm_state->query_context->max_nodes;
    size_t target_bound = (slot.csc_node_count > 0) ? std::max<size_t>(N, slot.csc_node_count) : vm_state->query_context->max_nodes;
    bool is_double = (vm_state->register_types[src_vec] == TYPE_DOUBLE_VECTOR);
    int h_src = static_cast<int>(vm_state->registers[src_vec]);

    const float* edge_weights_f32 = nullptr;
    if (rel < vm_state->query_context->attribute_slots.size() &&
        !vm_state->query_context->attribute_slots[rel].empty()) {
        const auto& a0 = vm_state->query_context->attribute_slots[rel][OP_41_WEIGHT_ATTR_SLOT];
        if (IMP_ATTR_BASE_TYPE(a0.type_code) == IMP_TYPE_CODE_FLOAT32 && a0.data_ptr != nullptr &&
            slot.edge_count > 0 && a0.data_bytes == slot.edge_count * sizeof(float)) {
            edge_weights_f32 = static_cast<const float*>(a0.data_ptr);
        }
    }
    IMPULSE_TESTCASE(mask_reg_idx >= 0);
    IMPULSE_TESTCASE(is_double);
    IMPULSE_TESTCASE(edge_weights_f32 != nullptr);
    IMPULSE_TESTCASE(dst == src_vec);

    if (is_double) {
        int h_dst = -1;
        if (vm_state->register_types[dst] == TYPE_DOUBLE_VECTOR && dst != src_vec) {
            h_dst = static_cast<int>(vm_state->registers[dst]);
        } else {
            h_dst = acquire_double_vector(vm_state->query_context);
            if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        }
        IMPULSE_ASSERT(h_dst >= 0 && static_cast<size_t>(h_dst) < vm_state->query_context->double_vectors.size());
        IMPULSE_ASSERT(h_src >= 0 && static_cast<size_t>(h_src) < vm_state->query_context->double_vectors.size());
        vm_state->query_context->double_vector_lengths[h_dst] = N;
        double* dst_data = vm_state->query_context->double_vectors[h_dst].data();
        const double* src_data = vm_state->query_context->double_vectors[h_src].data();

        #pragma omp parallel for schedule(static)
        for (size_t u = 0; u < N; ++u) {
            if (mask_reg_idx >= 0 && static_cast<uint32_t>(mask_reg_idx) < IMP_VM_GPR_COUNT) {
                size_t mask_handle = vm_state->registers[mask_reg_idx];
                if (!impulse_vm_context_bitset_test(vm_state->query_context, mask_handle, u)) {
                    dst_data[u] = 0.0;
                    continue;
                }
            }

            if (u < slot.node_count) {
                uint64_t start = slot.get_csr_offset(u);
                uint64_t end = slot.get_csr_offset(u + 1);
                IMPULSE_WELL_FORMED_ASSERT(start <= end);
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
                        double w = edge_weights_f32 ? static_cast<double>(edge_weights_f32[i]) : 1.0;
                        if (v < target_bound) min_val = std::min(min_val, src_data[v] + w);
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
        IMPULSE_ASSERT(h_dst >= 0 && static_cast<size_t>(h_dst) < vm_state->query_context->float_vectors.size());
        IMPULSE_ASSERT(h_src >= 0 && static_cast<size_t>(h_src) < vm_state->query_context->float_vectors.size());
        N = std::min(N, vm_state->query_context->float_vectors[h_dst].size());
        vm_state->query_context->float_vector_lengths[h_dst] = N;
        float* dst_data = vm_state->query_context->float_vectors[h_dst].data();
        const float* src_data = vm_state->query_context->float_vectors[h_src].data();

        #pragma omp parallel for schedule(static)
        for (size_t u = 0; u < N; ++u) {
            if (mask_reg_idx >= 0 && static_cast<uint32_t>(mask_reg_idx) < IMP_VM_GPR_COUNT) {
                size_t mask_handle = vm_state->registers[mask_reg_idx];
                if (!impulse_vm_context_bitset_test(vm_state->query_context, mask_handle, u)) {
                    dst_data[u] = 0.0f;
                    continue;
                }
            }

            if (u < slot.node_count) {
                uint64_t start = slot.get_csr_offset(u);
                uint64_t end = slot.get_csr_offset(u + 1);
                IMPULSE_WELL_FORMED_ASSERT(start <= end);
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
                        float w = edge_weights_f32 ? edge_weights_f32[i] : 1.0f;
                        if (v < target_bound) min_val = std::min(min_val, src_data[v] + w);
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

    IMP_VM_SET_ZF(vm_state, false);
    vm_state->pc++;
    DISPATCH();
}

