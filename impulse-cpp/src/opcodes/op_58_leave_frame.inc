// Opcode 0x58: OP_LEAVE_FRAME
op_LEAVE_FRAME:
    vm_state->pc++;
    DISPATCH();

