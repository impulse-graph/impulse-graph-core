// Opcode 0x52: OP_VEC_REDUCE
V11_OP_CASE(VEC_REDUCE) {
    const uint8_t subop = IMP_V11_SUBOP(w);
    const uint8_t flags = IMP_V11_FLAGS(w);
    const uint8_t dst = IMP_V11_DST(w);
    const uint8_t s1 = IMP_V11_S1(w);
    const uint8_t s3 = IMP_V11_S3(w);

    const V11Frontier* mask = p4_get_mask_frontier(ws, flags, s3);
    if (ws.status != IMPULSE_VM_V11_OK) V11_DISPATCH();
    const V11Vector* v = ws.v_regs[s1];
    if (!v) { ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; V11_DISPATCH(); }
    const uint32_t n = v->length;

    switch (subop) {
        case subop::kVEC_REDUCE_COUNT_VALID: {
            uint64_t cnt = 0ULL;
            for (uint32_t i = 0; i < n; ++i) {
                if (p4_lane_active(mask, i) && v->is_valid_at(i)) cnt++;
            }
            ws.write_r(dst, static_cast<uint8_t>(ScalarType::SCALAR_U64), cnt);
            ws.set_zf(cnt == 0ULL);
            ws.set_nf(false);
            ws.state.pc += 1u;
            break;
        }
        case subop::kVEC_REDUCE_ANY_TRUE: {
            if (v->elem_tag != kVecTagBool3vl) { ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; V11_DISPATCH(); }
            bool has_true = false, has_null = false;
            for (uint32_t i = 0; i < n; ++i) {
                if (!p4_lane_active(mask, i)) continue;
                if (!v->is_valid_at(i)) has_null = true;
                else if (v->bool_data_at(i)) { has_true = true; break; }
            }
            if (has_true) { ws.write_r(dst, static_cast<uint8_t>(ScalarType::SCALAR_BOOL_3VL), 1ULL); ws.set_zf(false); ws.set_nf(false); }
            else if (has_null) { ws.write_r_null(dst); ws.set_zf(true); ws.set_nf(true); }
            else { ws.write_r(dst, static_cast<uint8_t>(ScalarType::SCALAR_BOOL_3VL), 0ULL); ws.set_zf(true); ws.set_nf(false); }
            ws.state.pc += 1u;
            break;
        }
        case subop::kVEC_REDUCE_ALL_TRUE: {
            if (v->elem_tag != kVecTagBool3vl) { ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; V11_DISPATCH(); }
            bool has_false = false, has_null = false;
            for (uint32_t i = 0; i < n; ++i) {
                if (!p4_lane_active(mask, i)) continue;
                if (!v->is_valid_at(i)) has_null = true;
                else if (!v->bool_data_at(i)) { has_false = true; break; }
            }
            if (has_false) { ws.write_r(dst, static_cast<uint8_t>(ScalarType::SCALAR_BOOL_3VL), 0ULL); ws.set_zf(true); ws.set_nf(false); }
            else if (has_null) { ws.write_r_null(dst); ws.set_zf(true); ws.set_nf(true); }
            else { ws.write_r(dst, static_cast<uint8_t>(ScalarType::SCALAR_BOOL_3VL), 1ULL); ws.set_zf(false); ws.set_nf(false); }
            ws.state.pc += 1u;
            break;
        }
        default: {
            if (!p4_is_numeric_vec(v)) { ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; V11_DISPATCH(); }
            uint64_t cnt = 0ULL;
            double sum = 0.0, l1 = 0.0, l2sq = 0.0;
            bool has_mn = false, has_mx = false;
            double mn = 0.0, mx = 0.0;
            int64_t argmn = -1, argmx = -1;
            for (uint32_t i = 0; i < n; ++i) {
                if (!p4_lane_active(mask, i) || !v->is_valid_at(i)) continue;
                double x = p4_vec_to_f64(*v, i);
                if (std::isnan(x)) continue;
                cnt++;
                sum += x;
                l1 += std::fabs(x);
                l2sq += x * x;
                if (!has_mn) { has_mn = true; mn = x; argmn = i; }
                else if (x < mn) { mn = x; argmn = i; }
                else if (x == 0.0 && mn == 0.0) { mn = p1_ieee_min_f64(mn, x); }
                if (!has_mx) { has_mx = true; mx = x; argmx = i; }
                else if (x > mx) { mx = x; argmx = i; }
                else if (x == 0.0 && mx == 0.0) { mx = p1_ieee_max_f64(mx, x); }
            }
            switch (subop) {
                case subop::kVEC_REDUCE_SUM:        p4_write_f64_scalar(ws, dst, true, cnt == 0ULL ? 0.0 : sum); break;
                case subop::kVEC_REDUCE_MIN:        p4_write_f64_scalar(ws, dst, has_mn, mn); break;
                case subop::kVEC_REDUCE_MAX:        p4_write_f64_scalar(ws, dst, has_mx, mx); break;
                case subop::kVEC_REDUCE_MEAN:       p4_write_f64_scalar(ws, dst, cnt > 0ULL, cnt > 0ULL ? (sum / static_cast<double>(cnt)) : std::numeric_limits<double>::quiet_NaN()); break;
                case subop::kVEC_REDUCE_L1_NORM:    p4_write_f64_scalar(ws, dst, true, cnt == 0ULL ? 0.0 : l1); break;
                case subop::kVEC_REDUCE_L2_NORM_SQ: p4_write_f64_scalar(ws, dst, true, cnt == 0ULL ? 0.0 : l2sq); break;
                case subop::kVEC_REDUCE_ARGMIN:
                    if (argmn < 0) { ws.write_r_null(dst); ws.set_zf(true); ws.set_nf(true); }
                    else { ws.write_r(dst, static_cast<uint8_t>(ScalarType::SCALAR_U64), static_cast<uint64_t>(argmn)); ws.set_zf(argmn == 0); ws.set_nf(false); }
                    break;
                case subop::kVEC_REDUCE_ARGMAX:
                    if (argmx < 0) { ws.write_r_null(dst); ws.set_zf(true); ws.set_nf(true); }
                    else { ws.write_r(dst, static_cast<uint8_t>(ScalarType::SCALAR_U64), static_cast<uint64_t>(argmx)); ws.set_zf(argmx == 0); ws.set_nf(false); }
                    break;
                default:
                    ws.status = IMPULSE_VM_V11_ERR_INVALID_OPCODE;
                    V11_DISPATCH();
            }
            ws.state.pc += 1u;
            break;
        }
    }
    V11_DISPATCH();
}
