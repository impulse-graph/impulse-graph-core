// Opcode 0x8C: OP_COO_WALK_REDUCE
op_COO_WALK_REDUCE: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src = inst.payload & 0xFF;
    uint16_t rel = (inst.payload >> 16) & 0xFFFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src);

    int h_src = static_cast<int>(vm_state->registers[src]);
    const auto& bs_src = vm_state->query_context->bitsets[h_src];

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
            #pragma omp parallel for schedule(static)
            for (size_t u = 0; u < N; ++u) {
                if (bs_src.test(u)) {
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

