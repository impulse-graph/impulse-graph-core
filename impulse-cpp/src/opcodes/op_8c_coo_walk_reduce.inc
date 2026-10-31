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
#define OP_8C_ALLOWED_SRC_TYPES    (IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_BITSET_HANDLE))
#define OP_8C_OUTPUT_DST_TYPES     (IMP_TYPE_BIT(TYPE_FLOAT_VECTOR))
#endif

// Opcode 0x8C: OP_COO_WALK_REDUCE
op_COO_WALK_REDUCE: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src = inst.payload & 0xFF;
    uint16_t rel = (inst.payload >> 16) & 0xFFFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src);

    if (!vm_state->query_context) return IMPULSE_VM_ERR_NULL_SNAPSHOT;
    IMPULSE_CHECK_REG_TYPE(vm_state, src, OP_8C_ALLOWED_SRC_TYPES);

    bool src_is_bitset = (vm_state->register_types[src] == TYPE_BITSET_HANDLE);
    const VmBitSet* bs_src = nullptr;
    uint64_t scalar_src = vm_state->registers[src];
    if (src_is_bitset) {
        int h_src = static_cast<int>(scalar_src);
        if (h_src < 0 || h_src >= static_cast<int>(vm_state->query_context->bitsets.size())) {
            return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        }
        bs_src = &vm_state->query_context->bitsets[h_src];
    }

    if (vm_state->register_types[dst] != TYPE_FLOAT_VECTOR) {
        int h = acquire_float_vector(vm_state->query_context);
        if (h < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        vm_state->registers[dst] = h;
        vm_state->register_types[dst] = TYPE_FLOAT_VECTOR;
    }
    int h_dst = static_cast<int>(vm_state->registers[dst]);
    float* dst_vec = vm_state->query_context->float_vectors[h_dst].data();

    if (rel < vm_state->query_context->slots.size()) {
        const auto& slot = vm_state->query_context->slots[rel];
        if (slot.offsets_ptr && slot.targets_ptr) {
            size_t N = (slot.node_count > 0) ? slot.node_count : vm_state->query_context->max_nodes;
            if (src_is_bitset) {
                #pragma omp parallel for schedule(static)
                for (size_t u = 0; u < N; ++u) {
                    if (bs_src->test(u)) {
                        uint64_t start = slot.get_csr_offset(u);
                        uint64_t end = slot.get_csr_offset(u + 1);
                        for (uint64_t e = start; e < end; ++e) {
                            uint64_t v = slot.get_csr_target(e);
                            #pragma omp atomic
                            dst_vec[v] += 1.0f;
                        }
                    }
                }
            } else {
                size_t u = static_cast<size_t>(scalar_src);
                if (u < N) {
                    uint64_t start = slot.get_csr_offset(u);
                    uint64_t end = slot.get_csr_offset(u + 1);
                    for (uint64_t e = start; e < end; ++e) {
                        uint64_t v = slot.get_csr_target(e);
                        #pragma omp atomic
                        dst_vec[v] += 1.0f;
                    }
                }
            }
        }
    }
    vm_state->pc++;
    DISPATCH();
}

