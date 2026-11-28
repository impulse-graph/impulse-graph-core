// Opcode 0x0A: OP_SPILL
VM_OP_CASE(SPILL) {
    const uint8_t subop = IMP_SUBOP(w);
    const uint8_t s1 = IMP_S1(w);
    const uint16_t slot = IMP_IMM16_LO(w);
    if (slot >= kSpillPerFrame) {
        ws.status = IMPULSE_VM_ERR_STACK_OVERFLOW;
    } else {
        uint32_t idx = static_cast<uint32_t>(ws.state.call_depth) * kSpillPerFrame + slot;
        VmSpillSlot& sp = ws.spill_slots[idx];
        switch (subop) {
            case 0:
                sp.kind = kSpillR;
                sp.r_val = ws.read_r_raw(s1);
                sp.r_valid = ws.is_r_valid(s1);
                sp.r_type = ws.read_r_type(s1);
                ws.state.pc += 1u;
                break;
            case 1:
                if (!ws.is_f_init(s1)) {
                    ws.status = IMPULSE_VM_ERR_TYPE_MISMATCH;
                } else {
                    sp.kind = kSpillF;
                    if (!sp.f_val.copy_from(ws.arena, ws.f_regs[s1])) {
                        ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW;
                        break;
                    }
                    ws.state.pc += 1u;
                }
                break;
            case 2:
                if (ws.v_regs[s1] == nullptr) {
                    ws.status = IMPULSE_VM_ERR_TYPE_MISMATCH;
                } else {
                    sp.kind = kSpillV;
                    sp.v_val = ws.v_regs[s1];
                    ws.state.pc += 1u;
                }
                break;
            default:
                ws.status = IMPULSE_VM_ERR_INVALID_OPCODE;
                break;
        }
    }
    VM_DISPATCH();
}
