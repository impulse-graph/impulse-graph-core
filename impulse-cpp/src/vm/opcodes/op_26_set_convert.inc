// Opcode 0x26: OP_SET_CONVERT
VM_OP_CASE(SET_CONVERT) {
    const uint8_t subop = IMP_SUBOP(w);
    const bool accum = (IMP_FLAGS(w) & FLAG_ACCUM) != 0u;
    const uint8_t dst = IMP_DST(w);
    const uint8_t s1 = IMP_S1(w);
    const uint16_t dom_id = IMP_IMM16_LO(w);

    switch (subop) {
        case 0: { // .f2v_ids
            if (!ws.is_f_init(s1) || ws.f_regs[s1].multi_lane) { ws.status = IMPULSE_VM_ERR_TYPE_MISMATCH; break; }
            const uint32_t* ids = nullptr;
            uint32_t n = 0u;
            if (!ws.f_regs[s1].materialize_1bit(ws.arena, &ids, &n)) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; break; }
            ws.f_regs[s1].cached_card = static_cast<int64_t>(n);
            auto* vec = static_cast<VmVector*>(ws.arena.alloc(sizeof(VmVector), alignof(VmVector)));
            if (!vec) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; break; }
            new (vec) VmVector();
            vec->elem_tag = kVecTagU64;
            vec->length = n;
            if (n > 0u) {
                vec->i64_data = ws.arena.alloc_array<int64_t>(n, 8);
                if (!vec->i64_data) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; break; }
                for (uint32_t i = 0; i < n; ++i) vec->i64_data[i] = static_cast<int64_t>(ids[i]);
            }
            vec->valid_bits = nullptr;
            ws.v_regs[dst] = vec;
            ws.set_zf(n == 0u);
            ws.state.pc += 1u;
            break;
        }
        case 1: { // .v2f_ids
            uint64_t dom_card = 0;
            impulse_vm_status_t st = vm_lookup_domain_card(snap, dom_id, &dom_card);
            if (st != IMPULSE_VM_OK) { ws.status = st; break; }
            const VmVector* vec = ws.v_regs[s1];
            if (!vec || (vec->elem_tag != kVecTagI64 && vec->elem_tag != kVecTagU64)) { ws.status = IMPULSE_VM_ERR_TYPE_MISMATCH; break; }
            const uint32_t* base = nullptr;
            uint32_t base_len = 0u;
            if (accum) {
                if (!ws.is_f_init(dst)) { ws.status = IMPULSE_VM_ERR_TYPE_MISMATCH; break; }
                const VmFrontier& ex = ws.f_regs[dst];
                if (ex.domain_id != dom_id) { ws.status = IMPULSE_VM_ERR_DOMAIN_MISMATCH; break; }
                if (ex.multi_lane) { ws.status = IMPULSE_VM_ERR_TYPE_MISMATCH; break; }
                if (!ex.materialize_1bit(ws.arena, &base, &base_len)) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; break; }
            }
            size_t cap = static_cast<size_t>(base_len) + static_cast<size_t>(vec->length);
            if (cap > UINT32_MAX) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; break; }
            uint32_t* buf = (cap > 0u) ? ws.arena.alloc_array<uint32_t>(cap, 8) : nullptr;
            if (cap > 0u && !buf) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; break; }
            if (base_len > 0u) std::memcpy(buf, base, static_cast<size_t>(base_len) * sizeof(uint32_t));
            uint32_t cnt = base_len;
            bool oob = false;
            for (uint32_t i = 0; i < vec->length; ++i) {
                if (!vec->is_valid_at(i)) continue;
                uint64_t u = static_cast<uint64_t>(vec->i64_data[i]);
                if (u >= dom_card || u > UINT32_MAX) { oob = true; break; }
                buf[cnt++] = static_cast<uint32_t>(u);
            }
            if (oob) { ws.status = IMPULSE_VM_ERR_OUT_OF_BOUNDS; break; }
            uint32_t uniq = p2_dedupe_sorted_inplace(buf, cnt);
            if (!p2_set_f1bit(ws, dst, dom_id, dom_card, buf, uniq, false, true)) break;
            ws.set_zf(uniq == 0u);
            ws.state.pc += 1u;
            break;
        }
        case 2: { // .b2f
            const VmVector* vec = ws.v_regs[s1];
            if (!vec || vec->elem_tag != kVecTagBool3vl) { ws.status = IMPULSE_VM_ERR_TYPE_MISMATCH; break; }
            uint64_t dom_card = 0;
            if (snap != nullptr) {
                if (!snap->get_domain_card(dom_id, &dom_card)) { ws.status = IMPULSE_VM_ERR_OUT_OF_BOUNDS; break; }
            } else {
                dom_card = static_cast<uint64_t>(vec->length);
            }
            if (static_cast<uint64_t>(vec->length) > dom_card) { ws.status = IMPULSE_VM_ERR_OUT_OF_BOUNDS; break; }
            const uint32_t* base = nullptr;
            uint32_t base_len = 0u;
            if (accum) {
                if (!ws.is_f_init(dst)) { ws.status = IMPULSE_VM_ERR_TYPE_MISMATCH; break; }
                const VmFrontier& ex = ws.f_regs[dst];
                if (ex.domain_id != dom_id) { ws.status = IMPULSE_VM_ERR_DOMAIN_MISMATCH; break; }
                if (ex.multi_lane) { ws.status = IMPULSE_VM_ERR_TYPE_MISMATCH; break; }
                if (!ex.materialize_1bit(ws.arena, &base, &base_len)) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; break; }
            }
            size_t cap = static_cast<size_t>(base_len) + static_cast<size_t>(vec->length);
            if (cap > UINT32_MAX) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; break; }
            uint32_t* buf = (cap > 0u) ? ws.arena.alloc_array<uint32_t>(cap, 8) : nullptr;
            if (cap > 0u && !buf) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; break; }
            if (base_len > 0u) std::memcpy(buf, base, static_cast<size_t>(base_len) * sizeof(uint32_t));
            uint32_t cnt = base_len;
            for (uint32_t i = 0; i < vec->length; ++i) {
                if (vec->is_valid_at(i) && vec->bool_data_at(i)) buf[cnt++] = i;
            }
            uint32_t uniq = p2_dedupe_sorted_inplace(buf, cnt);
            if (!p2_set_f1bit(ws, dst, dom_id, dom_card, buf, uniq, false, true)) break;
            ws.set_zf(uniq == 0u);
            ws.state.pc += 1u;
            break;
        }
        case 3: { // .f2b
            if (!ws.is_f_init(s1) || ws.f_regs[s1].multi_lane) { ws.status = IMPULSE_VM_ERR_TYPE_MISMATCH; break; }
            const VmFrontier& fa = ws.f_regs[s1];
            if (fa.domain_card > UINT32_MAX) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; break; }
            uint32_t n = static_cast<uint32_t>(fa.domain_card);
            size_t words_n = (n > 0u) ? ((static_cast<size_t>(n) + 63u) >> 6) : 1u;
            auto* vec = static_cast<VmVector*>(ws.arena.alloc(sizeof(VmVector), alignof(VmVector)));
            uint64_t* valid = ws.arena.alloc_array<uint64_t>(words_n, 8);
            uint64_t* data  = ws.arena.alloc_array<uint64_t>(words_n, 8);
            if (!vec || !valid || !data) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; break; }
            new (vec) VmVector();
            std::memset(valid, 0, words_n * sizeof(uint64_t));
            std::memset(data, 0, words_n * sizeof(uint64_t));
            for (uint32_t i = 0; i < n; ++i) valid[i >> 6] |= (1ULL << (i & 63u));
            const uint32_t* ids = nullptr;
            uint32_t id_len = 0u;
            if (!fa.materialize_1bit(ws.arena, &ids, &id_len)) { ws.status = IMPULSE_VM_ERR_BUFFER_OVERFLOW; break; }
            for (uint32_t k = 0; k < id_len; ++k) {
                uint32_t u = ids[k];
                data[u >> 6] |= (1ULL << (u & 63u));
            }
            vec->elem_tag = kVecTagBool3vl;
            vec->length = n;
            vec->valid_bits = valid;
            vec->bool_data_bits = data;
            ws.v_regs[dst] = vec;
            ws.set_zf(id_len == 0u);
            ws.state.pc += 1u;
            break;
        }
        default:
            ws.status = IMPULSE_VM_ERR_INVALID_OPCODE;
            break;
    }
    VM_DISPATCH();
}
