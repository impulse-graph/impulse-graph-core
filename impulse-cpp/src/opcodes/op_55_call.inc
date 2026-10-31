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
#endif

// Opcode 0x55: OP_CALL
op_CALL: {
    const auto& inst = bytecode[vm_state->pc];
    uint32_t func_offset = inst.payload;
    if (vm_state->call_stack_depth >= 8) return IMPULSE_VM_ERR_STACK_OVERFLOW;
    vm_state->call_stack[vm_state->call_stack_depth++] = vm_state->pc + 1;
    
    // Register Windowing: Pass Out registers (R12..R15) to Callee In registers (R0..R3)
    uint64_t arg0 = vm_state->registers[12];
    uint64_t arg1 = vm_state->registers[13];
    uint64_t arg2 = vm_state->registers[14];
    uint64_t arg3 = vm_state->registers[15];
    uint8_t type0 = vm_state->register_types[12];
    uint8_t type1 = vm_state->register_types[13];
    uint8_t type2 = vm_state->register_types[14];
    uint8_t type3 = vm_state->register_types[15];

    vm_state->registers[0] = arg0;
    vm_state->registers[1] = arg1;
    vm_state->registers[2] = arg2;
    vm_state->registers[3] = arg3;
    vm_state->register_types[0] = type0;
    vm_state->register_types[1] = type1;
    vm_state->register_types[2] = type2;
    vm_state->register_types[3] = type3;

    vm_state->pc = func_offset;
    DISPATCH();
}

