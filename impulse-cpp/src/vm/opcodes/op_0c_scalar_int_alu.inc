// Opcode 0x0C: OP_SCALAR_INT_ALU
VM_OP_CASE(SCALAR_INT_ALU) {
    const uint8_t subop = IMP_SUBOP(w);
    const uint8_t dst = IMP_DST(w);
    const uint8_t s1 = IMP_S1(w);
    const uint8_t s2 = IMP_S2(w);
    const uint8_t s3 = IMP_S3(w);
    const int32_t imm32_s = IMP_IMM32_S(w);

    bool a_valid = ws.is_r_valid(s1);
    uint64_t a_u = ws.read_r_raw(s1);
    int64_t a_s = static_cast<int64_t>(a_u);

    if (subop == 10u) { // .not (unary)
        if (!a_valid) {
            ws.write_r_null(dst);
            p1_set_scalar_flags(ws, false, 0ULL);
        } else {
            uint64_t r = ~a_u;
            ws.write_r(dst, static_cast<uint8_t>(ScalarType::SCALAR_I64), r);
            p1_set_scalar_flags(ws, true, r);
        }
        ws.state.pc += 1u;
        VM_DISPATCH();
    }

    bool b_valid = (s3 == 1u) || ws.is_r_valid(s2);
    int64_t b_s = (s3 == 1u) ? static_cast<int64_t>(imm32_s) : static_cast<int64_t>(ws.read_r_raw(s2));
    uint64_t b_u = static_cast<uint64_t>(b_s);

    if (!a_valid || !b_valid) {
        ws.write_r_null(dst);
        p1_set_scalar_flags(ws, false, 0ULL);
        ws.state.pc += 1u;
        VM_DISPATCH();
    }

    uint8_t out_ty = (subop == 4u || subop == 6u || subop == 12u)
        ? static_cast<uint8_t>(ScalarType::SCALAR_U64)
        : static_cast<uint8_t>(ScalarType::SCALAR_I64);
    bool res_valid = true;
    uint64_t r_u = 0ULL;

    switch (subop) {
        case 0: r_u = a_u + b_u; break;
        case 1: r_u = a_u - b_u; break;
        case 2: r_u = a_u * b_u; break;
        case 3:
            if (b_s == 0) res_valid = false;
            else if (a_s == (std::numeric_limits<int64_t>::min)() && b_s == -1)
                r_u = static_cast<uint64_t>((std::numeric_limits<int64_t>::min)());
            else r_u = static_cast<uint64_t>(a_s / b_s);
            break;
        case 4:
            if (b_u == 0ULL) res_valid = false;
            else r_u = a_u / b_u;
            break;
        case 5:
            if (b_s == 0) res_valid = false;
            else if (a_s == (std::numeric_limits<int64_t>::min)() && b_s == -1) r_u = 0ULL;
            else r_u = static_cast<uint64_t>(a_s % b_s);
            break;
        case 6:
            if (b_u == 0ULL) res_valid = false;
            else r_u = a_u % b_u;
            break;
        case 7:  r_u = a_u & b_u; break;
        case 8:  r_u = a_u | b_u; break;
        case 9:  r_u = a_u ^ b_u; break;
        case 11: r_u = a_u << (b_u & 63ULL); break;
        case 12: r_u = a_u >> (b_u & 63ULL); break;
        case 13: r_u = static_cast<uint64_t>(a_s >> static_cast<unsigned>(b_u & 63ULL)); break;
        case 14: r_u = static_cast<uint64_t>((a_s < b_s) ? a_s : b_s); break;
        case 15: r_u = static_cast<uint64_t>((a_s > b_s) ? a_s : b_s); break;
        default: res_valid = false; break;
    }

    if (!res_valid) {
        ws.write_r_null(dst);
        p1_set_scalar_flags(ws, false, 0ULL);
    } else {
        ws.write_r(dst, out_ty, r_u);
        p1_set_scalar_flags(ws, true, r_u);
    }
    ws.state.pc += 1u;
    VM_DISPATCH();
}
