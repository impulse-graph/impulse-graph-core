// Opcode 0x4A: OP_VEC_MATH_TRIG_HYP
V11_OP_CASE(VEC_MATH_TRIG_HYP) {
    const uint8_t subop = IMP_V11_SUBOP(w);
    const uint8_t flags = IMP_V11_FLAGS(w);
    const uint8_t dst = IMP_V11_DST(w);
    const uint8_t s1 = IMP_V11_S1(w);
    const uint8_t s2 = IMP_V11_S2(w);
    const uint8_t s3 = IMP_V11_S3(w);

    const V11Frontier* mask = p4_get_mask_frontier(ws, flags, s3);
    if (ws.status != IMPULSE_VM_V11_OK) V11_DISPATCH();
    const V11Vector* va = ws.v_regs[s1];
    if (!p4_is_numeric_vec(va)) { ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; V11_DISPATCH(); }

    const uint32_t n = va->length;
    const bool is_binary = (subop == 6u); // atan2
    const bool scalar_rhs = (flags & FLAG_SCALAR_RHS) != 0u;
    const V11Vector* vb = nullptr;
    if (is_binary && !scalar_rhs) {
        vb = ws.v_regs[s2];
        if (!p4_is_numeric_vec(vb)) { ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; V11_DISPATCH(); }
        if (vb->length != n) { ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; V11_DISPATCH(); }
    }
    const bool s_valid = is_binary && scalar_rhs && p4_is_scalar_f64_valid(ws, s2);
    const double s_val = s_valid ? p4_read_scalar_f64(ws, s2) : std::numeric_limits<double>::quiet_NaN();

    const size_t vwords = (n > 0u) ? ((static_cast<size_t>(n) + 63u) >> 6) : 1u;
    double* out = (n > 0u) ? ws.arena.alloc_array<double>(n, 8) : nullptr;
    uint64_t* out_valid = ws.arena.alloc_array<uint64_t>(vwords, 8);
    auto* ov = static_cast<V11Vector*>(ws.arena.alloc(sizeof(V11Vector), alignof(V11Vector)));
    if ((n > 0u && !out) || !out_valid || !ov) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
    if (n > 0u) std::memset(out, 0, static_cast<size_t>(n) * sizeof(double));
    std::memset(out_valid, 0, vwords * sizeof(uint64_t));

    for (uint32_t i = 0; i < n; ++i) {
        if (!p4_lane_active(mask, i) || !va->is_valid_at(i)) continue;
        double a = p4_vec_to_f64(*va, i);
        if (std::isnan(a)) continue;
        double res = std::numeric_limits<double>::quiet_NaN();
        if (is_binary) {
            if (scalar_rhs ? !s_valid : !vb->is_valid_at(i)) continue;
            double b = scalar_rhs ? s_val : p4_vec_to_f64(*vb, i);
            if (std::isnan(b)) continue;
            res = std::atan2(a, b);
        } else {
            switch (subop) {
                case 0: res = std::sin(a); break;
                case 1: res = std::cos(a); break;
                case 2: res = std::tan(a); break;
                case 3: res = std::asin(a); break;
                case 4: res = std::acos(a); break;
                case 5: res = std::atan(a); break;
                case 7: res = (a == 0.0) ? 1.0 : (std::sin(kP4Pi * a) / (kP4Pi * a)); break;
                case 8: res = std::sinh(a); break;
                case 9: res = std::cosh(a); break;
                case 10: res = std::tanh(a); break;
                case 11: { // asinh (numerically stable log1p form matching Java/OCaml)
                    double ax = std::fabs(a);
                    double r = 0.0;
                    if (ax < 1e-8) r = ax;
                    else if (ax > 1e8) r = std::log(ax) + kP4Ln2;
                    else r = std::log1p(ax + (ax * ax) / (1.0 + std::sqrt(ax * ax + 1.0)));
                    res = std::copysign(r, a);
                    break;
                }
                case 12: res = (a < 1.0) ? std::numeric_limits<double>::quiet_NaN() : std::log(a + std::sqrt(a * a - 1.0)); break;
                case 13: { // atanh (numerically stable log1p form matching Java/OCaml)
                    double ax = std::fabs(a);
                    if (ax < 1.0) {
                        double r = (ax < 1e-8) ? ax : (0.5 * std::log1p((2.0 * ax) / (1.0 - ax)));
                        res = std::copysign(r, a);
                    } else if (a == 1.0) {
                        res = std::numeric_limits<double>::infinity();
                    } else if (a == -1.0) {
                        res = -std::numeric_limits<double>::infinity();
                    }
                    break;
                }
                default: break;
            }
        }
        if (!std::isnan(res)) {
            out[i] = res;
            out_valid[i >> 6] |= (1ULL << (i & 63u));
        }
    }
    new (ov) V11Vector();
    ov->elem_tag = kVecTagF64; ov->length = n; ov->f64_data = out; ov->valid_bits = out_valid;
    ws.v_regs[dst] = ov;
    ws.state.pc += 1u;
    V11_DISPATCH();
}
