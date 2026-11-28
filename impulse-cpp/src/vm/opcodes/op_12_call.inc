// Opcode 0x12: OP_CALL
VM_OP_CASE(CALL) {
    const uint8_t subop = IMP_SUBOP(w);
    const uint8_t s1 = IMP_S1(w);
    const uint8_t s2 = IMP_S2(w);
    const uint8_t s3 = IMP_S3(w);
    const int32_t imm32_s = IMP_IMM32_S(w);

    if (ws.state.call_depth >= ws.max_call_depth || ws.state.call_depth >= kMaxCallDepth) {
        ws.status = IMPULSE_VM_ERR_STACK_OVERFLOW;
        VM_DISPATCH();
    }
    uint8_t rs = (subop == 1u) ? 16u : s1;
    uint8_t fs = (subop == 1u) ? 16u : s2;
    uint8_t vs = (subop == 1u) ? 16u : s3;
    uint16_t d = ws.state.call_depth;
    VmCallFrame& frame = ws.call_frames[d];

    frame.return_pc = ws.state.pc + 1u;
    frame.r_stride  = rs;
    frame.f_stride  = fs;
    frame.v_stride  = vs;

    for (uint8_t i = 0; i < rs; ++i) frame.saved_r_val[i] = ws.read_r_raw(i);
    frame.saved_r_valid = ws.state.r_valid_mask;
    frame.saved_r_type  = ws.state.r_type_word;
    frame.saved_f_valid = ws.f_valid_mask;
    for (uint8_t i = 0; i < fs; ++i) {
        if (ws.is_f_init(i)) {
            if (!frame.saved_f[i].copy_from(ws.arena, ws.f_regs[i])) {
                ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW;
                VM_DISPATCH();
            }
        } else {
            frame.saved_f[i].reset_empty(0u, 0u);
        }
    }
    for (uint8_t i = 0; i < vs; ++i) frame.saved_v[i] = ws.v_regs[i];

    if (rs > 0u) {
        for (uint8_t i = 0; i < 16u; ++i) {
            if (static_cast<uint32_t>(i) + rs < 16u) {
                uint8_t src = static_cast<uint8_t>(i + rs);
                if (ws.is_r_valid(src)) ws.write_r(i, ws.read_r_type(src), ws.read_r_raw(src));
                else ws.write_r_null(i);
            } else {
                ws.write_r_null(i);
            }
        }
    }
    if (fs > 0u) {
        for (uint8_t i = 0; i < 16u; ++i) {
            if (static_cast<uint32_t>(i) + fs < 16u) {
                uint8_t src = static_cast<uint8_t>(i + fs);
                if (ws.is_f_init(src)) {
                    if (!ws.f_regs[i].copy_from(ws.arena, ws.f_regs[src])) {
                        ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW;
                        VM_DISPATCH();
                    }
                    ws.set_f_init(i, true);
                } else {
                    ws.set_f_init(i, false);
                }
            } else {
                ws.set_f_init(i, false);
            }
        }
    }
    if (vs > 0u) {
        for (uint8_t i = 0; i < 16u; ++i) {
            ws.v_regs[i] = (static_cast<uint32_t>(i) + vs < 16u) ? ws.v_regs[i + vs] : nullptr;
        }
    }

    ws.state.call_depth = static_cast<uint16_t>(d + 1u);
    uint32_t spill_base = static_cast<uint32_t>(ws.state.call_depth) * kSpillPerFrame;
    for (uint32_t k = 0; k < kSpillPerFrame; ++k) ws.spill_slots[spill_base + k].kind = kSpillEmpty;
    ws.state.pc = static_cast<uint32_t>(static_cast<int64_t>(ws.state.pc) + imm32_s);
    VM_DISPATCH();
}
