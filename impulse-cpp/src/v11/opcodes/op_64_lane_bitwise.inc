// Opcode 0x64: OP_LANE_BITWISE
V11_OP_CASE(LANE_BITWISE) {
    const uint8_t subop = IMP_V11_SUBOP(w);
    const uint8_t flags = IMP_V11_FLAGS(w);
    const uint8_t dst = IMP_V11_DST(w);
    const uint8_t s1 = IMP_V11_S1(w);
    const uint8_t s2 = IMP_V11_S2(w);
    const uint16_t imm_lo = IMP_V11_IMM16_LO(w);

    const V11Frontier* fa = p5_get_fml(ws, s1);
    if (!fa) V11_DISPATCH();
    const uint16_t dom_id = fa->domain_id;
    const uint64_t dom_card = fa->domain_card;
    const uint8_t aw = fa->alloc_width;
    const uint8_t ac = fa->active_lane_count;
    const V11MlMap ma = p5_ml_view(*fa);

    switch (subop) {
        case 0: case 1: { // prune_mask / keep_mask
            uint64_t m = 0ULL;
            if ((flags & FLAG_SCALAR_RHS) != 0u) {
                if (!ws.is_r_valid(s2)) { ws.status = IMPULSE_VM_V11_ERR_INVALID_OPERAND; V11_DISPATCH(); }
                m = ws.read_r_raw(s2);
            } else {
                m = static_cast<uint64_t>(imm_lo);
            }
            uint64_t* ns = (ma.len > 0u) ? ws.arena.alloc_array<uint64_t>(ma.len, 8) : nullptr;
            uint64_t* ms = (ma.len > 0u) ? ws.arena.alloc_array<uint64_t>(ma.len, 8) : nullptr;
            if (ma.len > 0u && (!ns || !ms)) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
            uint32_t cnt = 0u;
            for (uint32_t i = 0; i < ma.len; ++i) {
                uint64_t r = (subop == 0u) ? (ma.masks[i] & ~m) : (ma.masks[i] & m);
                if (r != 0ULL) { ns[cnt] = ma.nodes[i]; ms[cnt] = r; ++cnt; }
            }
            if (!ws.f_regs[dst].init_multi_lane(ws.arena, dom_id, dom_card, aw, ac, ns, ms, cnt, ws.geometry)) {
                ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH();
            }
            ws.set_f_init(dst, true);
            ws.set_zf(cnt == 0u);
            ws.state.pc += 1u;
            break;
        }
        case 2: { // and_not_lanes
            const V11Frontier* fb = p5_get_fml(ws, s2);
            if (!fb) V11_DISPATCH();
            if (dom_id != fb->domain_id) { ws.status = IMPULSE_VM_V11_ERR_DOMAIN_MISMATCH; V11_DISPATCH(); }
            V11MlMap out{};
            if (!p5_ml_diff(ws.arena, ma, p5_ml_view(*fb), &out)) {
                ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH();
            }
            if (!ws.f_regs[dst].init_multi_lane(ws.arena, dom_id, dom_card, aw, ac, out.nodes, out.masks, out.len, ws.geometry)) {
                ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH();
            }
            ws.set_f_init(dst, true);
            ws.set_zf(out.len == 0u);
            ws.state.pc += 1u;
            break;
        }
        case 3: { // extract_lane -> 1-bit F[dst]
            uint32_t lane = 0u;
            if ((flags & FLAG_SCALAR_RHS) != 0u) {
                if (!ws.is_r_valid(s2)) { ws.status = IMPULSE_VM_V11_ERR_INVALID_OPERAND; V11_DISPATCH(); }
                int64_t l64 = static_cast<int64_t>(ws.read_r_raw(s2));
                if (l64 < 0 || static_cast<uint64_t>(l64) >= aw) { ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; V11_DISPATCH(); }
                lane = static_cast<uint32_t>(l64);
            } else {
                if (imm_lo >= aw) { ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; V11_DISPATCH(); }
                lane = static_cast<uint32_t>(imm_lo);
            }
            uint64_t bit = 1ULL << lane;
            uint32_t* out = (ma.len > 0u) ? ws.arena.alloc_array<uint32_t>(ma.len, 8) : nullptr;
            if (ma.len > 0u && !out) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
            uint32_t cnt = 0u;
            for (uint32_t i = 0; i < ma.len; ++i) {
                if ((ma.masks[i] & bit) != 0ULL) out[cnt++] = static_cast<uint32_t>(ma.nodes[i]);
            }
            if (!ws.f_regs[dst].init_from_sorted_ids(ws.arena, dom_id, dom_card, out, cnt, ws.geometry, true)) {
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
