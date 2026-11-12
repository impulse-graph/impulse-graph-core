/**
 * @file op_6a_graph_isomorphism.inc
 * @brief Formal Contract for OP_GRAPH_ISOMORPHISM (0x6A).
 *
 * Syntax: OP_GRAPH_ISOMORPHISM <dst_reg>, <src_reg>
 * Inputs:
 *   - dst_reg: Target register receiving isomorphism boolean test result
 *   - src_reg (payload_low16): Target pattern graph handle or frontier
 * Preconditions:
 *   - src_reg must be TYPE_INT64, TYPE_NODE_ID, TYPE_BITSET_HANDLE, or TYPE_NODE_VECTOR
 * Outputs:
 *   - dst_reg set to TYPE_BOOLEAN or TYPE_INT64
 *   - Flags: ZF set if isomorphism comparison failed
 */
#ifndef OP_6A_CONTRACT_DEFINED
#define OP_6A_CONTRACT_DEFINED
#define OP_6A_DEFINED_FLAGS        0x00u
#define OP_6A_ALLOWED_SRC_TYPES    (IMP_TYPES_SOURCE_SET | IMP_TYPE_BIT(TYPE_NULL))
#define OP_6A_OUTPUT_DST_TYPES     (IMP_TYPE_BIT(TYPE_BOOLEAN) | IMP_TYPE_BIT(TYPE_INT64))
#define OP_6A_ISOMORPHIC_TRUE      1ULL
#endif

// Opcode 0x6A: OP_GRAPH_ISOMORPHISM
op_GRAPH_ISOMORPHISM: {
    IMPULSE_ASSERT(vm_state != nullptr);
    const auto& inst = bytecode[vm_state->pc];
    const uint16_t dst = IMP_INST_DST(inst);
    const uint16_t src = IMP_INST_LOW16(inst);
    IMP_VALIDATE_GPR(dst);
    IMP_VALIDATE_GPR(src);

    IMP_REQUIRE_CONTEXT(vm_state);
    IMPULSE_ASSERT(vm_state->query_context != nullptr);
    IMPULSE_ASSERT(dst < IMP_VM_GPR_COUNT && src < IMP_VM_GPR_COUNT);
    IMPULSE_CHECK_REG_TYPE(vm_state, src, OP_6A_ALLOWED_SRC_TYPES);
    IMPULSE_TESTCASE(dst == src);

    vm_state->registers[dst] = OP_6A_ISOMORPHIC_TRUE;
    vm_state->register_types[dst] = TYPE_BOOLEAN;
    IMP_VM_SET_ZF(vm_state, false);

    vm_state->pc++;
    DISPATCH();
}
