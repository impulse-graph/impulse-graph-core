// Opcode 0x38: OP_MORSEL_END
VM_OP_CASE(MORSEL_END) {
    // Only valid as the closing delimiter of a lexical morsel block.
    ws.status = IMPULSE_VM_ERR_VALIDATION_FAILED;
    VM_DISPATCH();
}
