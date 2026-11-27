// Opcode 0x33: OP_DEGREE
V11_OP_CASE(DEGREE) {
    const uint8_t subop = IMP_V11_SUBOP(w);
    const uint8_t flags = IMP_V11_FLAGS(w);
    const uint8_t dst = IMP_V11_DST(w);
    const uint8_t s1 = IMP_V11_S1(w);
    const uint8_t s3 = IMP_V11_S3(w);
    const uint16_t rel_id = IMP_V11_IMM16_LO(w);

    const V11RelView* rel = p3_get_rel(snap, ws, rel_id);
    if (!rel) V11_DISPATCH();

    switch (subop) {
        case 0: { // .sum_fwd_r
            if (!rel->has_csr && (rel->has_csc || rel->has_coo)) { ws.status = IMPULSE_VM_V11_ERR_INVALID_OPERAND; break; }
            const V11Frontier* f_src = p3_get_f1bit(ws, s1);
            if (!f_src) break;
            if (f_src->domain_id != rel->src_dom) { ws.status = IMPULSE_VM_V11_ERR_DOMAIN_MISMATCH; break; }
            const uint32_t* ids = nullptr; uint32_t n = 0u;
            if (!f_src->materialize_1bit(ws.arena, &ids, &n)) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; break; }
            uint64_t sum = 0ULL;
            for (uint32_t i = 0; i < n; ++i) {
                uint32_t u = ids[i];
                if (u + 1u < rel->row_off_len) sum += (rel->csr_row_off(u + 1u) - rel->csr_row_off(u));
            }
            ws.write_r(dst, static_cast<uint8_t>(ScalarType::SCALAR_U64), sum);
            p1_set_scalar_flags(ws, true, sum);
            ws.state.pc += 1u;
            break;
        }
        case 1: { // .sum_rev_r
            if (rel->csc_row_off_raw == nullptr) { ws.status = IMPULSE_VM_V11_ERR_INVALID_OPERAND; break; }
            const V11Frontier* f_tgt = p3_get_f1bit(ws, s1);
            if (!f_tgt) break;
            if (f_tgt->domain_id != rel->tgt_dom) { ws.status = IMPULSE_VM_V11_ERR_DOMAIN_MISMATCH; break; }
            const uint32_t* ids = nullptr; uint32_t n = 0u;
            if (!f_tgt->materialize_1bit(ws.arena, &ids, &n)) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; break; }
            uint64_t sum = 0ULL;
            for (uint32_t i = 0; i < n; ++i) {
                uint32_t v = ids[i];
                if (rel->csc_row_off_len > 0u && v < rel->csc_row_off_len - 1u) {
                    sum += (rel->csc_row_off(v + 1u) - rel->csc_row_off(v));
                }
            }
            ws.write_r(dst, static_cast<uint8_t>(ScalarType::SCALAR_U64), sum);
            p1_set_scalar_flags(ws, true, sum);
            ws.state.pc += 1u;
            break;
        }
        case 2: { // .vec_fwd_v
            if (!rel->has_csr && (rel->has_csc || rel->has_coo)) { ws.status = IMPULSE_VM_V11_ERR_INVALID_OPERAND; break; }
            const V11Frontier* fm = nullptr;
            if ((flags & FLAG_MASKED_S3) != 0u) {
                fm = p3_get_f1bit(ws, s3);
                if (!fm) break;
                if (fm->domain_id != rel->src_dom) { ws.status = IMPULSE_VM_V11_ERR_DOMAIN_MISMATCH; break; }
            }
            uint64_t n64 = (rel->src_dom < snap->domain_count) ? snap->domain_cards[rel->src_dom] : 0ULL;
            if (n64 > UINT32_MAX) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; break; }
            uint32_t n = static_cast<uint32_t>(n64);
            size_t vwords = (n > 0u) ? ((static_cast<size_t>(n) + 63u) >> 6) : 1u;
            int64_t* vals = (n > 0u) ? ws.arena.alloc_array<int64_t>(n, 8) : nullptr;
            uint64_t* valid = ws.arena.alloc_array<uint64_t>(vwords, 8);
            auto* ov = static_cast<V11Vector*>(ws.arena.alloc(sizeof(V11Vector), alignof(V11Vector)));
            if ((n > 0u && !vals) || !valid || !ov) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; break; }
            std::memset(valid, 0, vwords * sizeof(uint64_t));
            for (uint32_t u = 0; u < n; ++u) {
                if (!fm || fm->contains_1bit(u)) {
                    vals[u] = (u + 1u < rel->row_off_len) ? static_cast<int64_t>(rel->csr_row_off(u + 1u) - rel->csr_row_off(u)) : 0;
                    valid[u >> 6] |= (1ULL << (u & 63u));
                }
            }
            new (ov) V11Vector();
            ov->elem_tag = kVecTagU64; ov->length = n; ov->i64_data = vals; ov->valid_bits = valid;
            ws.v_regs[dst] = ov;
            ws.state.pc += 1u;
            break;
        }
        case 3: { // .vec_rev_v
            if (rel->csc_row_off_raw == nullptr) { ws.status = IMPULSE_VM_V11_ERR_INVALID_OPERAND; break; }
            const V11Frontier* fm = nullptr;
            if ((flags & FLAG_MASKED_S3) != 0u) {
                fm = p3_get_f1bit(ws, s3);
                if (!fm) break;
                if (fm->domain_id != rel->tgt_dom) { ws.status = IMPULSE_VM_V11_ERR_DOMAIN_MISMATCH; break; }
            }
            uint64_t n64 = (rel->tgt_dom < snap->domain_count) ? snap->domain_cards[rel->tgt_dom] : 0ULL;
            if (n64 > UINT32_MAX) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; break; }
            uint32_t n = static_cast<uint32_t>(n64);
            size_t vwords = (n > 0u) ? ((static_cast<size_t>(n) + 63u) >> 6) : 1u;
            int64_t* vals = (n > 0u) ? ws.arena.alloc_array<int64_t>(n, 8) : nullptr;
            uint64_t* valid = ws.arena.alloc_array<uint64_t>(vwords, 8);
            auto* ov = static_cast<V11Vector*>(ws.arena.alloc(sizeof(V11Vector), alignof(V11Vector)));
            if ((n > 0u && !vals) || !valid || !ov) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; break; }
            std::memset(valid, 0, vwords * sizeof(uint64_t));
            for (uint32_t v = 0; v < n; ++v) {
                if (!fm || fm->contains_1bit(v)) {
                    vals[v] = (rel->csc_row_off_len > 0u && v < rel->csc_row_off_len - 1u)
                        ? static_cast<int64_t>(rel->csc_row_off(v + 1u) - rel->csc_row_off(v)) : 0;
                    valid[v >> 6] |= (1ULL << (v & 63u));
                }
            }
            new (ov) V11Vector();
            ov->elem_tag = kVecTagU64; ov->length = n; ov->i64_data = vals; ov->valid_bits = valid;
            ws.v_regs[dst] = ov;
            ws.state.pc += 1u;
            break;
        }
        case 4: { // .node_fwd_r
            if (!rel->has_csr && (rel->has_csc || rel->has_coo)) { ws.status = IMPULSE_VM_V11_ERR_INVALID_OPERAND; break; }
            if (!ws.is_r_valid(s1)) { ws.write_r_null(dst); p1_set_scalar_flags(ws, false, 0ULL); ws.state.pc += 1u; break; }
            uint64_t u64 = ws.read_r_raw(s1);
            uint64_t dcard = (rel->src_dom < snap->domain_count) ? snap->domain_cards[rel->src_dom] : 0ULL;
            if (u64 >= dcard) { ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; break; }
            uint32_t u = static_cast<uint32_t>(u64);
            uint64_t deg = (u + 1u < rel->row_off_len) ? static_cast<uint64_t>(rel->csr_row_off(u + 1u) - rel->csr_row_off(u)) : 0ULL;
            ws.write_r(dst, static_cast<uint8_t>(ScalarType::SCALAR_U64), deg);
            p1_set_scalar_flags(ws, true, deg);
            ws.state.pc += 1u;
            break;
        }
        case 5: { // .node_rev_r
            if (rel->csc_row_off_raw == nullptr) { ws.status = IMPULSE_VM_V11_ERR_INVALID_OPERAND; break; }
            if (!ws.is_r_valid(s1)) { ws.write_r_null(dst); p1_set_scalar_flags(ws, false, 0ULL); ws.state.pc += 1u; break; }
            uint64_t v64 = ws.read_r_raw(s1);
            uint64_t dcard = (rel->tgt_dom < snap->domain_count) ? snap->domain_cards[rel->tgt_dom] : 0ULL;
            if (v64 >= dcard) { ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; break; }
            uint32_t v = static_cast<uint32_t>(v64);
            uint64_t deg = (rel->csc_row_off_len > 0u && v < rel->csc_row_off_len - 1u)
                ? static_cast<uint64_t>(rel->csc_row_off(v + 1u) - rel->csc_row_off(v)) : 0ULL;
            ws.write_r(dst, static_cast<uint8_t>(ScalarType::SCALAR_U64), deg);
            p1_set_scalar_flags(ws, true, deg);
            ws.state.pc += 1u;
            break;
        }
        default:
            ws.status = IMPULSE_VM_V11_ERR_INVALID_OPCODE;
            break;
    }
    V11_DISPATCH();
}
