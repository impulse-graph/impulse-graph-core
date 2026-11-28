// Opcode 0x6B: OP_LANE_STATE_INIT
VM_OP_CASE(LANE_STATE_INIT) {
    const uint8_t subop = IMP_SUBOP(w);
    const uint8_t flags = IMP_FLAGS(w);
    const uint8_t dst = IMP_DST(w);
    const uint8_t s1 = IMP_S1(w);
    const uint8_t s2 = IMP_S2(w);
    const uint16_t dom = IMP_IMM16_LO(w);
    const uint16_t imm_hi = IMP_IMM16_HI(w);

    if (!snap) { ws.status = IMPULSE_VM_ERR_NULL_SNAPSHOT; VM_DISPATCH(); }
    if (dom >= snap->domain_count) { ws.status = IMPULSE_VM_ERR_OUT_OF_BOUNDS; VM_DISPATCH(); }
    uint64_t n64 = snap->domain_cards[dom];
    if (n64 > UINT32_MAX) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
    uint32_t n = static_cast<uint32_t>(n64);

    if (subop <= 3u) {
        uint16_t k = (imm_hi > 0u) ? imm_hi : 1u;
        bool is_zero = (subop == 0u || subop == 2u);
        VmVector* ov = p5_alloc_lane_state(ws.arena, dom, n, k, false);
        if (!ov) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
        if (is_zero && n > 0u) {
            std::memset(ov->ls_valid, 1, static_cast<size_t>(n) * k);
        }
        ws.v_regs[dst] = ov;
        ws.set_zf(n == 0u);
        ws.state.pc += 1u;
    } else if (subop == 4u || subop == 5u) {
        const VmFrontier* fa = p5_get_fml(ws, s1);
        if (!fa) VM_DISPATCH();
        if (fa->domain_id != dom) { ws.status = IMPULSE_VM_ERR_DOMAIN_MISMATCH; VM_DISPATCH(); }
        uint16_t kk = (imm_hi > 0u) ? imm_hi : (fa->active_lane_count > 0u ? static_cast<uint16_t>(fa->active_lane_count) : 1u);
        bool seed_valid = ((flags & FLAG_SCALAR_RHS) == 0u) || p4_is_scalar_f64_valid(ws, s2);
        double seed_val = (((flags & FLAG_SCALAR_RHS) != 0u) && seed_valid) ? p4_read_scalar_f64(ws, s2) : 0.0;
        VmVector* ov = p5_alloc_lane_state(ws.arena, dom, n, kk, subop == 5u);
        if (!ov) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
        for (uint32_t i = 0; i < fa->ml_count; ++i) {
            uint64_t u64 = fa->ml_nodes[i];
            if (u64 >= n) continue;
            uint32_t u = static_cast<uint32_t>(u64);
            uint64_t lm = fa->ml_masks[i];
            size_t base = static_cast<size_t>(u) * kk;
            for (uint16_t l = 0; l < kk; ++l) {
                if ((lm & (1ULL << (l & 63u))) != 0ULL) {
                    if (seed_valid) {
                        ov->ls_vals[base + l] = seed_val;
                        ov->ls_valid[base + l] = 1u;
                    }
                    if (ov->ls_witness != nullptr) {
                        ov->ls_witness[base + l] = static_cast<int64_t>(u);
                        ov->ls_witness_valid[base + l] = 1u;
                    }
                }
            }
        }
        ws.v_regs[dst] = ov;
        ws.set_zf(fa->ml_count == 0u);
        ws.state.pc += 1u;
    } else {
        ws.status = IMPULSE_VM_ERR_INVALID_OPCODE;
    }
    VM_DISPATCH();
}
