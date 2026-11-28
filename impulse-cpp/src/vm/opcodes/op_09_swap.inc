// Opcode 0x09: OP_SWAP
VM_OP_CASE(SWAP) {
    const uint8_t subop = IMP_SUBOP(w);
    const uint8_t dst = IMP_DST(w);
    const uint8_t s1 = IMP_S1(w);
    switch (subop) {
        case 0: { // .r
            bool vd = ws.is_r_valid(dst);
            uint64_t val_d = ws.read_r_raw(dst);
            uint8_t ty_d = ws.read_r_type(dst);
            bool vs = ws.is_r_valid(s1);
            uint64_t val_s = ws.read_r_raw(s1);
            uint8_t ty_s = ws.read_r_type(s1);
            if (vs) ws.write_r(dst, ty_s, val_s); else ws.write_r_null(dst);
            if (vd) ws.write_r(s1, ty_d, val_d); else ws.write_r_null(s1);
            ws.state.pc += 1u;
            break;
        }
        case 1: { // .f
            bool init_d = ws.is_f_init(dst);
            bool init_s = ws.is_f_init(s1);
            VmFrontier tmp = ws.f_regs[dst];
            ws.f_regs[dst] = ws.f_regs[s1];
            ws.f_regs[s1] = tmp;
            ws.set_f_init(dst, init_s);
            ws.set_f_init(s1, init_d);
            ws.state.pc += 1u;
            break;
        }
        case 2: { // .v
            VmVector* tmp = ws.v_regs[dst];
            ws.v_regs[dst] = ws.v_regs[s1];
            ws.v_regs[s1] = tmp;
            ws.state.pc += 1u;
            break;
        }
        default:
            ws.status = IMPULSE_VM_ERR_INVALID_OPCODE;
            break;
    }
    VM_DISPATCH();
}
