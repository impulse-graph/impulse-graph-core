// Opcode 0x37: OP_MORSEL_COMMIT
VM_OP_CASE(MORSEL_COMMIT) {
    // Only valid inside a lexical OP_WALK_MORSEL / OP_FILTER_MORSEL block.
    ws.status = IMPULSE_VM_ERR_VALIDATION_FAILED;
    VM_DISPATCH();
}
