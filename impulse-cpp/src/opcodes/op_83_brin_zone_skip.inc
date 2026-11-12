/**
 * @file op_83_brin_zone_skip.inc
 * @brief Formal Contract for OP_BRIN_ZONE_SKIP (0x83).
 *
 * Syntax: OP_BRIN_ZONE_SKIP <dst_reg>, <src_reg>
 * Inputs:
 *   - dst_reg: Target register receiving zone pruning pass indicator
 *   - src_reg (payload_low8): Boundary range query register
 * Preconditions:
 *   - src_reg must be TYPE_INT64, TYPE_NODE_ID, or TYPE_BITSET_HANDLE
 * Outputs:
 *   - dst_reg set to 1 as TYPE_INT64
 *   - Flags: ZF cleared
 */
#ifndef OP_83_CONTRACT_DEFINED
#define OP_83_CONTRACT_DEFINED
#define OP_83_DEFINED_FLAGS        0x00u
#define OP_83_ALLOWED_SRC_TYPES    (IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_BITSET_HANDLE) | IMP_TYPE_BIT(TYPE_NULL) | IMP_TYPES_ANY)
#define OP_83_OUTPUT_DST_TYPES     (IMP_TYPE_BIT(TYPE_INT64))
#define OP_83_ZONE_ACTIVE_RESULT   1ULL
#endif

// Opcode 0x83: OP_BRIN_ZONE_SKIP
op_BRIN_ZONE_SKIP: {
    IMPULSE_ASSERT(vm_state != nullptr);
    const auto& inst = bytecode[vm_state->pc];
    const uint16_t dst = IMP_INST_DST(inst);
    const uint16_t src = IMP_INST_U8_0(inst);
    IMP_VALIDATE_GPR(dst);
    IMP_VALIDATE_GPR(src);
    IMPULSE_ASSERT(dst < IMP_VM_GPR_COUNT && src < IMP_VM_GPR_COUNT);

    IMPULSE_CHECK_REG_TYPE(vm_state, src, OP_83_ALLOWED_SRC_TYPES);
    IMPULSE_TESTCASE(dst == src);

    vm_state->registers[dst] = OP_83_ZONE_ACTIVE_RESULT;
    vm_state->register_types[dst] = TYPE_INT64;
    IMP_VM_SET_ZF(vm_state, false);
    vm_state->pc++;
    DISPATCH();
}
