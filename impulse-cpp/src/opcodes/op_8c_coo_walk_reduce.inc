/**
 * @file op_8c_coo_walk_reduce.inc
 * @brief Formal Contract for OP_COO_WALK_REDUCE (0x8C).
 *
 * Syntax: OP_COO_WALK_REDUCE <dst_reg>, <src_reg> (payload: [rel_id:16 | ... | src:8])
 * Inputs:
 *   - dst_reg: Target register receiving accumulated degree/count float vector
 *   - src_reg (payload_low8): Source frontier bitset
 * Preconditions:
 *   - src_reg must be TYPE_BITSET_HANDLE
 * Outputs:
 *   - dst_reg set to TYPE_FLOAT_VECTOR
 *   - Flags: None
 */
#ifndef OP_8C_CONTRACT_DEFINED
#define OP_8C_CONTRACT_DEFINED
#define OP_8C_DEFINED_FLAGS        0x00u
#define OP_8C_ALLOWED_SRC_TYPES    (IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_BITSET_HANDLE) | IMP_TYPE_BIT(TYPE_NULL))
#define OP_8C_OUTPUT_DST_TYPES     (IMP_TYPE_BIT(TYPE_FLOAT_VECTOR))
#define OP_8C_UNIT_EDGE_WEIGHT     1.0f
#endif

// Opcode 0x8C: OP_COO_WALK_REDUCE
op_COO_WALK_REDUCE: {
    IMPULSE_ASSERT(vm_state != nullptr);
    const auto& inst = bytecode[vm_state->pc];
    const uint16_t dst = IMP_INST_DST(inst);
    const uint16_t src = IMP_INST_U8_0(inst);
    const uint16_t rel = IMP_INST_HIGH16(inst);
    IMP_VALIDATE_GPR(dst);
    IMP_VALIDATE_GPR(src);

    IMP_REQUIRE_CONTEXT(vm_state);
    IMPULSE_ASSERT(vm_state->query_context != nullptr);
    IMPULSE_ASSERT(dst < IMP_VM_GPR_COUNT && src < IMP_VM_GPR_COUNT);
    IMPULSE_CHECK_REG_TYPE(vm_state, src, OP_8C_ALLOWED_SRC_TYPES);

    const bool src_is_null = (vm_state->register_types[src] == TYPE_NULL);
    const bool src_is_bitset = (vm_state->register_types[src] == TYPE_BITSET_HANDLE);
    IMPULSE_TESTCASE(src_is_null);
    IMPULSE_TESTCASE(src_is_bitset);

    const VmBitSet* bs_src = nullptr;
    const uint64_t scalar_src = vm_state->registers[src];
    if (src_is_bitset) {
        const int h_src = static_cast<int>(scalar_src);
        if (h_src < 0 || static_cast<size_t>(h_src) >= vm_state->query_context->bitsets.size()) {
            return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        }
        bs_src = &vm_state->query_context->bitsets[h_src];
    }

    if (vm_state->register_types[dst] != TYPE_FLOAT_VECTOR ||
        vm_state->registers[dst] >= vm_state->query_context->float_vectors.size()) {
        const int h = acquire_float_vector(vm_state->query_context);
        if (h < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        vm_state->registers[dst] = static_cast<uint64_t>(h);
        vm_state->register_types[dst] = TYPE_FLOAT_VECTOR;
    }
    const int h_dst = static_cast<int>(vm_state->registers[dst]);
    IMPULSE_ASSERT(h_dst >= 0 && static_cast<size_t>(h_dst) < vm_state->query_context->float_vectors.size());
    float* dst_vec = vm_state->query_context->float_vectors[h_dst].data();
    const size_t dst_cap = vm_state->query_context->float_vectors[h_dst].size();
    if (src_is_null) {
        vm_state->query_context->float_vector_lengths[h_dst] = 0;
    }

    const bool valid_rel = (rel < vm_state->query_context->slots.size());
    IMPULSE_TESTCASE(!valid_rel);
    if (valid_rel) {
        const auto& slot = vm_state->query_context->slots[rel];
        if (slot.offsets_ptr && slot.targets_ptr) {
            const size_t N = (slot.node_count > 0) ? slot.node_count : vm_state->query_context->max_nodes;
            if (src_is_bitset) {
                IMPULSE_ASSERT(bs_src != nullptr);
                #pragma omp parallel for schedule(static)
                for (size_t u = 0; u < N; ++u) {
                    if (bs_src->test(u)) {
                        const uint64_t start = slot.get_csr_offset(u);
                        const uint64_t end = slot.get_csr_offset(u + 1);
                        IMPULSE_WELL_FORMED_ASSERT(start <= end);
                        for (uint64_t e = start; e < end; ++e) {
                            const uint64_t v = slot.get_csr_target(e);
                            if (v < dst_cap) {
                                #pragma omp atomic
                                dst_vec[v] += OP_8C_UNIT_EDGE_WEIGHT;
                            }
                        }
                    }
                }
            } else if (!src_is_null) {
                const size_t u = static_cast<size_t>(scalar_src);
                if (u < N) {
                    const uint64_t start = slot.get_csr_offset(u);
                    const uint64_t end = slot.get_csr_offset(u + 1);
                    IMPULSE_WELL_FORMED_ASSERT(start <= end);
                    for (uint64_t e = start; e < end; ++e) {
                        const uint64_t v = slot.get_csr_target(e);
                        if (v < dst_cap) {
                            #pragma omp atomic
                            dst_vec[v] += OP_8C_UNIT_EDGE_WEIGHT;
                        }
                    }
                }
            }
        }
    }
    vm_state->pc++;
    DISPATCH();
}
