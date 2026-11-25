// Opcode 0x4C: OP_VEC_INT_BITWISE
V11_OP_CASE(VEC_INT_BITWISE) {
    const uint8_t subop = IMP_V11_SUBOP(w);
    const uint8_t flags = IMP_V11_FLAGS(w);
    const uint8_t dst = IMP_V11_DST(w);
    const uint8_t s1 = IMP_V11_S1(w);
    const uint8_t s2 = IMP_V11_S2(w);
    const uint8_t s3 = IMP_V11_S3(w);

    const V11Frontier* mask = p4_get_mask_frontier(ws, flags, s3);
    if (ws.status != IMPULSE_VM_V11_OK) V11_DISPATCH();
    const V11Vector* va = ws.v_regs[s1];
    if (!va || (va->elem_tag != kVecTagI64 && va->elem_tag != kVecTagU64)) {
        ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; V11_DISPATCH();
    }

    const uint32_t n = va->length;
    const bool is_unary = (subop == 3u || subop == 10u || subop == 11u || subop == 12u || subop == 13u);
    const bool scalar_rhs = (flags & FLAG_SCALAR_RHS) != 0u;
    const V11Vector* vb = nullptr;
    if (!is_unary && !scalar_rhs) {
        vb = ws.v_regs[s2];
        if (!vb || (vb->elem_tag != kVecTagI64 && vb->elem_tag != kVecTagU64)) {
            ws.status = IMPULSE_VM_V11_ERR_TYPE_MISMATCH; V11_DISPATCH();
        }
        if (vb->length != n) { ws.status = IMPULSE_VM_V11_ERR_OUT_OF_BOUNDS; V11_DISPATCH(); }
    }
    const bool s_valid = !is_unary && scalar_rhs && ws.is_r_valid(s2);
    const uint64_t s_val = s_valid ? ws.read_r_raw(s2) : 0ULL;

    const size_t vwords = (n > 0u) ? ((static_cast<size_t>(n) + 63u) >> 6) : 1u;
    int64_t* out = (n > 0u) ? ws.arena.alloc_array<int64_t>(n, 8) : nullptr;
    uint64_t* out_valid = ws.arena.alloc_array<uint64_t>(vwords, 8);
    auto* ov = static_cast<V11Vector*>(ws.arena.alloc(sizeof(V11Vector), alignof(V11Vector)));
    if ((n > 0u && !out) || !out_valid || !ov) { ws.status = IMPULSE_VM_V11_ERR_BUFFER_OVERFLOW; V11_DISPATCH(); }
    if (n > 0u) std::memset(out, 0, static_cast<size_t>(n) * sizeof(int64_t));
    std::memset(out_valid, 0, vwords * sizeof(uint64_t));

    for (uint32_t i = 0; i < n; ++i) {
        if (!p4_lane_active(mask, i) || !va->is_valid_at(i)) continue;
        uint64_t a = static_cast<uint64_t>(va->i64_data[i]);
        uint64_t res = 0ULL;
        if (is_unary) {
            switch (subop) {
                case 3:  res = ~a; break;
                case 10: res = static_cast<uint64_t>(std::popcount(a)); break;
                case 11: res = static_cast<uint64_t>(std::countl_zero(a)); break;
                case 12: res = static_cast<uint64_t>(std::countr_zero(a)); break;
                case 13: res = p4_bswap64(a); break;
                default: break;
            }
        } else {
            if (scalar_rhs ? !s_valid : !vb->is_valid_at(i)) continue;
            uint64_t b = scalar_rhs ? s_val : static_cast<uint64_t>(vb->i64_data[i]);
            unsigned sh = static_cast<unsigned>(b & 63ULL);
            switch (subop) {
                case 0: res = a & b; break;
                case 1: res = a | b; break;
                case 2: res = a ^ b; break;
                case 4: res = a & ~b; break;
                case 5: res = a << sh; break;
                case 6: res = a >> sh; break;
                case 7: res = static_cast<uint64_t>(static_cast<int64_t>(a) >> sh); break;
                case 8: res = std::rotl(a, static_cast<int>(sh)); break;
                case 9: res = std::rotr(a, static_cast<int>(sh)); break;
                default: break;
            }
        }
        out[i] = static_cast<int64_t>(res);
        out_valid[i >> 6] |= (1ULL << (i & 63u));
    }
    new (ov) V11Vector();
    ov->elem_tag = va->elem_tag; ov->length = n; ov->i64_data = out; ov->valid_bits = out_valid;
    ws.v_regs[dst] = ov;
    ws.state.pc += 1u;
    V11_DISPATCH();
}
