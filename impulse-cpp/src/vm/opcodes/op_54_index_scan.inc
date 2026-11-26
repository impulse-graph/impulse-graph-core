// Opcode 0x54: OP_INDEX_SCAN
V11_OP_CASE(INDEX_SCAN) {
    const uint8_t subop = IMP_V11_SUBOP(w);
    const uint8_t flags = IMP_V11_FLAGS(w);
    const uint8_t dst = IMP_V11_DST(w);
    const uint8_t s2 = IMP_V11_S2(w);
    const uint8_t s3 = IMP_V11_S3(w);
    const uint16_t dom_id = IMP_V11_IMM16_LO(w);
    const uint16_t attr_id = IMP_V11_IMM16_HI(w);

    if (!snap) { ws.status = IMPULSE_VM_V11_ERR_NULL_SNAPSHOT; V11_DISPATCH(); }
    if (dom_id >= snap->domain_count || !p3_has_index(snap, dom_id, attr_id)) {
        ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; V11_DISPATCH();
    }
    const V11Vector* col = p3_find_node_attr(snap, dom_id, attr_id);
    if (!col) { ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; V11_DISPATCH(); }

    const uint64_t dom_card = snap->domain_cards[dom_id];
    const bool accum = (flags & FLAG_ACCUM) != 0u;
    const uint32_t* acc_ids = nullptr; uint32_t acc_len = 0u;
    if (accum) {
        const V11Frontier* fd = p3_get_f1bit(ws, dst);
        if (!fd) V11_DISPATCH();
        if (fd->domain_id != dom_id) { ws.status = IMPULSE_VM_V11_ERR_DOMAIN_MISMATCH; V11_DISPATCH(); }
        if (!fd->materialize_1bit(ws.arena, &acc_ids, &acc_len)) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
    }

    const bool lo_valid = ws.is_r_valid(s2);
    const bool hi_valid = ws.is_r_valid(s3);
    const uint64_t lo_bits = ws.read_r_raw(s2);
    const uint64_t hi_bits = ws.read_r_raw(s3);
    const double lo_f = p1_bits_to_f64(lo_bits);
    const double hi_f = p1_bits_to_f64(hi_bits);
    const V11StrSlice lo_str = lo_valid ? ws.resolve_scalar_str(lo_bits) : V11StrSlice{nullptr, 0u, 0u};
    const V11StrSlice hi_str = hi_valid ? ws.resolve_scalar_str(hi_bits) : V11StrSlice{nullptr, 0u, 0u};

    uint32_t n = col->length;
    uint32_t* kept = (n > 0u) ? ws.arena.alloc_array<uint32_t>(n, 8) : nullptr;
    if (n > 0u && !kept) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
    uint32_t kept_cnt = 0u;

    for (uint32_t u = 0; u < n; ++u) {
        if (!col->is_valid_at(u) || !lo_valid) continue;
        bool keep = false;
        switch (col->elem_tag) {
            case kVecTagI64: {
                int64_t x = col->i64_data[u];
                int64_t lo = static_cast<int64_t>(lo_bits);
                int64_t hi = static_cast<int64_t>(hi_bits);
                switch (subop) {
                    case 0: keep = (x == lo); break;
                    case 1: keep = hi_valid && x >= lo && x <= hi; break;
                    case 2: keep = hi_valid && x >= lo && x <  hi; break;
                    case 3: keep = (x <  lo); break;
                    case 4: keep = (x <= lo); break;
                    case 5: keep = (x >  lo); break;
                    case 6: keep = (x >= lo); break;
                    default: break;
                }
                break;
            }
            case kVecTagU64: {
                uint64_t x = static_cast<uint64_t>(col->i64_data[u]);
                switch (subop) {
                    case 0: keep = (x == lo_bits); break;
                    case 1: keep = hi_valid && x >= lo_bits && x <= hi_bits; break;
                    case 2: keep = hi_valid && x >= lo_bits && x <  hi_bits; break;
                    case 3: keep = (x <  lo_bits); break;
                    case 4: keep = (x <= lo_bits); break;
                    case 5: keep = (x >  lo_bits); break;
                    case 6: keep = (x >= lo_bits); break;
                    default: break;
                }
                break;
            }
            case kVecTagF64: {
                double x = col->f64_data[u];
                if (!std::isnan(lo_f)) {
                    switch (subop) {
                        case 0: keep = (x == lo_f); break;
                        case 1: keep = hi_valid && !std::isnan(hi_f) && x >= lo_f && x <= hi_f; break;
                        case 2: keep = hi_valid && !std::isnan(hi_f) && x >= lo_f && x <  hi_f; break;
                        case 3: keep = (x <  lo_f); break;
                        case 4: keep = (x <= lo_f); break;
                        case 5: keep = (x >  lo_f); break;
                        case 6: keep = (x >= lo_f); break;
                        default: break;
                    }
                }
                break;
            }
            case kVecTagStr: {
                V11StrSlice x = col->str_data[u];
                if (lo_str.valid) {
                    switch (subop) {
                        case 0: keep = p4_str_eq(x, lo_str); break;
                        case 1: keep = hi_str.valid && p4_str_cmp(x, lo_str) >= 0 && p4_str_cmp(x, hi_str) <= 0; break;
                        case 2: keep = hi_str.valid && p4_str_cmp(x, lo_str) >= 0 && p4_str_cmp(x, hi_str) <  0; break;
                        case 3: keep = p4_str_cmp(x, lo_str) <  0; break;
                        case 4: keep = p4_str_cmp(x, lo_str) <= 0; break;
                        case 5: keep = p4_str_cmp(x, lo_str) >  0; break;
                        case 6: keep = p4_str_cmp(x, lo_str) >= 0; break;
                        default: break;
                    }
                }
                break;
            }
            default: break;
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
