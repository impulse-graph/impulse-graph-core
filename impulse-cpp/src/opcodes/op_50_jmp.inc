// Opcode 0x50: OP_JMP
op_JMP: {
    const auto& inst = bytecode[vm_state->pc];
    int32_t offset = static_cast<int32_t>(inst.payload);
    vm_state->pc += offset;
    DISPATCH();
}

