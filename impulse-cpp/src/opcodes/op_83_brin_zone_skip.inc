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
#define OP_83_ALLOWED_SRC_TYPES    (IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_BITSET_HANDLE))
#define OP_83_OUTPUT_DST_TYPES     (IMP_TYPE_BIT(TYPE_INT64))
#endif

// Opcode 0x83: OP_BRIN_ZONE_SKIP
op_BRIN_ZONE_SKIP: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src = inst.payload & 0xFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src);

    IMPULSE_CHECK_REG_TYPE(vm_state, src, OP_83_ALLOWED_SRC_TYPES);

    vm_state->registers[dst] = 1;
    vm_state->register_types[dst] = TYPE_INT64;
    vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;
    vm_state->pc++;
    DISPATCH();
}

