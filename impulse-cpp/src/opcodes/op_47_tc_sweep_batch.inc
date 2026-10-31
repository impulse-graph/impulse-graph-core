/**
 * Formal Opcode Contract: OP_TC_SWEEP_BATCH (0x47)
 *
 * Layout:
 *   dst_reg:          inst.dst_reg (DST_REG)
 *   allowed_src:      none
 *   requires_ctx:     false
 */
#ifndef OP_47_CONTRACT_DEFINED
#define OP_47_CONTRACT_DEFINED
#define OP_47_DEFINED_FLAGS        0x00u
#define OP_47_OUTPUT_DST_TYPES     IMP_TYPES_NONE
#endif

// Opcode 0x47: OP_TC_SWEEP_BATCH
op_TC_SWEEP_BATCH: {
    const auto& inst = bytecode[vm_state->pc];
    VALIDATE_REG(inst.dst_reg);
    vm_state->pc++;
    DISPATCH();
}
