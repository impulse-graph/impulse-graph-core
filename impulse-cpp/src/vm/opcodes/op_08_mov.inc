// Opcode 0x08: OP_MOV
VM_OP_CASE(MOV) {
    const uint8_t subop = IMP_SUBOP(w);
    const uint8_t dst = IMP_DST(w);
    const uint8_t s1 = IMP_S1(w);
    const uint16_t imm_lo = IMP_IMM16_LO(w);
    switch (subop) {
        case 0: { // .r
            if (!ws.is_r_valid(s1)) {
                ws.write_r_null(dst);
                p1_set_scalar_flags(ws, false, 0ULL);
            } else {
                uint8_t ty = ws.read_r_type(s1);
                uint64_t v = ws.read_r_raw(s1);
                ws.write_r(dst, ty, v);
                if (ty == static_cast<uint8_t>(ScalarType::SCALAR_F64)) {
                    p1_set_f64_flags(ws, true, p1_bits_to_f64(v));
                } else {
                    p1_set_scalar_flags(ws, true, v);
                }
            }
            ws.state.pc += 1u;
            break;
        }
        case 1: { // .f
            if (!ws.is_f_init(s1)) {
                ws.status = IMPULSE_VM_ERR_TYPE_MISMATCH;
            } else {
                if (dst != s1) {
                    if (!ws.f_regs[dst].copy_from(ws.arena, ws.f_regs[s1])) {
                        ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW;
                        break;
                    }
                    ws.set_f_init(dst, true);
                }
                ws.set_zf(ws.f_regs[dst].is_empty());
                ws.state.pc += 1u;
            }
            break;
        }
        case 2: { // .v
            if (ws.v_regs[s1] == nullptr) {
                ws.status = IMPULSE_VM_ERR_TYPE_MISMATCH;
            } else {
                if (dst != s1) ws.v_regs[dst] = ws.v_regs[s1];
                ws.state.pc += 1u;
            }
            break;
        }
        case 3: { // .clear_r
            ws.write_r(dst, static_cast<uint8_t>(ScalarType::SCALAR_I64), 0ULL);
            p1_set_scalar_flags(ws, true, 0ULL);
            ws.state.pc += 1u;
            break;
        }
        case 4: { // .clear_f
            uint64_t dom_card = 0;
            impulse_vm_status_t st = vm_lookup_domain_card(snap, imm_lo, &dom_card);
            if (st != IMPULSE_VM_OK) {
                ws.status = st;
            } else {
                ws.f_regs[dst].reset_empty(imm_lo, dom_card);
                ws.set_f_init(dst, true);
                ws.set_zf(true);
                ws.state.pc += 1u;
            }
            break;
        }
        case 5: { // .clear_v
            ws.v_regs[dst] = nullptr;
            ws.state.pc += 1u;
            break;
        }
        default:
            ws.status = IMPULSE_VM_ERR_INVALID_OPCODE;
            break;
    }
    VM_DISPATCH();
}
