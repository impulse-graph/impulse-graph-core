// Opcode 0x07: OP_LOAD_PARAM
V11_OP_CASE(LOAD_PARAM) {
    const uint8_t subop = IMP_V11_SUBOP(w);
    const uint8_t flags = IMP_V11_FLAGS(w);
    const uint8_t dst = IMP_V11_DST(w);
    const uint16_t pid = IMP_V11_IMM16_LO(w);
    const uint16_t hi = IMP_V11_IMM16_HI(w);
    const V11ParamEntry* e = ws.find_param(pid);
    switch (subop) {
        case 0: { // .scalar
            if (!e) {
                ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS;
            } else if (e->kind == ParamKind::Scalar) {
                bool valid_expected_tag = (hi <= 6u);
                bool exp_ok = valid_expected_tag &&
                    (hi == e->scalar_type ||
                     (!e->scalar_valid && e->scalar_type == static_cast<uint8_t>(ScalarType::SCALAR_NULL_UNSET)));
                if (!exp_ok) {
                    ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH;
                } else {
                    if (e->scalar_type == static_cast<uint8_t>(ScalarType::SCALAR_F64) && e->scalar_valid) {
                        double f = p1_bits_to_f64(e->scalar_bits);
                        if (std::isnan(f)) {
                            ws.write_r_null(dst);
                            p1_set_f64_flags(ws, false, 0.0);
                        } else {
                            ws.write_r(dst, static_cast<uint8_t>(ScalarType::SCALAR_F64), e->scalar_bits);
                            p1_set_f64_flags(ws, true, f);
                        }
                    } else if (!e->scalar_valid) {
                        ws.write_r_null(dst);
                        p1_set_scalar_flags(ws, false, 0ULL);
                    } else {
                        ws.write_r(dst, e->scalar_type, e->scalar_bits);
                        p1_set_scalar_flags(ws, true, e->scalar_bits);
                    }
                    ws.state.pc += 1u;
                }
            } else if (e->kind == ParamKind::Str) {
                if (hi != static_cast<uint16_t>(ScalarType::SCALAR_STR_HANDLE)) {
                    ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH;
                } else {
                    if (!e->str.valid) {
                        ws.write_r_null(dst);
                        p1_set_scalar_flags(ws, false, 0ULL);
                    } else {
                        uint64_t h = ws.intern_scalar_str(e->str.ptr, e->str.len);
                        if (h == 0ULL) {
                            ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW;
                            break;
                        }
                        ws.write_r(dst, static_cast<uint8_t>(ScalarType::SCALAR_STR_HANDLE), h);
                        ws.set_zf(e->str.len == 0u);
                        ws.set_nf(false);
                    }
                    ws.state.pc += 1u;
                }
            } else {
                ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH;
            }
            break;
        }
        case 1: { // .frontier
            if (!e) {
                ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS;
            } else if (e->kind == ParamKind::Frontier) {
                if (e->dom_id != hi) {
                    ws.status = IMPULSE_VM_V11_ERR_DOMAIN_MISMATCH;
                } else {
                    p1_load_frontier_ids(ws, snap, dst, hi, e->f_ids, e->f_len, (flags & FLAG_ACCUM) != 0u);
                }
            } else if (e->kind == ParamKind::MultiLane) {
                if (e->dom_id != hi) {
                    ws.status = IMPULSE_VM_V11_ERR_DOMAIN_MISMATCH;
                } else {
                    uint64_t dom_card = 0;
                    impulse_vm_status_v11_t st = v11_lookup_domain_card(snap, hi, &dom_card);
                    if (st != IMPULSE_VM_V11_OK) {
                        ws.status = st;
                        break;
                    }
                    struct Pair { uint64_t u; uint32_t idx; uint64_t m; };
                    Pair* pairs = (e->ml_len > 0) ? ws.arena.alloc_array<Pair>(e->ml_len, 8) : nullptr;
                    if (e->ml_len > 0 && !pairs) {
                        ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW;
                        break;
                    }
                    uint32_t kept = 0;
                    bool oob = false;
                    for (uint32_t i = 0; i < e->ml_len; ++i) {
                        uint64_t u = e->ml_nodes[i];
                        uint64_t m = e->ml_masks[i];
                        if (u >= dom_card) { oob = true; break; }
                        if (m != 0ULL) pairs[kept++] = {u, i, m};
                    }
                    if (oob) { ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; break; }
                    std::sort(pairs, pairs + kept, [](const Pair& a, const Pair& b) {
                        return (a.u != b.u) ? (a.u < b.u) : (a.idx < b.idx);
                    });
                    uint32_t uniq = 0;
                    for (uint32_t i = 0; i < kept; ++i) {
                        if (uniq > 0 && pairs[uniq - 1].u == pairs[i].u) pairs[uniq - 1].m = pairs[i].m;
                        else pairs[uniq++] = pairs[i];
                    }
                    uint64_t* sn = (uniq > 0) ? ws.arena.alloc_array<uint64_t>(uniq, 8) : nullptr;
                    uint64_t* sm = (uniq > 0) ? ws.arena.alloc_array<uint64_t>(uniq, 8) : nullptr;
                    if (uniq > 0 && (!sn || !sm)) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; break; }
                    for (uint32_t i = 0; i < uniq; ++i) { sn[i] = pairs[i].u; sm[i] = pairs[i].m; }
                    if (!ws.f_regs[dst].init_multi_lane(
                            ws.arena, hi, dom_card, e->ml_width, e->ml_active, sn, sm, uniq, ws.geometry)) {
                        ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW;
                        break;
                    }
                    ws.set_f_init(dst, true);
                    ws.set_zf(uniq == 0u);
                    ws.state.pc += 1u;
                }
            } else {
                ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH;
            }
            break;
        }
        case 2: { // .vec
            if (!e) {
                ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS;
            } else if (e->kind == ParamKind::Vec && e->vec != nullptr) {
                if (!e->vec->matches_expected_param_tag(hi)) {
                    ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH;
                } else {
                    ws.v_regs[dst] = e->vec;
                    ws.state.pc += 1u;
                }
            } else {
                ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH;
            }
            break;
        }
        default:
            ws.status = IMPULSE_VM_V11_ERR_INVALID_OPCODE;
            break;
    }
    V11_DISPATCH();
}
