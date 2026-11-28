// Opcode 0x49: OP_VEC_MATH_EXP_LOG
VM_OP_CASE(VEC_MATH_EXP_LOG) {
    const uint8_t subop = IMP_SUBOP(w);
    const uint8_t flags = IMP_FLAGS(w);
    const uint8_t dst = IMP_DST(w);
    const uint8_t s1 = IMP_S1(w);
    const uint8_t s2 = IMP_S2(w);
    const uint8_t s3 = IMP_S3(w);

    const VmFrontier* mask = p4_get_mask_frontier(ws, flags, s3);
    if (ws.status != IMPULSE_VM_OK) VM_DISPATCH();
    const VmVector* va = ws.v_regs[s1];
    if (!p4_is_numeric_vec(va)) { ws.status = IMPULSE_VM_ERR_TYPE_MISMATCH; VM_DISPATCH(); }

    const uint32_t n = va->length;
    const bool is_binary = (subop == 8u || subop == 10u); // pow, hypot
    const bool scalar_rhs = (flags & FLAG_SCALAR_RHS) != 0u;
    const VmVector* vb = nullptr;
    if (is_binary && !scalar_rhs) {
        vb = ws.v_regs[s2];
        if (!p4_is_numeric_vec(vb)) { ws.status = IMPULSE_VM_ERR_TYPE_MISMATCH; VM_DISPATCH(); }
        if (vb->length != n) { ws.status = IMPULSE_VM_ERR_OUT_OF_BOUNDS; VM_DISPATCH(); }
    }
    const bool s_valid = is_binary && scalar_rhs && p4_is_scalar_f64_valid(ws, s2);
    const double s_val = s_valid ? p4_read_scalar_f64(ws, s2) : std::numeric_limits<double>::quiet_NaN();

    const size_t vwords = (n > 0u) ? ((static_cast<size_t>(n) + 63u) >> 6) : 1u;
    double* out = (n > 0u) ? ws.arena.alloc_array<double>(n, 8) : nullptr;
    uint64_t* out_valid = ws.arena.alloc_array<uint64_t>(vwords, 8);
    auto* ov = static_cast<VmVector*>(ws.arena.alloc(sizeof(VmVector), alignof(VmVector)));
    if ((n > 0u && !out) || !out_valid || !ov) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
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
            res = (subop == 8u) ? std::pow(a, b) : std::hypot(a, b);
        } else {
            switch (subop) {
                case 0:  res = std::exp(a); break;
                case 1:  res = std::pow(2.0, a); break;
                case 2:  res = std::pow(10.0, a); break;
                case 3:  res = std::expm1(a); break;
                case 4:  res = (a <= 0.0) ? std::numeric_limits<double>::quiet_NaN() : std::log(a); break;
                case 5:  res = (a <= 0.0) ? std::numeric_limits<double>::quiet_NaN() : (std::log(a) / kP4Ln2); break;
                case 6:  res = (a <= 0.0) ? std::numeric_limits<double>::quiet_NaN() : std::log10(a); break;
                case 7:  res = (a <= -1.0) ? std::numeric_limits<double>::quiet_NaN() : std::log1p(a); break;
                case 9:  res = std::cbrt(a); break;
                case 11: res = p4_erf_approx(a); break;
                case 12: res = p4_erfc_approx(a); break;
                case 13: res = p4_lgamma_approx(a); break;
                default: break;
            }
        }
        if (!std::isnan(res)) {
            out[i] = res;
            out_valid[i >> 6] |= (1ULL << (i & 63u));
        }
    }
    new (ov) VmVector();
    ov->elem_tag = kVecTagF64; ov->length = n; ov->f64_data = out; ov->valid_bits = out_valid;
    ws.v_regs[dst] = ov;
    ws.state.pc += 1u;
    VM_DISPATCH();
}
