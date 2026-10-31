/**
 * @file op_68_kcore_decomposition.inc
 * @brief Formal Contract for OP_KCORE_DECOMPOSITION (0x68).
 *
 * Syntax: OP_KCORE_DECOMPOSITION <dst_reg>, <src_reg> (payload: [src:8])
 * Inputs:
 *   - dst_reg: Target register receiving per-node coreness numbers (float vector)
 *   - src_reg (payload_low8): Register holding relation ID
 * Preconditions:
 *   - src_reg must be TYPE_INT64, TYPE_NODE_ID, or TYPE_RELATION_ID
 * Outputs:
 *   - dst_reg set to TYPE_FLOAT_VECTOR
 *   - Flags: ZF cleared
 */
#ifndef OP_68_CONTRACT_DEFINED
#define OP_68_CONTRACT_DEFINED
#define OP_68_DEFINED_FLAGS        0x00u
#define OP_68_ALLOWED_SRC_TYPES    (IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_RELATION_ID))
#define OP_68_OUTPUT_DST_TYPES     (IMP_TYPE_BIT(TYPE_FLOAT_VECTOR))
#endif

// Opcode 0x68: OP_KCORE_DECOMPOSITION
op_KCORE_DECOMPOSITION: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t r_src = inst.payload & 0xFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(r_src);

    if (!vm_state->query_context) return IMPULSE_VM_ERR_NULL_SNAPSHOT;
    IMPULSE_CHECK_REG_TYPE(vm_state, r_src, OP_68_ALLOWED_SRC_TYPES);

    uint16_t rel_id = static_cast<uint16_t>(vm_state->registers[r_src]);
    size_t n = 0;
    const BoundRelationSlot* slot_ptr = nullptr;
    if (rel_id < vm_state->query_context->slots.size()) {
        slot_ptr = &vm_state->query_context->slots[rel_id];
        n = std::max(slot_ptr->node_count, slot_ptr->csc_node_count);
    }
    if (n == 0) n = vm_state->query_context->max_nodes;

    int h_dst = acquire_float_vector(vm_state->query_context);
    if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    n = std::min(n, vm_state->query_context->float_vectors[h_dst].size());
    vm_state->query_context->float_vector_lengths[h_dst] = n;
    float* core = vm_state->query_context->float_vectors[h_dst].data();
    std::fill_n(core, n, 0.0f);

    if (slot_ptr && slot_ptr->offsets_ptr && slot_ptr->targets_ptr && n > 0) {
        const auto& slot = *slot_ptr;
        auto& deg = vm_state->query_context->scratch_int_deg;
        deg.clear();
        deg.resize(n, 0);
        int max_deg = 0;
        for (size_t i = 0; i < n; ++i) {
            int d = (i < slot.node_count) ? static_cast<int>(slot.get_csr_offset(i + 1) - slot.get_csr_offset(i)) : 0;
            deg[i] = d;
            if (d > max_deg) max_deg = d;
        }

        auto& vert = vm_state->query_context->scratch_int_vert;
        vert.clear();
        vert.resize(n, 0);
        auto& pos = vm_state->query_context->scratch_int_pos;
        pos.clear();
        pos.resize(n, 0);
        auto& bin = vm_state->query_context->scratch_int_bin;
        bin.clear();
        bin.resize(static_cast<size_t>(max_deg) + 1, 0);

        for (size_t i = 0; i < n; ++i) bin[deg[i]]++;
        int start = 0;
        for (int d = 0; d <= max_deg; ++d) {
            int num = bin[d];
            bin[d] = start;
            start += num;
        }
        for (size_t i = 0; i < n; ++i) {
            int p = bin[deg[i]]++;
            pos[i] = p;
            vert[p] = static_cast<int>(i);
        }
        for (int d = max_deg; d >= 1; --d) {
            bin[d] = bin[d - 1];
        }
        bin[0] = 0;

        for (size_t i = 0; i < n; ++i) {
            int v = vert[i];
            core[v] = static_cast<float>(deg[v]);
            uint64_t e_start = (static_cast<uint64_t>(v) < slot.node_count) ? slot.get_csr_offset(v) : 0;
            uint64_t e_end = (static_cast<uint64_t>(v) < slot.node_count) ? slot.get_csr_offset(v + 1) : 0;
            for (uint64_t e = e_start; e < e_end; ++e) {
                uint64_t u = slot.get_csr_target(e);
                if (u < n && deg[u] > deg[v]) {
                    int du = deg[u];
                    int pu = pos[u];
                    int pw = bin[du];
                    int w = vert[pw];
                    if (static_cast<size_t>(u) != static_cast<size_t>(w)) {
                        pos[u] = pw;
                        pos[w] = pu;
                        vert[pu] = w;
                        vert[pw] = static_cast<int>(u);
                    }
                    bin[du]++;
                    deg[u]--;
                }
            }
        }
    }

    vm_state->registers[dst] = static_cast<uint64_t>(h_dst);
    vm_state->register_types[dst] = TYPE_FLOAT_VECTOR;
    vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

    vm_state->pc++;
    DISPATCH();
}

