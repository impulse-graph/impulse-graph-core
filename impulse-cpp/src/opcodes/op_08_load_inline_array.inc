/**
 * @file op_08_load_inline_array.inc
 * @brief Formal Contract for OP_LOAD_INLINE_ARRAY (0x08).
 *
 * Mnemonic: OP_LOAD_INLINE_ARRAY
 * Opcode: 0x08
 * Description: Loads contiguous inline numerical/node array slice into float or node vector handle.
 *
 * Operands:
 *  - dst = inst.dst_reg, offset = payload & 0xFFFF, count = payload >> 16
 *
 * Inputs:
 *  - query_context->inline_data_ptr
 *
 * Outputs:
 *  - dst: TYPE_FLOAT_VECTOR (flags=0) or TYPE_NODE_VECTOR (flags=1)
 *
 * Flags:
 *  - ZF: Cleared (0)
 *
 * Context Requirement:
 *  - Requires context (query_context inline data and vector pool)
 */
#ifndef OP_08_CONTRACT_DEFINED
#define OP_08_CONTRACT_DEFINED 1
#define OP_08_FLAG_NODE_VECTOR  IMP_FLAG_BIT0
#define OP_08_INLINE_ELEM_BYTES sizeof(uint32_t)
#endif

// Opcode 0x08: OP_LOAD_INLINE_ARRAY
op_LOAD_INLINE_ARRAY: {
    const impulse_instruction_t& inst = bytecode[vm_state->pc];
    const uint16_t dst = IMP_INST_DST(inst);
    const uint16_t offset_bytes = IMP_INST_LOW16(inst);
    const uint16_t count = IMP_INST_HIGH16(inst);
    VALIDATE_REG(dst);
    IMPULSE_ASSERT(dst < IMP_VM_GPR_COUNT);

    IMP_REQUIRE_CONTEXT(vm_state);
    IMPULSE_ASSERT(vm_state->query_context != nullptr);
    IMPULSE_TESTCASE(vm_state->query_context->inline_data_ptr == nullptr);
    if (!vm_state->query_context->inline_data_ptr) {
        return IMPULSE_VM_ERR_NULL_SNAPSHOT;
    }
    if (offset_bytes + count * OP_08_INLINE_ELEM_BYTES > vm_state->query_context->inline_data_bytes) {
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
    IMPULSE_TESTCASE(count == 0u);

    int h_dst = acquire_float_vector(vm_state->query_context);
    if (h_dst < 0) {
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
    IMPULSE_ASSERT(h_dst >= 0 && static_cast<size_t>(h_dst) < vm_state->query_context->float_vectors.size());
    size_t copy_count = std::min<size_t>(count, vm_state->query_context->float_vectors[h_dst].size());

    float* dst_vec_f = vm_state->query_context->float_vectors[h_dst].data();
    std::memcpy(dst_vec_f, vm_state->query_context->inline_data_ptr + offset_bytes, copy_count * OP_08_INLINE_ELEM_BYTES);
    vm_state->query_context->float_vector_lengths[h_dst] = copy_count;

    vm_state->query_context->node_vectors_allocated[h_dst] = true;
    vm_state->query_context->node_vector_lengths[h_dst] = copy_count;
    vm_state->query_context->node_vector_length_explicit[h_dst] = true;
    uint64_t* dst_vec_u = vm_state->query_context->node_vectors[h_dst].data();
    const uint32_t* raw_u32 = reinterpret_cast<const uint32_t*>(vm_state->query_context->inline_data_ptr + offset_bytes);
    for (size_t i = 0; i < copy_count; ++i) {
        dst_vec_u[i] = static_cast<uint64_t>(static_cast<int64_t>(static_cast<int32_t>(raw_u32[i])));
    }

    const bool as_node_vec = IMP_INST_HAS_FLAG(inst, OP_08_FLAG_NODE_VECTOR);
    IMPULSE_TESTCASE(as_node_vec);
    vm_state->registers[dst] = static_cast<uint64_t>(h_dst);
    vm_state->register_types[dst] = as_node_vec ? TYPE_NODE_VECTOR : TYPE_FLOAT_VECTOR;
    IMP_VM_SET_ZF(vm_state, false);

    vm_state->pc++;
    DISPATCH();
}

