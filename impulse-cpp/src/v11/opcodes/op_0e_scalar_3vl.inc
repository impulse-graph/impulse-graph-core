// Opcode 0x0E: OP_SCALAR_3VL
V11_OP_CASE(SCALAR_3VL) {
    const uint8_t subop = IMP_V11_SUBOP(w);
    const uint8_t dst = IMP_V11_DST(w);
    const uint8_t s1 = IMP_V11_S1(w);
    const uint8_t s2 = IMP_V11_S2(w);

    bool a_valid = ws.is_r_valid(s1);
    uint64_t a_u = ws.read_r_raw(s1);
    int64_t a_s  = static_cast<int64_t>(a_u);
    bool b_valid = ws.is_r_valid(s2);
    uint64_t b_u = ws.read_r_raw(s2);
    int64_t b_s  = static_cast<int64_t>(b_u);

    if (subop == 15u) { // .coalesce
        if (a_valid) {
            uint8_t ty = ws.read_r_type(s1);
            ws.write_r(dst, ty, a_u);
            if (ty == static_cast<uint8_t>(ScalarType::SCALAR_F64)) p1_set_f64_flags(ws, true, p1_bits_to_f64(a_u));
            else p1_set_scalar_flags(ws, true, a_u);
        } else if (b_valid) {
            uint8_t ty = ws.read_r_type(s2);
            ws.write_r(dst, ty, b_u);
            if (ty == static_cast<uint8_t>(ScalarType::SCALAR_F64)) p1_set_f64_flags(ws, true, p1_bits_to_f64(b_u));
            else p1_set_scalar_flags(ws, true, b_u);
        } else {
            ws.write_r_null(dst);
            p1_set_scalar_flags(ws, false, 0ULL);
        }
        ws.state.pc += 1u;
        V11_DISPATCH();
    }

    int k_res = 0;
    switch (subop) {
        case 0: k_res = (a_valid && b_valid) ? (a_u == b_u ? 2 : 1) : 0; break;
        case 1: k_res = (a_valid && b_valid) ? (a_u != b_u ? 2 : 1) : 0; break;
        case 2: k_res = (a_valid && b_valid) ? (a_s <  b_s ? 2 : 1) : 0; break;
        case 3: k_res = (a_valid && b_valid) ? (a_s <= b_s ? 2 : 1) : 0; break;
        case 4: k_res = (a_valid && b_valid) ? (a_u <  b_u ? 2 : 1) : 0; break;
        case 5: k_res = (a_valid && b_valid) ? (a_u <= b_u ? 2 : 1) : 0; break;
        case 6: case 7: case 8: case 9:
            if (!a_valid || !b_valid) {
                k_res = 0;
            } else {
                double af = p1_bits_to_f64(a_u);
                double bf = p1_bits_to_f64(b_u);
                if (std::isnan(af) || std::isnan(bf)) {
                    k_res = 0;
                } else {
                    bool p = (subop == 6) ? (af == bf) : (subop == 7) ? (af != bf) : (subop == 8) ? (af < bf) : (af <= bf);
                    k_res = p ? 2 : 1;
                }
            }
            break;
        case 10: {
            bool a_false = a_valid && (a_u == 0ULL);
            bool b_false = b_valid && (b_u == 0ULL);
            if (a_false || b_false) k_res = 1;
            else if (a_valid && b_valid) k_res = 2;
            else k_res = 0;
            break;
        }
        case 11: {
            bool a_true = a_valid && (a_u != 0ULL);
            bool b_true = b_valid && (b_u != 0ULL);
            if (a_true || b_true) k_res = 2;
            else if (a_valid && b_valid) k_res = 1;
            else k_res = 0;
            break;
        }
        case 12:
            if (!a_valid) k_res = 0;
            else k_res = (a_u == 0ULL) ? 2 : 1;
            break;
        case 13: k_res = !a_valid ? 2 : 1; break;
        case 14: k_res =  a_valid ? 2 : 1; break;
        default: k_res = 0; break;
    }

    if (k_res == 0) {
        ws.write_r_null(dst);
        p1_set_scalar_flags(ws, false, 0ULL);
    } else {
        uint64_t out = (k_res == 2) ? 1ULL : 0ULL;
        ws.write_r(dst, static_cast<uint8_t>(ScalarType::SCALAR_BOOL_3VL), out);
        p1_set_scalar_flags(ws, true, out);
    }
    ws.state.pc += 1u;
    V11_DISPATCH();
}
