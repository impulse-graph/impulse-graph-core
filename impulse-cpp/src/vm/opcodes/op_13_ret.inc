// Opcode 0x13: OP_RET
VM_OP_CASE(RET) {
    if (ws.state.call_depth == 0u) {
        ws.status = IMPULSE_VM_ERR_STACK_UNDERFLOW;
        VM_DISPATCH();
    }
    uint32_t callee_spill_base = static_cast<uint32_t>(ws.state.call_depth) * kSpillPerFrame;
    for (uint32_t k = 0; k < kSpillPerFrame; ++k) ws.spill_slots[callee_spill_base + k].kind = kSpillEmpty;

    uint16_t d = --ws.state.call_depth;
    const VmCallFrame& frame = ws.call_frames[d];
    uint8_t rs = frame.r_stride;
    uint8_t fs = frame.f_stride;
    uint8_t vs = frame.v_stride;
    uint16_t saved_rvm = frame.saved_r_valid;
    uint64_t saved_rtw = frame.saved_r_type;
    uint16_t saved_fvm = frame.saved_f_valid;

    if (rs > 0u) {
        for (int ii = 15; ii >= 0; --ii) {
            uint8_t i = static_cast<uint8_t>(ii);
            if (i >= rs) {
                uint8_t src = static_cast<uint8_t>(i - rs);
                if (ws.is_r_valid(src)) ws.write_r(i, ws.read_r_type(src), ws.read_r_raw(src));
                else ws.write_r_null(i);
            } else if (((saved_rvm >> i) & 1u) != 0u) {
                uint8_t ty = static_cast<uint8_t>((saved_rtw >> (i * 4u)) & 0xFULL);
                ws.write_r(i, ty, frame.saved_r_val[i]);
            } else {
                ws.write_r_null(i);
            }
        }
    }
    if (fs > 0u) {
        for (int ii = 15; ii >= 0; --ii) {
            uint8_t i = static_cast<uint8_t>(ii);
            if (i >= fs) {
                uint8_t src = static_cast<uint8_t>(i - fs);
                if (ws.is_f_init(src)) {
                    if (!ws.f_regs[i].copy_from(ws.arena, ws.f_regs[src])) {
                        ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW;
                        VM_DISPATCH();
                    }
                    ws.set_f_init(i, true);
                } else {
                    ws.set_f_init(i, false);
                }
            } else if (((saved_fvm >> i) & 1u) != 0u) {
                if (!ws.f_regs[i].copy_from(ws.arena, frame.saved_f[i])) {
                    ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW;
                    VM_DISPATCH();
                }
                ws.set_f_init(i, true);
            } else {
                ws.set_f_init(i, false);
            }
        }
    }
    if (vs > 0u) {
        for (int ii = 15; ii >= 0; --ii) {
            uint8_t i = static_cast<uint8_t>(ii);
            ws.v_regs[i] = (i >= vs) ? ws.v_regs[i - vs] : frame.saved_v[i];
        }
    }
    ws.state.pc = frame.return_pc;
    VM_DISPATCH();
}
