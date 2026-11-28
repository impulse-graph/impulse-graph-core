// Opcode 0x06: OP_LOAD_CONST_POOL
VM_OP_CASE(LOAD_CONST_POOL) {
    const uint8_t subop = IMP_SUBOP(w);
    const uint8_t flags = IMP_FLAGS(w);
    const uint8_t dst = IMP_DST(w);
    const uint32_t imm32 = IMP_IMM32(w);
    const uint16_t imm_lo = IMP_IMM16_LO(w);
    const uint16_t imm_hi = IMP_IMM16_HI(w);
    switch (subop) {
        case 0: { // .vec
            const VmConstPoolEntry* e = ws.find_const_pool(imm32);
            if (!e) {
                ws.status = IMPULSE_VM_ERR_OUT_OF_BOUNDS;
            } else if (e->kind == ConstPoolKind::Vec) {
                ws.v_regs[dst] = e->vec;
                ws.state.pc += 1u;
            } else {
                ws.status = IMPULSE_VM_ERR_TYPE_MISMATCH;
            }
            break;
        }
        case 1: { // .frontier
            const VmConstPoolEntry* e = ws.find_const_pool(imm_lo);
            if (!e) {
                ws.status = IMPULSE_VM_ERR_OUT_OF_BOUNDS;
            } else if (e->kind == ConstPoolKind::Frontier) {
                p1_load_frontier_ids(ws, snap, dst, imm_hi, e->f_ids, e->f_len, (flags & FLAG_ACCUM) != 0u);
            } else {
                ws.status = IMPULSE_VM_ERR_TYPE_MISMATCH;
            }
            break;
        }
        case 2: { // .str
            const VmConstPoolEntry* e = ws.find_const_pool(imm32);
            if (!e) {
                ws.status = IMPULSE_VM_ERR_OUT_OF_BOUNDS;
            } else if (e->kind == ConstPoolKind::Str) {
                uint64_t h = ws.intern_scalar_str(e->str.ptr, e->str.len);
                if (h == 0ULL) {
                    ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW;
                    break;
                }
                ws.write_r(dst, static_cast<uint8_t>(ScalarType::SCALAR_STR_HANDLE), h);
                ws.set_zf(e->str.len == 0u);
                ws.set_nf(false);
                ws.state.pc += 1u;
            } else {
                ws.status = IMPULSE_VM_ERR_TYPE_MISMATCH;
            }
            break;
        }
        default:
            ws.status = IMPULSE_VM_ERR_INVALID_OPCODE;
            break;
    }
    VM_DISPATCH();
}
