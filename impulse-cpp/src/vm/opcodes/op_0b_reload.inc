// Opcode 0x0B: OP_RELOAD
VM_OP_CASE(RELOAD) {
    const uint8_t subop = IMP_SUBOP(w);
    const uint8_t dst = IMP_DST(w);
    const uint16_t slot = IMP_IMM16_LO(w);
    if (slot >= kSpillPerFrame) {
        ws.status = IMPULSE_VM_ERR_STACK_OVERFLOW;
    } else {
        uint32_t idx = static_cast<uint32_t>(ws.state.call_depth) * kSpillPerFrame + slot;
        const VmSpillSlot& sp = ws.spill_slots[idx];
        if (subop == 0 && sp.kind == kSpillR) {
            if (!sp.r_valid) {
                ws.write_r_null(dst);
                p1_set_scalar_flags(ws, false, 0ULL);
            } else {
                ws.write_r(dst, sp.r_type, sp.r_val);
                if (sp.r_type == static_cast<uint8_t>(ScalarType::SCALAR_F64)) {
                    p1_set_f64_flags(ws, true, p1_bits_to_f64(sp.r_val));
                } else {
                    p1_set_scalar_flags(ws, true, sp.r_val);
                }
            }
            ws.state.pc += 1u;
        } else if (subop == 1 && sp.kind == kSpillF) {
            if (!ws.f_regs[dst].copy_from(ws.arena, sp.f_val)) {
                ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW;
            } else {
                ws.set_f_init(dst, true);
                ws.set_zf(ws.f_regs[dst].is_empty());
                ws.state.pc += 1u;
            }
        } else if (subop == 2 && sp.kind == kSpillV) {
            ws.v_regs[dst] = sp.v_val;
            ws.state.pc += 1u;
        } else {
            ws.status = IMPULSE_VM_ERR_TYPE_MISMATCH;
        }
    }
    VM_DISPATCH();
}
