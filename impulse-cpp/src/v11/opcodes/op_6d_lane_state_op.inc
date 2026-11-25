// Opcode 0x6D: OP_LANE_STATE_OP
V11_OP_CASE(LANE_STATE_OP) {
    const uint8_t subop = IMP_V11_SUBOP(w);
    const uint8_t flags = IMP_V11_FLAGS(w);
    const uint8_t dst = IMP_V11_DST(w);
    const uint8_t s1 = IMP_V11_S1(w);
    const uint8_t s2 = IMP_V11_S2(w);
    const uint8_t s3 = IMP_V11_S3(w);
    const int32_t imm32_s = IMP_V11_IMM32_S(w);

    const V11Vector* ls = ws.v_regs[s1];
    if (!ls || ls->elem_tag != kVecTagLaneState) { ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; V11_DISPATCH(); }
    const uint32_t n = ls->length;
    const uint16_t k = ls->ls_k;
    const uint16_t dom = ls->ls_dom;

    switch (subop) {
        case 0: { // .project_affine: fma(a, x, b)
            double a = p4_is_scalar_f64_valid(ws, s2) ? p4_read_scalar_f64(ws, s2) : 1.0;
            double b = p4_is_scalar_f64_valid(ws, s3) ? p4_read_scalar_f64(ws, s3) : 0.0;
            V11Vector* ov = p5_alloc_lane_state(ws.arena, dom, n, k, false);
            if (!ov) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
            ov->ls_witness = ls->ls_witness;
            ov->ls_witness_valid = ls->ls_witness_valid;
            size_t total = static_cast<size_t>(n) * k;
            for (size_t idx = 0; idx < total; ++idx) {
                if (!ls->ls_valid[idx]) continue;
                double r = std::fma(a, ls->ls_vals[idx], b);
                if (!std::isnan(r)) { ov->ls_vals[idx] = r; ov->ls_valid[idx] = 1u; }
            }
            ws.v_regs[dst] = ov;
            ws.set_zf(false);
            ws.state.pc += 1u;
            break;
        }
        case 1: { // .prune_cutoff
            double cut = p4_is_scalar_f64_valid(ws, s2) ? p4_read_scalar_f64(ws, s2) : std::numeric_limits<double>::infinity();
            V11Vector* ov = p5_alloc_lane_state(ws.arena, dom, n, k, false);
            if (!ov) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
            ov->ls_witness = ls->ls_witness;
            ov->ls_witness_valid = ls->ls_witness_valid;
            size_t total = static_cast<size_t>(n) * k;
            bool any = false;
            for (size_t idx = 0; idx < total; ++idx) {
                if (ls->ls_valid[idx] && ls->ls_vals[idx] <= cut) {
                    ov->ls_vals[idx] = ls->ls_vals[idx];
                    ov->ls_valid[idx] = 1u;
                    any = true;
                }
            }
            ws.v_regs[dst] = ov;
            ws.set_zf(!any);
            ws.state.pc += 1u;
            break;
        }
        case 2: { // .prune_hub_quorum
            int64_t q = ((flags & FLAG_SCALAR_RHS) != 0u)
                ? (ws.is_r_valid(s2) ? static_cast<int64_t>(ws.read_r_raw(s2)) : 1)
                : static_cast<int64_t>(imm32_s);
            V11Vector* ov = p5_alloc_lane_state(ws.arena, dom, n, k, false);
            if (!ov) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
            ov->ls_witness = ls->ls_witness;
            ov->ls_witness_valid = ls->ls_witness_valid;
            bool any = false;
            for (uint32_t u = 0; u < n; ++u) {
                size_t base = static_cast<size_t>(u) * k;
                int64_t cnt = 0;
                for (uint16_t l = 0; l < k; ++l) if (ls->ls_valid[base + l]) ++cnt;
                if (cnt >= q) {
                    any = true;
                    std::memcpy(ov->ls_vals + base, ls->ls_vals + base, static_cast<size_t>(k) * sizeof(double));
                    std::memcpy(ov->ls_valid + base, ls->ls_valid + base, k);
                }
            }
            ws.v_regs[dst] = ov;
            ws.set_zf(!any);
            ws.state.pc += 1u;
            break;
        }
        case 3: { // .to_lane_frontier
            uint64_t* ns = (n > 0u) ? ws.arena.alloc_array<uint64_t>(n, 8) : nullptr;
            uint64_t* ms = (n > 0u) ? ws.arena.alloc_array<uint64_t>(n, 8) : nullptr;
            if (n > 0u && (!ns || !ms)) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
            uint16_t lim = (k < 64u) ? k : 64u;
            uint32_t cnt = 0u;
            for (uint32_t u = 0; u < n; ++u) {
                size_t base = static_cast<size_t>(u) * k;
                uint64_t m = 0ULL;
                for (uint16_t l = 0; l < lim; ++l) {
                    if (ls->ls_valid[base + l]) m |= (1ULL << l);
                }
                if (m != 0ULL) { ns[cnt] = u; ms[cnt] = m; ++cnt; }
            }
            if (!ws.f_regs[dst].init_multi_lane(ws.arena, dom, n, p5_choose_width(k), static_cast<uint8_t>(k), ns, ms, cnt, ws.geometry)) {
                ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH();
            }
            ws.set_f_init(dst, true);
            ws.set_zf(cnt == 0u);
            ws.state.pc += 1u;
            break;
        }
        case 4: { // .to_quorum_frontier
            int64_t q = ((flags & FLAG_SCALAR_RHS) != 0u)
                ? (ws.is_r_valid(s2) ? static_cast<int64_t>(ws.read_r_raw(s2)) : 1)
                : static_cast<int64_t>(imm32_s);
            uint32_t* out = (n > 0u) ? ws.arena.alloc_array<uint32_t>(n, 8) : nullptr;
            if (n > 0u && !out) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
            uint32_t cnt = 0u;
            for (uint32_t u = 0; u < n; ++u) {
                size_t base = static_cast<size_t>(u) * k;
                int64_t lc = 0;
                for (uint16_t l = 0; l < k; ++l) if (ls->ls_valid[base + l]) ++lc;
                if (lc >= q) out[cnt++] = u;
            }
            if (!ws.f_regs[dst].init_from_sorted_ids(ws.arena, dom, n, out, cnt, ws.geometry, true)) {
                ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH();
            }
            ws.set_f_init(dst, true);
            ws.set_zf(cnt == 0u);
            ws.state.pc += 1u;
            break;
        }
        default:
            ws.status = IMPULSE_VM_V11_ERR_INVALID_OPCODE;
            break;
    }
    V11_DISPATCH();
}
