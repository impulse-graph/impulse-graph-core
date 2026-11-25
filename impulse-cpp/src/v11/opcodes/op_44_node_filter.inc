// Opcode 0x44: OP_NODE_FILTER
V11_OP_CASE(NODE_FILTER) {
    const uint8_t subop = IMP_V11_SUBOP(w);
    const uint8_t flags = IMP_V11_FLAGS(w);
    const uint8_t dst = IMP_V11_DST(w);
    const uint8_t s1 = IMP_V11_S1(w);
    const uint8_t s2 = IMP_V11_S2(w);
    const uint8_t s3 = IMP_V11_S3(w);
    const uint16_t dom_id = IMP_V11_IMM16_LO(w);
    const uint16_t attr_id = IMP_V11_IMM16_HI(w);

    if (!snap) { ws.status = IMPULSE_VM_V11_ERR_NULL_SNAPSHOT; V11_DISPATCH(); }
    const V11Frontier* f_src = p3_get_f1bit(ws, s1);
    if (!f_src) V11_DISPATCH();
    if (f_src->domain_id != dom_id) { ws.status = IMPULSE_VM_V11_ERR_DOMAIN_MISMATCH; V11_DISPATCH(); }
    if (dom_id >= snap->domain_count) { ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; V11_DISPATCH(); }
    const V11Vector* col = p3_find_node_attr(snap, dom_id, attr_id);
    if (!col) { ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; V11_DISPATCH(); }

    const uint64_t dom_card = snap->domain_cards[dom_id];
    const bool accum = (flags & FLAG_ACCUM) != 0u;
    const bool scalar_rhs = (flags & FLAG_SCALAR_RHS) != 0u;

    const uint32_t* acc_ids = nullptr; uint32_t acc_len = 0u;
    if (accum) {
        const V11Frontier* fd = p3_get_f1bit(ws, dst);
        if (!fd) V11_DISPATCH();
        if (fd->domain_id != dom_id) { ws.status = IMPULSE_VM_V11_ERR_DOMAIN_MISMATCH; V11_DISPATCH(); }
        if (!fd->materialize_1bit(ws.arena, &acc_ids, &acc_len)) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
    }

    const uint32_t* src_ids = nullptr; uint32_t src_len = 0u;
    if (!f_src->materialize_1bit(ws.arena, &src_ids, &src_len)) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }

    const V11Vector* v_s2 = scalar_rhs ? nullptr : ws.v_regs[s2];
    const V11Vector* v_s3 = scalar_rhs ? nullptr : ws.v_regs[s3];

    uint32_t* kept = (src_len > 0u) ? ws.arena.alloc_array<uint32_t>(src_len, 8) : nullptr;
    if (src_len > 0u && !kept) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
    uint32_t kept_cnt = 0u;

    for (uint32_t i = 0; i < src_len; ++i) {
        uint32_t u = src_ids[i];
        if (u >= col->length) { ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; V11_DISPATCH(); }
        bool col_valid = col->is_valid_at(u);
        bool keep = false;
        if (subop == 7u) { // .is_null
            keep = !col_valid;
        } else if (subop == 8u) { // .is_not_null
            keep = col_valid;
        } else if (col_valid) {
            if (col->elem_tag == kVecTagF64) {
                double x = col->f64_data[u];
                bool lo_ok = !std::isnan(x) && (scalar_rhs ? p4_is_scalar_f64_valid(ws, s2)
                                                           : (p4_is_numeric_vec(v_s2) && u < v_s2->length && v_s2->is_valid_at(u)));
                if (lo_ok) {
                    double lo = scalar_rhs ? p4_read_scalar_f64(ws, s2) : p4_vec_to_f64(*v_s2, u);
                    if (!std::isnan(lo)) {
                        if (subop == 6u) { // .between
                            bool hi_ok = scalar_rhs ? p4_is_scalar_f64_valid(ws, s3)
                                                    : (p4_is_numeric_vec(v_s3) && u < v_s3->length && v_s3->is_valid_at(u));
                            if (hi_ok) {
                                double hi = scalar_rhs ? p4_read_scalar_f64(ws, s3) : p4_vec_to_f64(*v_s3, u);
                                keep = !std::isnan(hi) && (x >= lo && x <= hi);
                            }
                        } else {
                            keep = p4_eval_cmp_f64(subop, x, lo);
                        }
                    }
                }
            } else if (col->elem_tag == kVecTagI64 || col->elem_tag == kVecTagU64) {
                bool is_u = (col->elem_tag == kVecTagU64);
                int64_t x = col->i64_data[u];
                bool lo_ok = scalar_rhs ? ws.is_r_valid(s2)
                                        : (v_s2 && (v_s2->elem_tag == kVecTagI64 || v_s2->elem_tag == kVecTagU64) && u < v_s2->length && v_s2->is_valid_at(u));
                if (lo_ok) {
                    int64_t lo = scalar_rhs ? static_cast<int64_t>(ws.read_r_raw(s2)) : v_s2->i64_data[u];
                    if (subop == 6u) {
                        bool hi_ok = scalar_rhs ? ws.is_r_valid(s3)
                                                : (v_s3 && (v_s3->elem_tag == kVecTagI64 || v_s3->elem_tag == kVecTagU64) && u < v_s3->length && v_s3->is_valid_at(u));
                        if (hi_ok) {
                            int64_t hi = scalar_rhs ? static_cast<int64_t>(ws.read_r_raw(s3)) : v_s3->i64_data[u];
                            if (is_u) {
                                uint64_t xu = static_cast<uint64_t>(x), lou = static_cast<uint64_t>(lo), hiu = static_cast<uint64_t>(hi);
                                keep = (xu >= lou && xu <= hiu);
                            } else {
                                keep = (x >= lo && x <= hi);
                            }
                        }
                    } else {
                        int cmp = 0;
                        if (is_u) {
                            uint64_t xu = static_cast<uint64_t>(x), lou = static_cast<uint64_t>(lo);
                            cmp = (xu < lou) ? -1 : (xu > lou ? 1 : 0);
                        } else {
                            cmp = (x < lo) ? -1 : (x > lo ? 1 : 0);
                        }
                        keep = p4_eval_cmp_op(subop, cmp);
                    }
                }
            }
        }
        if (keep) kept[kept_cnt++] = u;
    }

    uint32_t* final_ids = kept;
    uint32_t final_len = kept_cnt;
    if (accum && acc_len > 0u) {
        if (!p3_union_sorted(ws.arena, acc_ids, acc_len, kept, kept_cnt, &final_ids, &final_len)) {
            ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH();
        }
    }
    if (!ws.f_regs[dst].init_from_sorted_ids(ws.arena, dom_id, dom_card, final_ids, final_len, ws.geometry, true)) {
        ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH();
    }
    ws.set_f_init(dst, true);
    ws.set_zf(final_len == 0u);
    ws.state.pc += 1u;
    V11_DISPATCH();
}
