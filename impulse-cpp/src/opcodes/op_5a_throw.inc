/**
 * Formal Opcode Contract: OP_THROW (0x5A)
 *
 * Layout:
 *   payload:          user error code payload (written to R0)
 *   allowed_src:      none
 *   requires_ctx:     false
 */
#ifndef OP_5A_CONTRACT_DEFINED
#define OP_5A_CONTRACT_DEFINED
#define OP_5A_DEFINED_FLAGS        0x00u
#define OP_5A_OUTPUT_DST_TYPES     IMP_TYPES_NONE
#define OP_5A_ERR_DST_REG          0u
#endif

// Opcode 0x5A: OP_THROW
op_THROW: {
    IMPULSE_ASSERT(vm_state != nullptr);
    const impulse_instruction_t& inst = bytecode[vm_state->pc];
    const uint32_t err_code = IMP_INST_PAYLOAD_U32(inst);
    IMPULSE_TESTCASE(err_code == 0u);
    vm_state->registers[OP_5A_ERR_DST_REG] = err_code;
    vm_state->register_types[OP_5A_ERR_DST_REG] = TYPE_INT64;
    return IMPULSE_VM_ERR_USER_THROW;
}
