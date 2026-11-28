// Opcode 0x58: OP_VEC_DECIMAL_ALU
VM_OP_CASE(VEC_DECIMAL_ALU) {
    VM_OP_CONTRACT_ASSERT(OP_VEC_DECIMAL_ALU, 0x3u, 0xFFFFu);
    const uint8_t subop = IMP_SUBOP(w);
    const uint8_t flags = IMP_FLAGS(w);
    const uint8_t dst = IMP_DST(w);
    const uint8_t s1 = IMP_S1(w);
    const uint8_t s2 = IMP_S2(w);
    const uint8_t s3 = IMP_S3(w);
    const uint16_t lo = IMP_IMM16_LO(w);
    const uint16_t hi = IMP_IMM16_HI(w);
    const bool scalar_rhs = (flags & FLAG_SCALAR_RHS) != 0u;

    switch (subop) {
        case subop::kVEC_DECIMAL_ALU_ADD_CHK:
        case subop::kVEC_DECIMAL_ALU_SUB_CHK:
        case subop::kVEC_DECIMAL_ALU_MUL_CHK:
        case subop::kVEC_DECIMAL_ALU_DIV_ROUND:
        case subop::kVEC_DECIMAL_ALU_DIV_SAFE: {
            const VmFrontier* mask = p4_get_mask_frontier(ws, flags, s3);
            if (ws.status != IMPULSE_VM_OK) VM_DISPATCH();
            const VmVector* va = ws.v_regs[s1];
            if (!va || va->elem_tag != kVecTagI64) { ws.status = IMPULSE_VM_ERR_TYPE_MISMATCH; VM_DISPATCH(); }
            const uint32_t n = va->length;
            const VmVector* vb = nullptr;
            if (!scalar_rhs) {
                vb = ws.v_regs[s2];
                if (!vb || vb->elem_tag != kVecTagI64) { ws.status = IMPULSE_VM_ERR_TYPE_MISMATCH; VM_DISPATCH(); }
                if (vb->length != n) { ws.status = IMPULSE_VM_ERR_OUT_OF_BOUNDS; VM_DISPATCH(); }
            }
            const bool r_valid = scalar_rhs && ws.is_r_valid(s2);
            const int64_t r_val = r_valid ? static_cast<int64_t>(ws.read_r_raw(s2)) : 0LL;

            const size_t vwords = (n > 0u) ? ((static_cast<size_t>(n) + 63u) >> 6) : 1u;
            int64_t* out = (n > 0u) ? ws.arena.alloc_array<int64_t>(n, 8) : nullptr;
            uint64_t* out_valid = ws.arena.alloc_array<uint64_t>(vwords, 8);
            auto* ov = static_cast<VmVector*>(ws.arena.alloc(sizeof(VmVector), alignof(VmVector)));
            if ((n > 0u && !out) || !out_valid || !ov) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
            if (n > 0u) std::memset(out, 0, static_cast<size_t>(n) * sizeof(int64_t));
            std::memset(out_valid, 0, vwords * sizeof(uint64_t));

            for (uint32_t i = 0; i < n; ++i) {
                if (!p4_lane_active(mask, i)) continue;
                if (!va->is_valid_at(i)) continue;
                if (scalar_rhs ? !r_valid : !vb->is_valid_at(i)) continue;
                const int64_t a = va->i64_data[i];
                const int64_t b = scalar_rhs ? r_val : vb->i64_data[i];
                int64_t res = 0LL;
                bool lane_ok = true;
                impulse_vm_status_t st = IMPULSE_VM_OK;
                if (subop == subop::kVEC_DECIMAL_ALU_ADD_CHK) st = dec_add_chk(a, b, &res);
                else if (subop == subop::kVEC_DECIMAL_ALU_SUB_CHK) st = dec_sub_chk(a, b, &res);
                else if (subop == subop::kVEC_DECIMAL_ALU_MUL_CHK) st = dec_mul_chk(a, b, &res);
                else if (subop == subop::kVEC_DECIMAL_ALU_DIV_ROUND) st = dec_div_round(a, b, &res);
                else st = dec_div_safe(a, b, &res, &lane_ok);
                if (st != IMPULSE_VM_OK) { ws.status = st; VM_DISPATCH(); }
                if (lane_ok) {
                    out[i] = res;
                    out_valid[i >> 6] |= (1ULL << (i & 63u));
                }
            }
            new (ov) VmVector();
            ov->elem_tag = kVecTagI64; ov->length = n; ov->i64_data = out; ov->valid_bits = out_valid;
            ws.v_regs[dst] = ov;
            ws.state.pc += 1u;
            break;
        }
        case subop::kVEC_DECIMAL_ALU_MUL_DIV_R:
        case subop::kVEC_DECIMAL_ALU_MUL_DIV_V:
        case subop::kVEC_DECIMAL_ALU_MUL_DIV_SAFE_R: {
            const VmVector* va = ws.v_regs[s1];
            if (!va || va->elem_tag != kVecTagI64) { ws.status = IMPULSE_VM_ERR_TYPE_MISMATCH; VM_DISPATCH(); }
            const uint32_t n = va->length;
            const VmVector* vb = nullptr;
            if (!scalar_rhs) {
                vb = ws.v_regs[s2];
                if (!vb || vb->elem_tag != kVecTagI64) { ws.status = IMPULSE_VM_ERR_TYPE_MISMATCH; VM_DISPATCH(); }
                if (vb->length != n) { ws.status = IMPULSE_VM_ERR_OUT_OF_BOUNDS; VM_DISPATCH(); }
            }
            const bool r_b_valid = scalar_rhs && ws.is_r_valid(s2);
            const int64_t r_b_val = r_b_valid ? static_cast<int64_t>(ws.read_r_raw(s2)) : 0LL;

            const bool div_is_vec = (subop == subop::kVEC_DECIMAL_ALU_MUL_DIV_V);
            const VmVector* vd = nullptr;
            if (div_is_vec) {
                vd = ws.v_regs[s3];
                if (!vd || vd->elem_tag != kVecTagI64) { ws.status = IMPULSE_VM_ERR_TYPE_MISMATCH; VM_DISPATCH(); }
                if (vd->length != n) { ws.status = IMPULSE_VM_ERR_OUT_OF_BOUNDS; VM_DISPATCH(); }
            }
            const bool r_d_valid = !div_is_vec && ws.is_r_valid(s3);
            const int64_t r_d_val = r_d_valid ? static_cast<int64_t>(ws.read_r_raw(s3)) : 0LL;

            const size_t vwords = (n > 0u) ? ((static_cast<size_t>(n) + 63u) >> 6) : 1u;
            int64_t* out = (n > 0u) ? ws.arena.alloc_array<int64_t>(n, 8) : nullptr;
            uint64_t* out_valid = ws.arena.alloc_array<uint64_t>(vwords, 8);
            auto* ov = static_cast<VmVector*>(ws.arena.alloc(sizeof(VmVector), alignof(VmVector)));
            if ((n > 0u && !out) || !out_valid || !ov) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
            if (n > 0u) std::memset(out, 0, static_cast<size_t>(n) * sizeof(int64_t));
            std::memset(out_valid, 0, vwords * sizeof(uint64_t));

            const bool is_safe = (subop == subop::kVEC_DECIMAL_ALU_MUL_DIV_SAFE_R);
            for (uint32_t i = 0; i < n; ++i) {
                if (!va->is_valid_at(i)) continue;
                if (scalar_rhs ? !r_b_valid : !vb->is_valid_at(i)) continue;
                if (div_is_vec ? !vd->is_valid_at(i) : !r_d_valid) continue;
                const int64_t a = va->i64_data[i];
                const int64_t b = scalar_rhs ? r_b_val : vb->i64_data[i];
                const int64_t d = div_is_vec ? vd->i64_data[i] : r_d_val;
                int64_t res = 0LL;
                impulse_vm_status_t st = dec_mul_div_round(a, b, d, &res);
                if (st == IMPULSE_VM_OK) {
                    out[i] = res;
                    out_valid[i >> 6] |= (1ULL << (i & 63u));
                } else if (!is_safe) {
                    ws.status = st;
                    VM_DISPATCH();
                }
            }
            new (ov) VmVector();
            ov->elem_tag = kVecTagI64; ov->length = n; ov->i64_data = out; ov->valid_bits = out_valid;
            ws.v_regs[dst] = ov;
            ws.state.pc += 1u;
            break;
        }
        case subop::kVEC_DECIMAL_ALU_F64_TO_DEC: {
            if (lo > 18u || hi < 1u || hi > 18u) { ws.status = IMPULSE_VM_ERR_INVALID_OPERAND; VM_DISPATCH(); }
            const VmFrontier* mask = p4_get_mask_frontier(ws, flags, s3);
            if (ws.status != IMPULSE_VM_OK) VM_DISPATCH();
            const VmVector* va = ws.v_regs[s1];
            if (!va || va->elem_tag != kVecTagF64) { ws.status = IMPULSE_VM_ERR_TYPE_MISMATCH; VM_DISPATCH(); }
            const uint32_t n = va->length;
            const size_t vwords = (n > 0u) ? ((static_cast<size_t>(n) + 63u) >> 6) : 1u;
            int64_t* out = (n > 0u) ? ws.arena.alloc_array<int64_t>(n, 8) : nullptr;
            uint64_t* out_valid = ws.arena.alloc_array<uint64_t>(vwords, 8);
            auto* ov = static_cast<VmVector*>(ws.arena.alloc(sizeof(VmVector), alignof(VmVector)));
            if ((n > 0u && !out) || !out_valid || !ov) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
            if (n > 0u) std::memset(out, 0, static_cast<size_t>(n) * sizeof(int64_t));
            std::memset(out_valid, 0, vwords * sizeof(uint64_t));

            for (uint32_t i = 0; i < n; ++i) {
                if (!p4_lane_active(mask, i)) continue;
                if (!va->is_valid_at(i)) continue;
                int64_t res = 0LL;
                bool lane_ok = false;
                impulse_vm_status_t st = dec_f64_to_dec(va->f64_data[i], lo, hi, &res, &lane_ok);
                if (st != IMPULSE_VM_OK) { ws.status = st; VM_DISPATCH(); }
                if (lane_ok) {
                    out[i] = res;
                    out_valid[i >> 6] |= (1ULL << (i & 63u));
                }
            }
            new (ov) VmVector();
            ov->elem_tag = kVecTagI64; ov->length = n; ov->i64_data = out; ov->valid_bits = out_valid;
            ws.v_regs[dst] = ov;
            ws.state.pc += 1u;
            break;
        }
        case subop::kVEC_DECIMAL_ALU_DEC_TO_F64: {
            if (lo > 18u) { ws.status = IMPULSE_VM_ERR_INVALID_OPERAND; VM_DISPATCH(); }
            const VmFrontier* mask = p4_get_mask_frontier(ws, flags, s3);
            if (ws.status != IMPULSE_VM_OK) VM_DISPATCH();
            const VmVector* va = ws.v_regs[s1];
            if (!va || va->elem_tag != kVecTagI64) { ws.status = IMPULSE_VM_ERR_TYPE_MISMATCH; VM_DISPATCH(); }
            const uint32_t n = va->length;
            const size_t vwords = (n > 0u) ? ((static_cast<size_t>(n) + 63u) >> 6) : 1u;
            double* out = (n > 0u) ? ws.arena.alloc_array<double>(n, 8) : nullptr;
            uint64_t* out_valid = ws.arena.alloc_array<uint64_t>(vwords, 8);
            auto* ov = static_cast<VmVector*>(ws.arena.alloc(sizeof(VmVector), alignof(VmVector)));
            if ((n > 0u && !out) || !out_valid || !ov) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
            if (n > 0u) std::memset(out, 0, static_cast<size_t>(n) * sizeof(double));
            std::memset(out_valid, 0, vwords * sizeof(uint64_t));

            for (uint32_t i = 0; i < n; ++i) {
                if (!p4_lane_active(mask, i)) continue;
                if (!va->is_valid_at(i)) continue;
                double f = 0.0;
                if (dec_to_f64(va->i64_data[i], lo, &f) == IMPULSE_VM_OK && !std::isnan(f)) {
                    out[i] = f;
                    out_valid[i >> 6] |= (1ULL << (i & 63u));
                }
            }
            new (ov) VmVector();
            ov->elem_tag = kVecTagF64; ov->length = n; ov->f64_data = out; ov->valid_bits = out_valid;
            ws.v_regs[dst] = ov;
            ws.state.pc += 1u;
            break;
        }
        case subop::kVEC_DECIMAL_ALU_RESCALE:
        case subop::kVEC_DECIMAL_ALU_BOUND_CHK:
        case subop::kVEC_DECIMAL_ALU_ABS_CHK:
        case subop::kVEC_DECIMAL_ALU_NEG_CHK:
        case subop::kVEC_DECIMAL_ALU_ROUND_SCALE:
        case subop::kVEC_DECIMAL_ALU_TRUNC_SCALE: {
            bool imm_ok = true;
            if (subop == subop::kVEC_DECIMAL_ALU_RESCALE) imm_ok = (lo <= 18u && (hi == 0u || hi == 1u));
            else if (subop == subop::kVEC_DECIMAL_ALU_BOUND_CHK) imm_ok = (lo >= 1u && lo <= 18u);
            else if (subop == subop::kVEC_DECIMAL_ALU_ROUND_SCALE || subop == subop::kVEC_DECIMAL_ALU_TRUNC_SCALE) imm_ok = (lo <= 18u);
            if (!imm_ok) { ws.status = IMPULSE_VM_ERR_INVALID_OPERAND; VM_DISPATCH(); }

            const VmFrontier* mask = p4_get_mask_frontier(ws, flags, s3);
            if (ws.status != IMPULSE_VM_OK) VM_DISPATCH();
            const VmVector* va = ws.v_regs[s1];
            if (!va || va->elem_tag != kVecTagI64) { ws.status = IMPULSE_VM_ERR_TYPE_MISMATCH; VM_DISPATCH(); }
            const uint32_t n = va->length;
            const size_t vwords = (n > 0u) ? ((static_cast<size_t>(n) + 63u) >> 6) : 1u;
            int64_t* out = (n > 0u) ? ws.arena.alloc_array<int64_t>(n, 8) : nullptr;
            uint64_t* out_valid = ws.arena.alloc_array<uint64_t>(vwords, 8);
            auto* ov = static_cast<VmVector*>(ws.arena.alloc(sizeof(VmVector), alignof(VmVector)));
            if ((n > 0u && !out) || !out_valid || !ov) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; VM_DISPATCH(); }
            if (n > 0u) std::memset(out, 0, static_cast<size_t>(n) * sizeof(int64_t));
            std::memset(out_valid, 0, vwords * sizeof(uint64_t));

            for (uint32_t i = 0; i < n; ++i) {
                if (!p4_lane_active(mask, i)) continue;
                if (!va->is_valid_at(i)) continue;
                const int64_t a = va->i64_data[i];
                int64_t res = 0LL;
                impulse_vm_status_t st = IMPULSE_VM_OK;
                if (subop == subop::kVEC_DECIMAL_ALU_RESCALE) st = dec_rescale(a, lo, hi, &res);
                else if (subop == subop::kVEC_DECIMAL_ALU_BOUND_CHK) st = dec_bound_chk(a, lo, &res);
                else if (subop == subop::kVEC_DECIMAL_ALU_ABS_CHK) st = dec_abs_chk(a, &res);
                else if (subop == subop::kVEC_DECIMAL_ALU_NEG_CHK) st = dec_neg_chk(a, &res);
                else if (subop == subop::kVEC_DECIMAL_ALU_ROUND_SCALE) st = dec_round_scale(a, lo, &res);
                else st = dec_trunc_scale(a, lo, &res);
                if (st != IMPULSE_VM_OK) { ws.status = st; VM_DISPATCH(); }
                out[i] = res;
                out_valid[i >> 6] |= (1ULL << (i & 63u));
            }
            new (ov) VmVector();
            ov->elem_tag = kVecTagI64; ov->length = n; ov->i64_data = out; ov->valid_bits = out_valid;
            ws.v_regs[dst] = ov;
            ws.state.pc += 1u;
            break;
        }
        default:
            ws.status = IMPULSE_VM_ERR_INVALID_OPCODE;
            break;
    }
    VM_DISPATCH();
}
