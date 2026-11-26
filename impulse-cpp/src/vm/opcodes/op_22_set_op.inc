// Opcode 0x22: OP_SET_OP
V11_OP_CASE(SET_OP) {
    const uint8_t subop = IMP_V11_SUBOP(w);
    const uint8_t dst = IMP_V11_DST(w);
    const uint8_t s1 = IMP_V11_S1(w);
    const uint8_t s2 = IMP_V11_S2(w);

    if (subop > 3u) { ws.status = IMPULSE_VM_V11_ERR_INVALID_OPCODE; V11_DISPATCH(); }
    if (!ws.is_f_init(s1) || !ws.is_f_init(s2)) { ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; V11_DISPATCH(); }
    const V11Frontier& fa = ws.f_regs[s1];
    const V11Frontier& fb = ws.f_regs[s2];
    if (fa.domain_id != fb.domain_id) { ws.status = IMPULSE_VM_V11_ERR_DOMAIN_MISMATCH; V11_DISPATCH(); }
    if (fa.multi_lane != fb.multi_lane) { ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; V11_DISPATCH(); }
    uint16_t dom_id = fa.domain_id;
    uint64_t dom_card = fa.domain_card;

    if (!fa.multi_lane) {
        const uint32_t* a = nullptr;
        const uint32_t* b = nullptr;
        uint32_t na = 0u, nb = 0u;
        if (!fa.materialize_1bit(ws.arena, &a, &na) || !fb.materialize_1bit(ws.arena, &b, &nb)) {
            ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH();
        }
        size_t max_out = 0u;
        if (subop == 0u || subop == 3u) max_out = static_cast<size_t>(na) + static_cast<size_t>(nb);
        else if (subop == 1u)           max_out = (na < nb) ? na : nb;
        else if (subop == 2u)           max_out = na;
        else { ws.status = IMPULSE_VM_V11_ERR_INVALID_OPCODE; V11_DISPATCH(); }

        uint32_t* out = (max_out > 0u) ? ws.arena.alloc_array<uint32_t>(max_out, 8) : nullptr;
        if (max_out > 0u && !out) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
        uint32_t i = 0u, j = 0u, k = 0u;
        if (subop == 0u) {
            while (i < na && j < nb) {
                if (a[i] < b[j]) out[k++] = a[i++];
                else if (b[j] < a[i]) out[k++] = b[j++];
                else { out[k++] = a[i++]; ++j; }
            }
            while (i < na) out[k++] = a[i++];
            while (j < nb) out[k++] = b[j++];
        } else if (subop == 1u) {
            while (i < na && j < nb) {
                if (a[i] < b[j]) ++i;
                else if (b[j] < a[i]) ++j;
                else { out[k++] = a[i++]; ++j; }
            }
        } else if (subop == 2u) {
            while (i < na && j < nb) {
                if (a[i] < b[j]) out[k++] = a[i++];
                else if (b[j] < a[i]) ++j;
                else { ++i; ++j; }
            }
            while (i < na) out[k++] = a[i++];
        } else {
            while (i < na && j < nb) {
                if (a[i] < b[j]) out[k++] = a[i++];
                else if (b[j] < a[i]) out[k++] = b[j++];
                else { ++i; ++j; }
            }
            while (i < na) out[k++] = a[i++];
            while (j < nb) out[k++] = b[j++];
        }
        bool is_all = (static_cast<uint64_t>(k) == dom_card && dom_card > 0ULL);
        if (!p2_set_f1bit(ws, dst, dom_id, dom_card, out, k, is_all, true)) { V11_DISPATCH(); }
        ws.set_zf(k == 0u);
        ws.state.pc += 1u;
    } else {
        if (fa.alloc_width != fb.alloc_width) { ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; V11_DISPATCH(); }
        uint32_t na = fa.ml_count, nb = fb.ml_count;
        size_t max_out = static_cast<size_t>(na) + static_cast<size_t>(nb);
        uint64_t* out_n = (max_out > 0u) ? ws.arena.alloc_array<uint64_t>(max_out, 8) : nullptr;
        uint64_t* out_m = (max_out > 0u) ? ws.arena.alloc_array<uint64_t>(max_out, 8) : nullptr;
        if (max_out > 0u && (!out_n || !out_m)) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
        uint32_t i = 0u, j = 0u, k = 0u;
        while (i < na || j < nb) {
            uint64_t u = 0ULL, va = 0ULL, vb = 0ULL;
            if (j >= nb || (i < na && fa.ml_nodes[i] < fb.ml_nodes[j])) {
                u = fa.ml_nodes[i]; va = fa.ml_masks[i++]; vb = 0ULL;
            } else if (i >= na || fb.ml_nodes[j] < fa.ml_nodes[i]) {
                u = fb.ml_nodes[j]; va = 0ULL; vb = fb.ml_masks[j++];
            } else {
                u = fa.ml_nodes[i]; va = fa.ml_masks[i++]; vb = fb.ml_masks[j++];
            }
            uint64_t r = (subop == 0u) ? (va | vb) : (subop == 1u) ? (va & vb) : (subop == 2u) ? (va & ~vb) : (va ^ vb);
            if (r != 0ULL) { out_n[k] = u; out_m[k] = r; ++k; }
        }
        uint8_t width = fa.alloc_width;
        uint8_t active_lanes = (fa.active_lane_count > fb.active_lane_count) ? fa.active_lane_count : fb.active_lane_count;
        if (!ws.f_regs[dst].init_multi_lane(ws.arena, dom_id, dom_card, width, active_lanes, out_n, out_m, k, ws.geometry)) {
            ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH();
        }
        ws.set_f_init(dst, true);
        ws.set_zf(k == 0u);
        ws.state.pc += 1u;
    }
    V11_DISPATCH();
}
