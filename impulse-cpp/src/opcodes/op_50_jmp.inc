/**
 * Formal Opcode Contract: OP_JMP (0x50)
 *
 * Layout:
 *   payload:          int32_t PC jump offset
 *   allowed_src:      none
 *   requires_ctx:     false
 */
#ifndef OP_50_CONTRACT_DEFINED
#define OP_50_CONTRACT_DEFINED
#define OP_50_DEFINED_FLAGS        0x00u
#define OP_50_OUTPUT_DST_TYPES     IMP_TYPES_NONE
#endif

// Opcode 0x50: OP_JMP
op_JMP: {
    IMPULSE_ASSERT(vm_state != nullptr);
    const auto& inst = bytecode[vm_state->pc];
    const int32_t offset = IMP_INST_PAYLOAD_I32(inst);
    IMPULSE_TESTCASE(offset < 0);
    IMPULSE_TESTCASE(offset == 0);
    IMPULSE_TESTCASE(offset > 0);
    vm_state->pc += offset;
    DISPATCH();
}
