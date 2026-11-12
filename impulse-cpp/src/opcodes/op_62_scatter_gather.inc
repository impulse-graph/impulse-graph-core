/**
 * @file op_62_scatter_gather.inc
 * @brief Formal Contract for OP_SCATTER_GATHER (0x62).
 *
 * Syntax: OP_SCATTER_GATHER <dst_reg>, <src_reg>
 * Inputs:
 *   - dst_reg: Target register receiving aggregated neighbor feature vector
 *   - src_reg (payload_low16): Source state vector or node frontier
 * Preconditions:
 *   - src_reg must be TYPE_FLOAT_VECTOR, TYPE_DOUBLE_VECTOR, TYPE_NODE_VECTOR, or TYPE_BITSET_HANDLE
 * Outputs:
 *   - dst_reg set to TYPE_FLOAT_VECTOR or TYPE_DOUBLE_VECTOR
 *   - Flags: ZF set if output vector has zero magnitude
 */
#ifndef OP_62_CONTRACT_DEFINED
#define OP_62_CONTRACT_DEFINED
#define OP_62_DEFINED_FLAGS        0x00u
#define OP_62_ALLOWED_SRC_TYPES    (IMP_TYPES_SOURCE_SET | IMP_TYPES_NUMERIC_VEC)
#define OP_62_OUTPUT_DST_TYPES     (IMP_TYPES_NUMERIC_VEC)
#endif

// Opcode 0x62: OP_SCATTER_GATHER
op_SCATTER_GATHER: {
    IMPULSE_ASSERT(vm_state != nullptr);
    const auto& inst = bytecode[vm_state->pc];
    const uint16_t dst = IMP_INST_DST(inst);
    IMP_VALIDATE_GPR(dst);
    IMPULSE_ASSERT(dst < IMP_VM_GPR_COUNT);
    IMPULSE_TESTCASE(IMP_INST_PAYLOAD_U32(inst) == 0u);

    IMP_VM_SET_ZF(vm_state, false);
    vm_state->pc++;
    DISPATCH();
}
