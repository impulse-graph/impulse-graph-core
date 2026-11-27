// Opcode 0x15: OP_SCALAR_DECIMAL_ALU
V11_OP_CASE(SCALAR_DECIMAL_ALU) {
    V11_OP_CONTRACT_ASSERT(OP_SCALAR_DECIMAL_ALU, 0x0u, 0xFFFFu);
    const uint8_t subop = IMP_V11_SUBOP(w);
    const uint8_t dst = IMP_V11_DST(w);
    const uint8_t s1 = IMP_V11_S1(w);
    const uint8_t s2 = IMP_V11_S2(w);
    const uint8_t s3 = IMP_V11_S3(w);
    const uint16_t lo = IMP_V11_IMM16_LO(w);
    const uint16_t hi = IMP_V11_IMM16_HI(w);

    const bool a_valid = ws.is_r_valid(s1);
    const bool b_valid = ws.is_r_valid(s2);
    const bool c_valid = ws.is_r_valid(s3);
    const int64_t a = a_valid ? static_cast<int64_t>(ws.read_r_raw(s1)) : 0LL;
    const int64_t b = b_valid ? static_cast<int64_t>(ws.read_r_raw(s2)) : 0LL;
    const int64_t c = c_valid ? static_cast<int64_t>(ws.read_r_raw(s3)) : 0LL;

    auto finish_i64 = [&](impulse_vm_status_v11_t st, bool valid, int64_t val) noexcept {
        if (st != IMPULSE_VM_V11_OK) {
            ws.status = st;
            return;
        }
        if (!valid) {
            ws.write_r_null(dst);
            p1_set_scalar_flags(ws, false, 0ULL);
        } else {
            ws.write_r(dst, static_cast<uint8_t>(ScalarType::SCALAR_I64), static_cast<uint64_t>(val));
            p1_set_scalar_flags(ws, true, static_cast<uint64_t>(val));
        }
        ws.state.pc += 1u;
    };

    switch (subop) {
        case subop::kSCALAR_DECIMAL_ALU_ADD_CHK: {
            if (!a_valid || !b_valid) { finish_i64(IMPULSE_VM_V11_OK, false, 0LL); break; }
            int64_t res = 0;
            impulse_vm_status_v11_t st = dec_add_chk(a, b, &res);
            finish_i64(st, true, res);
            break;
        }
        case subop::kSCALAR_DECIMAL_ALU_SUB_CHK: {
            if (!a_valid || !b_valid) { finish_i64(IMPULSE_VM_V11_OK, false, 0LL); break; }
            int64_t res = 0;
            impulse_vm_status_v11_t st = dec_sub_chk(a, b, &res);
            finish_i64(st, true, res);
            break;
        }
        case subop::kSCALAR_DECIMAL_ALU_MUL_CHK: {
            if (!a_valid || !b_valid) { finish_i64(IMPULSE_VM_V11_OK, false, 0LL); break; }
            int64_t res = 0;
            impulse_vm_status_v11_t st = dec_mul_chk(a, b, &res);
            finish_i64(st, true, res);
            break;
        }
        case subop::kSCALAR_DECIMAL_ALU_MUL_DIV_ROUND: {
            if (!a_valid || !b_valid || !c_valid) { finish_i64(IMPULSE_VM_V11_OK, false, 0LL); break; }
            int64_t res = 0;
            impulse_vm_status_v11_t st = dec_mul_div_round(a, b, c, &res);
            finish_i64(st, true, res);
            break;
        }
        case subop::kSCALAR_DECIMAL_ALU_DIV_ROUND: {
            if (!a_valid || !b_valid) { finish_i64(IMPULSE_VM_V11_OK, false, 0LL); break; }
            int64_t res = 0;
            impulse_vm_status_v11_t st = dec_div_round(a, b, &res);
            finish_i64(st, true, res);
            break;
        }
        case subop::kSCALAR_DECIMAL_ALU_RESCALE: {
            if (lo > 18u || (hi != 0u && hi != 1u)) { ws.status = IMPULSE_VM_V11_ERR_INVALID_OPERAND; break; }
            if (!a_valid) { finish_i64(IMPULSE_VM_V11_OK, false, 0LL); break; }
            int64_t res = 0;
            impulse_vm_status_v11_t st = dec_rescale(a, lo, hi, &res);
            finish_i64(st, true, res);
            break;
        }
        case subop::kSCALAR_DECIMAL_ALU_BOUND_CHK: {
            if (lo < 1u || lo > 18u) { ws.status = IMPULSE_VM_V11_ERR_INVALID_OPERAND; break; }
            if (!a_valid) { finish_i64(IMPULSE_VM_V11_OK, false, 0LL); break; }
            int64_t res = 0;
            impulse_vm_status_v11_t st = dec_bound_chk(a, lo, &res);
            finish_i64(st, true, res);
            break;
        }
        case subop::kSCALAR_DECIMAL_ALU_ABS_CHK: {
            if (!a_valid) { finish_i64(IMPULSE_VM_V11_OK, false, 0LL); break; }
            int64_t res = 0;
            impulse_vm_status_v11_t st = dec_abs_chk(a, &res);
            finish_i64(st, true, res);
            break;
        }
        case subop::kSCALAR_DECIMAL_ALU_NEG_CHK: {
            if (!a_valid) { finish_i64(IMPULSE_VM_V11_OK, false, 0LL); break; }
            int64_t res = 0;
            impulse_vm_status_v11_t st = dec_neg_chk(a, &res);
            finish_i64(st, true, res);
            break;
        }
        case subop::kSCALAR_DECIMAL_ALU_F64_TO_DEC: {
            if (lo > 18u || hi < 1u || hi > 18u) { ws.status = IMPULSE_VM_V11_ERR_INVALID_OPERAND; break; }
            if (!a_valid) { finish_i64(IMPULSE_VM_V11_OK, false, 0LL); break; }
            int64_t res = 0;
            bool out_valid = false;
            impulse_vm_status_v11_t st = dec_f64_to_dec(p1_bits_to_f64(ws.read_r_raw(s1)), lo, hi, &res, &out_valid);
            finish_i64(st, out_valid, res);
            break;
        }
        case subop::kSCALAR_DECIMAL_ALU_DEC_TO_F64: {
            if (lo > 18u) { ws.status = IMPULSE_VM_V11_ERR_INVALID_OPERAND; break; }
            if (!a_valid) {
                p1_write_f64_result(ws, dst, false, 0.0);
                ws.state.pc += 1u;
                break;
            }
            double f = 0.0;
            impulse_vm_status_v11_t st = dec_to_f64(a, lo, &f);
            if (st != IMPULSE_VM_V11_OK) { ws.status = st; break; }
            p1_write_f64_result(ws, dst, true, f);
            ws.state.pc += 1u;
            break;
        }
        case subop::kSCALAR_DECIMAL_ALU_ROUND_SCALE: {
            if (lo > 18u) { ws.status = IMPULSE_VM_V11_ERR_INVALID_OPERAND; break; }
            if (!a_valid) { finish_i64(IMPULSE_VM_V11_OK, false, 0LL); break; }
            int64_t res = 0;
            impulse_vm_status_v11_t st = dec_round_scale(a, lo, &res);
            finish_i64(st, true, res);
            break;
        }
        case subop::kSCALAR_DECIMAL_ALU_TRUNC_SCALE: {
            if (lo > 18u) { ws.status = IMPULSE_VM_V11_ERR_INVALID_OPERAND; break; }
            if (!a_valid) { finish_i64(IMPULSE_VM_V11_OK, false, 0LL); break; }
            int64_t res = 0;
            impulse_vm_status_v11_t st = dec_trunc_scale(a, lo, &res);
            finish_i64(st, true, res);
            break;
        }
        case subop::kSCALAR_DECIMAL_ALU_FLOOR_SCALE: {
            if (lo > 18u) { ws.status = IMPULSE_VM_V11_ERR_INVALID_OPERAND; break; }
            if (!a_valid) { finish_i64(IMPULSE_VM_V11_OK, false, 0LL); break; }
            int64_t res = 0;
            impulse_vm_status_v11_t st = dec_floor_scale(a, lo, &res);
            finish_i64(st, true, res);
            break;
        }
        case subop::kSCALAR_DECIMAL_ALU_CEIL_SCALE: {
            if (lo > 18u) { ws.status = IMPULSE_VM_V11_ERR_INVALID_OPERAND; break; }
            if (!a_valid) { finish_i64(IMPULSE_VM_V11_OK, false, 0LL); break; }
            int64_t res = 0;
            impulse_vm_status_v11_t st = dec_ceil_scale(a, lo, &res);
            finish_i64(st, true, res);
            break;
        }
        case subop::kSCALAR_DECIMAL_ALU_DIV_SAFE: {
            if (!a_valid || !b_valid) { finish_i64(IMPULSE_VM_V11_OK, false, 0LL); break; }
            int64_t res = 0;
            bool out_valid = false;
            impulse_vm_status_v11_t st = dec_div_safe(a, b, &res, &out_valid);
            finish_i64(st, out_valid, res);
            break;
        }
        default:
            ws.status = IMPULSE_VM_V11_ERR_INVALID_OPCODE;
            break;
    }
    V11_DISPATCH();
}
