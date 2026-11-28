// Opcode 0x03: OP_THROW
VM_OP_CASE(THROW) {
    const uint8_t subop = IMP_SUBOP(w);
    const uint8_t s1 = IMP_S1(w);
    ws.user_throw_code = IMP_IMM32(w);
    if (subop == 0u) {
        ws.has_user_throw_payload = false;
        ws.user_throw_payload = 0ULL;
    } else if (subop == 1u) {
        if (ws.is_r_valid(s1)) {
            ws.has_user_throw_payload = true;
            ws.user_throw_payload = ws.read_r_raw(s1);
        } else {
            ws.has_user_throw_payload = false;
            ws.user_throw_payload = 0ULL;
        }
    }
    ws.status = IMPULSE_VM_ERR_USER_THROW;
    VM_DISPATCH();
}
