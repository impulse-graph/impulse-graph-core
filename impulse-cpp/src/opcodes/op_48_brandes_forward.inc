/**
 * Formal Opcode Contract: OP_BRANDES_FORWARD (0x48)
 *
 * Layout:
 *   dst_reg:          inst.dst_reg (DST_REG)
 *   allowed_src:      none
 *   requires_ctx:     false
 */
#ifndef OP_48_CONTRACT_DEFINED
#define OP_48_CONTRACT_DEFINED
#define OP_48_DEFINED_FLAGS        0x00u
#define OP_48_OUTPUT_DST_TYPES     IMP_TYPES_NONE
#endif

// Opcode 0x48: OP_BRANDES_FORWARD
op_BRANDES_FORWARD: {
    const auto& inst = bytecode[vm_state->pc];
    VALIDATE_REG(inst.dst_reg);
    vm_state->pc++;
    DISPATCH();
}
