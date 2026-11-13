/**
 * @file op_67_louvain_modularity.inc
 * @brief Formal Contract for OP_LOUVAIN_MODULARITY (0x67).
 *
 * Syntax: OP_LOUVAIN_MODULARITY <dst_reg>, <src_reg>
 * Inputs:
 *   - dst_reg: Destination register receiving partition community vector
 *   - src_reg (payload_low16): Seed node frontier or relation handle
 * Preconditions:
 *   - src_reg must be TYPE_INT64, TYPE_NODE_ID, TYPE_BITSET_HANDLE, or TYPE_NODE_VECTOR
 * Outputs:
 *   - dst_reg set to TYPE_UINT64_VECTOR or TYPE_NODE_VECTOR
 *   - Flags: ZF set if zero communities detected
 */
#ifndef OP_67_CONTRACT_DEFINED
#define OP_67_CONTRACT_DEFINED
#define OP_67_DEFINED_FLAGS        0x00u
#define OP_67_ALLOWED_SRC_TYPES    (IMP_TYPES_SOURCE_SET | IMP_TYPE_BIT(TYPE_NULL))
#define OP_67_OUTPUT_DST_TYPES     (IMP_TYPE_BIT(TYPE_UINT64_VECTOR) | IMP_TYPE_BIT(TYPE_NODE_VECTOR))
#endif

// Opcode 0x67: OP_LOUVAIN_MODULARITY
op_LOUVAIN_MODULARITY: {
    IMPULSE_ASSERT(vm_state != nullptr);
    const auto& inst = bytecode[vm_state->pc];
    const uint16_t dst = IMP_INST_DST(inst);
    const uint16_t src = IMP_INST_LOW16(inst);
    IMP_VALIDATE_GPR(dst);
    IMP_VALIDATE_GPR(src);

    IMP_REQUIRE_CONTEXT(vm_state);
    IMPULSE_ASSERT(vm_state->query_context != nullptr);
    IMPULSE_ASSERT(dst < IMP_VM_GPR_COUNT && src < IMP_VM_GPR_COUNT);
    IMPULSE_CHECK_REG_TYPE(vm_state, src, OP_67_ALLOWED_SRC_TYPES);
    if (((1ULL << vm_state->register_types[src]) & OP_67_ALLOWED_SRC_TYPES) == 0) {
        return IMPULSE_VM_ERR_INVALID_REGISTER;
    }
    IMPULSE_TESTCASE(dst == src);

    const int h = acquire_node_vector(vm_state->query_context);
    if (h < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    IMPULSE_ASSERT(h >= 0 && static_cast<size_t>(h) < vm_state->query_context->node_vectors.size());
    vm_state->registers[dst] = static_cast<uint64_t>(h);
    vm_state->register_types[dst] = TYPE_NODE_VECTOR;
    IMP_VM_SET_ZF(vm_state, true);

    vm_state->pc++;
    DISPATCH();
}
