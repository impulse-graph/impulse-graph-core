// Opcode 0x0D: OP_SCALAR_FP_ALU
V11_OP_CASE(SCALAR_FP_ALU) {
    const uint8_t subop = IMP_V11_SUBOP(w);
    const uint8_t dst = IMP_V11_DST(w);
    const uint8_t s1 = IMP_V11_S1(w);
    const uint8_t s2 = IMP_V11_S2(w);
    const uint8_t s3 = IMP_V11_S3(w);

    bool a_raw_valid = ws.is_r_valid(s1);
    double a = a_raw_valid ? p1_bits_to_f64(ws.read_r_raw(s1)) : std::numeric_limits<double>::quiet_NaN();
    bool a_valid = a_raw_valid && !std::isnan(a);

    bool b_raw_valid = ws.is_r_valid(s2);
    double b = b_raw_valid ? p1_bits_to_f64(ws.read_r_raw(s2)) : std::numeric_limits<double>::quiet_NaN();
    bool b_valid = b_raw_valid && !std::isnan(b);

    bool c_raw_valid = ws.is_r_valid(s3);
    double c = c_raw_valid ? p1_bits_to_f64(ws.read_r_raw(s3)) : std::numeric_limits<double>::quiet_NaN();
    bool c_valid = c_raw_valid && !std::isnan(c);

    switch (subop) {
        case 0: p1_write_f64_result(ws, dst, a_valid && b_valid, a + b); break;
        case 1: p1_write_f64_result(ws, dst, a_valid && b_valid, a - b); break;
        case 2: p1_write_f64_result(ws, dst, a_valid && b_valid, a * b); break;
        case 3:
            if (!a_valid || !b_valid || b == 0.0) p1_write_f64_result(ws, dst, false, 0.0);
            else p1_write_f64_result(ws, dst, true, a / b);
            break;
        case 4: p1_write_f64_result(ws, dst, a_valid, -a); break;
        case 5: p1_write_f64_result(ws, dst, a_valid, std::fabs(a)); break;
        case 6:
            if (!a_valid || a < 0.0) p1_write_f64_result(ws, dst, false, 0.0);
            else p1_write_f64_result(ws, dst, true, std::sqrt(a));
            break;
        case 7:  p1_write_f64_result(ws, dst, a_valid && b_valid, p1_ieee_min_f64(a, b)); break;
        case 8:  p1_write_f64_result(ws, dst, a_valid && b_valid, p1_ieee_max_f64(a, b)); break;
        case 9:  p1_write_f64_result(ws, dst, a_valid && b_valid && c_valid, std::fma(a, b, c)); break;
        case 10: p1_write_f64_result(ws, dst, a_valid, std::floor(a)); break;
        case 11: p1_write_f64_result(ws, dst, a_valid, std::ceil(a)); break;
        case 12: p1_write_f64_result(ws, dst, a_valid, p1_round_half_away(a)); break;
        case 13:
            if (!a_raw_valid) p1_write_f64_result(ws, dst, false, 0.0);
            else p1_write_f64_result(ws, dst, true, static_cast<double>(static_cast<int64_t>(ws.read_r_raw(s1))));
            break;
        case 14:
            if (!a_valid) {
                ws.write_r_null(dst);
                ws.set_zf(true);
                ws.set_nf(true);
            } else {
                int64_t i = 0;
                if (a >= 9223372036854775808.0) i = (std::numeric_limits<int64_t>::max)();
                else if (a <= -9223372036854775808.0) i = (std::numeric_limits<int64_t>::min)();
                else i = static_cast<int64_t>(a < 0.0 ? std::ceil(a) : std::floor(a));
                ws.write_r(dst, static_cast<uint8_t>(ScalarType::SCALAR_I64), static_cast<uint64_t>(i));
                ws.set_zf(i == 0);
                ws.set_nf(false);
            }
            break;
        case 15:
            if (!b_valid || b == 0.0) p1_write_f64_result(ws, dst, c_valid, c);
            else if (!a_valid) p1_write_f64_result(ws, dst, false, 0.0);
            else p1_write_f64_result(ws, dst, true, a / b);
            break;
        default:
            p1_write_f64_result(ws, dst, false, 0.0);
            break;
    }
    ws.state.pc += 1u;
    V11_DISPATCH();
}
