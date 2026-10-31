/**
 * Formal Opcode Contract: OP_BRANDES_BACKWARD (0x49)
 *
 * Layout:
 *   dst_reg:          inst.dst_reg (DST_REG)
 *   allowed_src:      none
 *   requires_ctx:     false
 */
#ifndef OP_49_CONTRACT_DEFINED
#define OP_49_CONTRACT_DEFINED
#define OP_49_DEFINED_FLAGS        0x00u
#define OP_49_OUTPUT_DST_TYPES     IMP_TYPES_NONE
#endif

// Opcode 0x49: OP_BRANDES_BACKWARD
op_BRANDES_BACKWARD: {
    const auto& inst = bytecode[vm_state->pc];
    VALIDATE_REG(inst.dst_reg);
    vm_state->pc++;
    DISPATCH();
}
