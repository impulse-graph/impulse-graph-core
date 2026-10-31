/**
 * Formal Opcode Contract: OP_PROJECT_STATE (0x3A)
 *
 * Layout:
 *   dst_reg:          inst.dst_reg (DST_REG)
 *   src_reg:          inst.payload & 0xFFFF (PAYLOAD_LOW16)
 *   allowed_src:      TYPE_FRONTIER_STATE
 *   output_dst:       TYPE_FRONTIER_STATE
 *   requires_ctx:     false
 */
#ifndef OP_3A_CONTRACT_DEFINED
#define OP_3A_CONTRACT_DEFINED
#define OP_3A_DEFINED_FLAGS        0x00u
#define OP_3A_ALLOWED_SRC_TYPES    (IMP_TYPE_BIT(TYPE_FRONTIER_STATE))
#define OP_3A_OUTPUT_DST_TYPES     (IMP_TYPE_BIT(TYPE_FRONTIER_STATE))
#endif

// Opcode 0x3A: OP_PROJECT_STATE
op_PROJECT_STATE: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src = inst.payload & 0xFFFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src);
    IMPULSE_CHECK_REG_TYPE(vm_state, src, OP_3A_ALLOWED_SRC_TYPES);
    vm_state->register_types[dst] = TYPE_FRONTIER_STATE;
    vm_state->registers[dst] = vm_state->registers[src];
    vm_state->pc++;
    DISPATCH();
}

