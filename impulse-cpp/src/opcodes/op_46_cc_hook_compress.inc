// Opcode 0x46: OP_CC_HOOK_COMPRESS
op_CC_HOOK_COMPRESS: {
    const auto& inst = bytecode[vm_state->pc];
    VALIDATE_REG(inst.dst_reg);
    vm_state->pc++;
    DISPATCH();
}
