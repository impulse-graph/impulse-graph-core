// Opcode 0x08: OP_LOAD_INLINE_ARRAY
op_LOAD_INLINE_ARRAY: {
    const impulse_instruction_t& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t offset_bytes = inst.payload & 0xFFFF;
    uint16_t count = (inst.payload >> 16) & 0xFFFF;
    VALIDATE_REG(dst);

    if (!vm_state->query_context || !vm_state->query_context->inline_data_ptr) {
        return IMPULSE_VM_ERR_NULL_SNAPSHOT;
    }
    if (offset_bytes + count * sizeof(uint32_t) > vm_state->query_context->inline_data_bytes) {
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }

    int h_dst = acquire_float_vector(vm_state->query_context);
    if (h_dst < 0) {
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
    size_t copy_count = std::min<size_t>(count, vm_state->query_context->float_vectors[h_dst].size());

    float* dst_vec_f = vm_state->query_context->float_vectors[h_dst].data();
    std::memcpy(dst_vec_f, vm_state->query_context->inline_data_ptr + offset_bytes, copy_count * sizeof(uint32_t));
    vm_state->query_context->float_vector_lengths[h_dst] = copy_count;

    vm_state->query_context->node_vectors_allocated[h_dst] = true;
    vm_state->query_context->node_vector_lengths[h_dst] = copy_count;
    vm_state->query_context->node_vector_length_explicit[h_dst] = true;
    uint64_t* dst_vec_u = vm_state->query_context->node_vectors[h_dst].data();
    const uint32_t* raw_u32 = reinterpret_cast<const uint32_t*>(vm_state->query_context->inline_data_ptr + offset_bytes);
    for (size_t i = 0; i < copy_count; ++i) {
        dst_vec_u[i] = static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(raw_u32[i])));
    }

    vm_state->registers[dst] = h_dst;
    vm_state->register_types[dst] = (inst.flags & 0x01) ? TYPE_NODE_VECTOR : TYPE_FLOAT_VECTOR;
    vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

    vm_state->pc++;
    DISPATCH();
}

