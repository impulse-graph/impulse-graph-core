/**
 * @file op_0a_load_inline_set.inc
 * @brief Formal Contract for OP_LOAD_INLINE_SET (0x0A).
 *
 * Mnemonic: OP_LOAD_INLINE_SET
 * Opcode: 0x0A
 * Description: Loads inline array of node IDs directly into an off-heap BitSet frontier.
 *
 * Operands:
 *  - dst = inst.dst_reg, offset = payload & 0xFFFF, count = payload >> 16
 *
 * Inputs:
 *  - query_context->inline_data_ptr
 *
 * Outputs:
 *  - dst: TYPE_BITSET_HANDLE
 *
 * Flags:
 *  - ZF: Cleared (0)
 *
 * Context Requirement:
 *  - Requires context (query_context inline data and bitset pool)
 */
#ifndef OP_0A_CONTRACT_DEFINED
#define OP_0A_CONTRACT_DEFINED 1
#define OP_0A_INLINE_ELEM_BYTES sizeof(uint32_t)
#endif

// Opcode 0x0A: OP_LOAD_INLINE_SET
op_LOAD_INLINE_SET: {
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
    if (offset_bytes + count * OP_0A_INLINE_ELEM_BYTES > vm_state->query_context->inline_data_bytes) {
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
    IMPULSE_TESTCASE(count == 0u);

    int h_dst = acquire_bitset(vm_state->query_context);
    if (h_dst < 0) {
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
    IMPULSE_ASSERT(h_dst >= 0 && static_cast<size_t>(h_dst) < vm_state->query_context->bitsets.size());
    vm_state->query_context->bitsets[h_dst].clear();

    const uint32_t* src_data = reinterpret_cast<const uint32_t*>(vm_state->query_context->inline_data_ptr + offset_bytes);
    for (uint16_t i = 0; i < count; ++i) {
        vm_state->query_context->bitsets[h_dst].set(src_data[i]);
    }

    vm_state->registers[dst] = static_cast<uint64_t>(h_dst);
    vm_state->register_types[dst] = TYPE_BITSET_HANDLE;
    IMP_VM_SET_ZF(vm_state, false);

    vm_state->pc++;
    DISPATCH();
}

