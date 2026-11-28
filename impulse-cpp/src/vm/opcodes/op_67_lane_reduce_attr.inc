// Opcode 0x67: OP_LANE_REDUCE_ATTR
VM_OP_CASE(LANE_REDUCE_ATTR) {
    const uint8_t subop = IMP_SUBOP(w);
    const uint8_t dst = IMP_DST(w);
    const uint8_t s1 = IMP_S1(w);
    const uint8_t s2 = IMP_S2(w);

    const VmFrontier* fa = p5_get_fml(ws, s1);
    if (!fa) VM_DISPATCH();
    const uint32_t ac = fa->active_lane_count;

    if (subop == 3u) { // .count
        int64_t* counts = (ac > 0u) ? ws.arena.alloc_array<int64_t>(ac, 8) : nullptr;
        uint64_t* valid = p4_alloc_valid_all_true(ws.arena, ac);
        auto* ov = static_cast<VmVector*>(ws.arena.alloc(sizeof(VmVector), alignof(VmVector)));
        if ((ac > 0u && !counts) || !valid || !ov) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
        if (ac > 0u) std::memset(counts, 0, static_cast<size_t>(ac) * sizeof(int64_t));
        uint32_t bit_lim = (ac < 64u) ? ac : 64u;
        for (uint32_t i = 0; i < fa->ml_count; ++i) {
            uint64_t lm = fa->ml_masks[i];
            for (uint32_t l = 0; l < bit_lim; ++l) {
                if ((lm & (1ULL << l)) != 0ULL) counts[l]++;
            }
        }
        new (ov) VmVector();
        ov->elem_tag = kVecTagU64; ov->length = ac; ov->i64_data = counts; ov->valid_bits = valid;
        ws.v_regs[dst] = ov;
        ws.set_zf(ac == 0u);
        ws.state.pc += 1u;
        VM_DISPATCH();
    }

    if (subop > 2u) { ws.status = IMPULSE_VM_ERR_INVALID_OPCODE; VM_DISPATCH(); }
    const VmVector* attr = ws.v_regs[s2];
    if (!p4_is_numeric_vec(attr)) { ws.status = IMPULSE_VM_ERR_TYPE_MISMATCH; VM_DISPATCH(); }

    size_t vwords = (ac > 0u) ? ((static_cast<size_t>(ac) + 63u) >> 6) : 1u;
    double* acc = (ac > 0u) ? ws.arena.alloc_array<double>(ac, 8) : nullptr;
    uint64_t* valid = (subop == 0u) ? p4_alloc_valid_all_true(ws.arena, ac) : ws.arena.alloc_array<uint64_t>(vwords, 8);
    auto* ov = static_cast<VmVector*>(ws.arena.alloc(sizeof(VmVector), alignof(VmVector)));
    if ((ac > 0u && !acc) || !valid || !ov) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
    if (ac > 0u) std::memset(acc, 0, static_cast<size_t>(ac) * sizeof(double));
    if (subop != 0u) std::memset(valid, 0, vwords * sizeof(uint64_t));

    for (uint32_t i = 0; i < fa->ml_count; ++i) {
        int64_t u_s = static_cast<int64_t>(fa->ml_nodes[i]);
        uint64_t u64 = fa->ml_nodes[i];
        if (u_s < 0 || u64 >= attr->length) { ws.status = IMPULSE_VM_ERR_OUT_OF_BOUNDS; VM_DISPATCH(); }
        uint32_t u = static_cast<uint32_t>(u64);
        if (!attr->is_valid_at(u)) continue;
        double v = p4_vec_to_f64(*attr, u);
        uint64_t lm = fa->ml_masks[i];
        uint32_t bit_lim = (ac < 64u) ? ac : 64u;
        for (uint32_t l = 0; l < bit_lim; ++l) {
            if ((lm & (1ULL << l)) == 0ULL) continue;
            bool has = ((valid[l >> 6] >> (l & 63u)) & 1ULL) != 0ULL;
            if (subop == 0u) acc[l] = has ? (acc[l] + v) : v;
            else if (subop == 1u) acc[l] = has ? p1_ieee_min_f64(acc[l], v) : v;
            else acc[l] = has ? p1_ieee_max_f64(acc[l], v) : v;
            valid[l >> 6] |= (1ULL << (l & 63u));
        }
    }
    new (ov) VmVector();
    ov->elem_tag = kVecTagF64; ov->length = ac; ov->f64_data = acc; ov->valid_bits = valid;
    ws.v_regs[dst] = ov;
    ws.set_zf(ac == 0u);
    ws.state.pc += 1u;
    VM_DISPATCH();
}
