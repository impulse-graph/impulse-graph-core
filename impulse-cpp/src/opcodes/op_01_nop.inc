// Opcode 0x01: OP_NOP
op_NOP:
    vm_state->pc++;
    DISPATCH();

