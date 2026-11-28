// Opcode 0x6E: OP_LANE_STATE_REDUCE
VM_OP_CASE(LANE_STATE_REDUCE) {
    const uint8_t subop = IMP_SUBOP(w);
    const uint8_t flags = IMP_FLAGS(w);
    const uint8_t dst = IMP_DST(w);
    const uint8_t s1 = IMP_S1(w);
    const uint8_t s2 = IMP_S2(w);

    const VmVector* ls = ws.v_regs[s1];
    if (!ls || ls->elem_tag != kVecTagLaneState) { ws.status = IMPULSE_VM_ERR_TYPE_MISMATCH; VM_DISPATCH(); }
    const uint32_t n = ls->length;
    const uint16_t k = ls->ls_k;
    const size_t vwords = (n > 0u) ? ((static_cast<size_t>(n) + 63u) >> 6) : 1u;

    if (subop <= 3u) { // min, max, sum, mean
        double* out = (n > 0u) ? ws.arena.alloc_array<double>(n, 8) : nullptr;
        uint64_t* valid = ws.arena.alloc_array<uint64_t>(vwords, 8);
        auto* ov = static_cast<VmVector*>(ws.arena.alloc(sizeof(VmVector), alignof(VmVector)));
        if ((n > 0u && !out) || !valid || !ov) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
        if (n > 0u) std::memset(out, 0, static_cast<size_t>(n) * sizeof(double));
        std::memset(valid, 0, vwords * sizeof(uint64_t));
        bool any = false;
        for (uint32_t u = 0; u < n; ++u) {
            size_t base = static_cast<size_t>(u) * k;
            uint32_t cnt = 0u;
            double sum = 0.0, mn = 0.0, mx = 0.0;
            bool has = false;
            for (uint16_t l = 0; l < k; ++l) {
                if (!ls->ls_valid[base + l]) continue;
                double x = ls->ls_vals[base + l];
                cnt++;
                sum += x;
                mn = has ? p1_ieee_min_f64(mn, x) : x;
                mx = has ? p1_ieee_max_f64(mx, x) : x;
                has = true;
            }
            if (cnt > 0u) {
                if (subop == 0u) out[u] = mn;
                else if (subop == 1u) out[u] = mx;
                else if (subop == 2u) out[u] = sum;
                else out[u] = sum / static_cast<double>(cnt);
                valid[u >> 6] |= (1ULL << (u & 63u));
                any = true;
            }
        }
        new (ov) VmVector();
        ov->elem_tag = kVecTagF64; ov->length = n; ov->f64_data = out; ov->valid_bits = valid;
        ws.v_regs[dst] = ov;
        ws.set_zf(!any);
        ws.state.pc += 1u;
    } else if (subop == 4u || subop == 5u) { // argmin, argmax
        int64_t* out = (n > 0u) ? ws.arena.alloc_array<int64_t>(n, 8) : nullptr;
        uint64_t* valid = ws.arena.alloc_array<uint64_t>(vwords, 8);
        auto* ov = static_cast<VmVector*>(ws.arena.alloc(sizeof(VmVector), alignof(VmVector)));
        if ((n > 0u && !out) || !valid || !ov) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
        if (n > 0u) std::memset(out, 0, static_cast<size_t>(n) * sizeof(int64_t));
        std::memset(valid, 0, vwords * sizeof(uint64_t));
        bool any = false;
        for (uint32_t u = 0; u < n; ++u) {
            size_t base = static_cast<size_t>(u) * k;
            bool has = false;
            double best_v = 0.0;
            uint16_t best_l = 0u;
            for (uint16_t l = 0; l < k; ++l) {
                if (!ls->ls_valid[base + l]) continue;
                double x = ls->ls_vals[base + l];
                if (!has || (subop == 4u ? (x < best_v) : (x > best_v))) {
                    has = true; best_v = x; best_l = l;
                }
            }
            if (has) {
                out[u] = static_cast<int64_t>(best_l);
                valid[u >> 6] |= (1ULL << (u & 63u));
                any = true;
            }
        }
        new (ov) VmVector();
        ov->elem_tag = kVecTagU64; ov->length = n; ov->i64_data = out; ov->valid_bits = valid;
        ws.v_regs[dst] = ov;
        ws.set_zf(!any);
        ws.state.pc += 1u;
    } else if (subop == 6u) { // trace_path
        int64_t lane64 = ((flags & FLAG_SCALAR_RHS) != 0u && ws.is_r_valid(s2))
            ? static_cast<int64_t>(ws.read_r_raw(s2)) : 0;
        if (lane64 < 0 || lane64 >= static_cast<int64_t>(k)) { ws.status = IMPULSE_VM_ERR_OUT_OF_BOUNDS; VM_DISPATCH(); }
        uint16_t lane = static_cast<uint16_t>(lane64);
        int64_t* out = (n > 0u) ? ws.arena.alloc_array<int64_t>(n, 8) : nullptr;
        uint64_t* valid = ws.arena.alloc_array<uint64_t>(vwords, 8);
        auto* ov = static_cast<VmVector*>(ws.arena.alloc(sizeof(VmVector), alignof(VmVector)));
        if ((n > 0u && !out) || !valid || !ov) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
        if (n > 0u) std::memset(out, 0, static_cast<size_t>(n) * sizeof(int64_t));
        std::memset(valid, 0, vwords * sizeof(uint64_t));
        bool any = false;
        if (ls->ls_witness != nullptr) {
            for (uint32_t u = 0; u < n; ++u) {
                size_t idx = static_cast<size_t>(u) * k + lane;
                if (ls->ls_witness_valid[idx]) {
                    out[u] = ls->ls_witness[idx];
                    valid[u >> 6] |= (1ULL << (u & 63u));
                    any = true;
                }
            }
        }
        new (ov) VmVector();
        ov->elem_tag = kVecTagU64; ov->length = n; ov->i64_data = out; ov->valid_bits = valid;
        ws.v_regs[dst] = ov;
        ws.set_zf(!any);
        ws.state.pc += 1u;
    } else {
        ws.status = IMPULSE_VM_ERR_INVALID_OPCODE;
    }
    VM_DISPATCH();
}
