// Opcode 0x57: OP_ENTER_FRAME
op_ENTER_FRAME:
    vm_state->pc++;
    DISPATCH();

