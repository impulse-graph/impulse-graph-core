/**
 * Formal Opcode Contract: OP_CALL (0x55)
 *
 * Layout:
 *   payload:          uint32_t callee target PC
 *   allowed_src:      none
 *   requires_ctx:     false
 */
#ifndef OP_55_CONTRACT_DEFINED
#define OP_55_CONTRACT_DEFINED
#define OP_55_DEFINED_FLAGS        0x00u
#define OP_55_OUTPUT_DST_TYPES     IMP_TYPES_NONE
#define OP_55_MAX_CALL_DEPTH       8u
#define OP_55_OUT_REG_BASE         12u
#define OP_55_IN_REG_BASE          0u
#define OP_55_WINDOW_ARITY         4u
#endif

// Opcode 0x55: OP_CALL
op_CALL: {
    IMPULSE_ASSERT(vm_state != nullptr);
    const auto& inst = bytecode[vm_state->pc];
    const uint32_t func_offset = IMP_INST_PAYLOAD_U32(inst);
    IMPULSE_TESTCASE(vm_state->call_stack_depth == 0);
    IMPULSE_TESTCASE(vm_state->call_stack_depth == (OP_55_MAX_CALL_DEPTH - 1u));
    IMPULSE_TESTCASE(vm_state->call_stack_depth >= OP_55_MAX_CALL_DEPTH);
    if (IMPULSE_UNLIKELY(vm_state->call_stack_depth >= OP_55_MAX_CALL_DEPTH)) {
        return IMPULSE_VM_ERR_STACK_OVERFLOW;
    }
    IMPULSE_ASSERT(vm_state->call_stack_depth < OP_55_MAX_CALL_DEPTH);
    vm_state->call_stack[vm_state->call_stack_depth++] = vm_state->pc + 1;

    // Register Windowing: Pass Out registers (R12..R15) to Callee In registers (R0..R3)
    const uint64_t arg0 = vm_state->registers[OP_55_OUT_REG_BASE + 0u];
    const uint64_t arg1 = vm_state->registers[OP_55_OUT_REG_BASE + 1u];
    const uint64_t arg2 = vm_state->registers[OP_55_OUT_REG_BASE + 2u];
    const uint64_t arg3 = vm_state->registers[OP_55_OUT_REG_BASE + 3u];
    const uint8_t type0 = vm_state->register_types[OP_55_OUT_REG_BASE + 0u];
    const uint8_t type1 = vm_state->register_types[OP_55_OUT_REG_BASE + 1u];
    const uint8_t type2 = vm_state->register_types[OP_55_OUT_REG_BASE + 2u];
    const uint8_t type3 = vm_state->register_types[OP_55_OUT_REG_BASE + 3u];

    vm_state->registers[OP_55_IN_REG_BASE + 0u] = arg0;
    vm_state->registers[OP_55_IN_REG_BASE + 1u] = arg1;
    vm_state->registers[OP_55_IN_REG_BASE + 2u] = arg2;
    vm_state->registers[OP_55_IN_REG_BASE + 3u] = arg3;
    vm_state->register_types[OP_55_IN_REG_BASE + 0u] = type0;
    vm_state->register_types[OP_55_IN_REG_BASE + 1u] = type1;
    vm_state->register_types[OP_55_IN_REG_BASE + 2u] = type2;
    vm_state->register_types[OP_55_IN_REG_BASE + 3u] = type3;

    IMPULSE_TESTCASE(func_offset == 0u);
    vm_state->pc = func_offset;
    DISPATCH();
}
