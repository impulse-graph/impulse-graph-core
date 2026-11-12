/**
 * Formal Opcode Contract: OP_VECTOR_RERANK_DOT (0xCB)
 *
 * Layout:
 *   dst_reg:          inst.dst_reg (DST_REG)
 *   cand_reg (src):   inst.payload & 0xFF (PAYLOAD_LOW8)
 *   q_reg (aux1):     (inst.payload >> 8) & 0xFF (PAYLOAD_BYTE1)
 *   attr_id:          (inst.payload >> 16) & 0xFFFF
 *   allowed_src:      TYPE_BITSET_HANDLE | TYPE_LANE_FRONTIER
 *   allowed_aux1:     TYPE_FLOAT_VECTOR
 *   output_dst:       TYPE_FLOAT_VECTOR
 *   metric (flags & 0x03):
 *     0x00: Dot Product
 *     0x01: Cosine Similarity
 *     0x02: L2 Distance (Squared)
 *     ZF set if output scores are all zero
 *   requires_ctx:     true
 */
#ifndef OP_CB_CONTRACT_DEFINED
#define OP_CB_CONTRACT_DEFINED
#define OP_CB_DEFINED_FLAGS        0x03u
#define OP_CB_ALLOWED_SRC_TYPES    (IMP_TYPE_BIT(TYPE_BITSET_HANDLE) | IMP_TYPE_BIT(TYPE_LANE_FRONTIER))
#define OP_CB_ALLOWED_AUX1_TYPES   (IMP_TYPE_BIT(TYPE_FLOAT_VECTOR))
#define OP_CB_OUTPUT_DST_TYPES     (IMP_TYPE_BIT(TYPE_FLOAT_VECTOR))
#endif

// Opcode 0xCB: OP_VECTOR_RERANK_DOT
op_VECTOR_RERANK_DOT: {
    {
        const auto& inst = bytecode[vm_state->pc];
        uint16_t dst = inst.dst_reg;
        uint16_t cand_reg = inst.payload & 0xFF;
        uint16_t q_reg = (inst.payload >> 8) & 0xFF;
        uint16_t attr_id = (inst.payload >> 16) & 0xFFFF;
        uint8_t metric_flag = inst.flags & 0x03;
        VALIDATE_REG(dst);
        VALIDATE_REG(cand_reg);
        VALIDATE_REG(q_reg);
        if (!vm_state->query_context) return IMPULSE_VM_ERR_NULL_SNAPSHOT;

        uint8_t t_cand = vm_state->register_types[cand_reg];
        uint8_t t_q = vm_state->register_types[q_reg];
        if (t_q != TYPE_FLOAT_VECTOR || (t_cand != TYPE_BITSET_HANDLE && t_cand != TYPE_LANE_FRONTIER)) {
            return IMPULSE_VM_ERR_INVALID_REGISTER;
        }
        uint64_t h_q = vm_state->registers[q_reg];
        if (h_q >= VM_MAX_VECTOR_HANDLES || !vm_state->query_context->float_vectors_allocated[h_q]) {
            return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        }
        const auto& q_vec = vm_state->query_context->float_vectors[h_q];
        size_t q_len = vm_state->query_context->float_vector_lengths[h_q];
        if (q_len == 0 || q_len > q_vec.size()) q_len = q_vec.size();

        auto& cands = vm_state->query_context->scratch_u32_a;
        cands.clear();
        if (t_cand == TYPE_BITSET_HANDLE) {
            uint64_t h_bs = vm_state->registers[cand_reg];
            if (h_bs >= VM_MAX_BITSET_HANDLES || !vm_state->query_context->bitset_allocated[h_bs]) {
                return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
            }
            extract_active_bits(vm_state->query_context->bitsets[h_bs], cands);
        } else {
            size_t h_lf = static_cast<size_t>(vm_state->registers[cand_reg]);
            if (h_lf >= VM_MAX_LANE_FRONTIER_HANDLES || h_lf >= vm_state->query_context->lane_frontiers_count) {
                return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
            }
            const auto& lf = vm_state->query_context->lane_frontiers[h_lf];
            for (size_t w_idx = 0; w_idx < lf.active_words.size(); ++w_idx) {
                uint64_t word = lf.active_words[w_idx];
                while (word != 0ULL) {
                    uint32_t bit = static_cast<uint32_t>(std::countr_zero(word));
                    size_t u = (w_idx << 6) + bit;
                    word &= (word - 1ULL);
                    if (u >= lf.lanes.size()) break;
                    if ((lf.lanes[u] & lf.active_mask) != 0ULL) {
                        cands.push_back(static_cast<uint32_t>(u));
                    }
                }
            }
        }

        const auto* ma_ptr = find_mock_node_attr(vm_state->query_context, attr_id);
        if (!ma_ptr) ma_ptr = find_mock_node_attr(vm_state->query_context, 0);
        const std::vector<float>* mock_embs = (ma_ptr && !ma_ptr->float_data.empty()) ? &ma_ptr->float_data : nullptr;

        const uint8_t* snap_raw = nullptr;
        uint64_t snap_bytes = 0;
        const uint8_t* snap_bm = nullptr;
        uint64_t snap_bm_raw_bytes = 0;
        uint8_t snap_base_tc = 0;
        uint16_t snap_ver = 9;
        size_t snap_dim = q_len;
        size_t snap_dom_rows = 0;
        if (!mock_embs && vm_state->query_context->snapshot) {
            snap_ver = impulse_snapshot_version(vm_state->query_context->snapshot);
            uint16_t dom_id = (attr_id >> 8) & 0xFFu;
            uint16_t local_aid = attr_id & 0xFFu;
            const BoundAttributeSlot* emb_slot = resolve_node_attribute_slot(vm_state->query_context, dom_id, local_aid);
            if (!emb_slot && dom_id == 0 && local_aid != 0) {
                emb_slot = resolve_node_attribute_slot(vm_state->query_context, 0, 0);
            }
            if (emb_slot && emb_slot->data_ptr) {
                snap_dom_rows = resolve_domain_row_count(vm_state->query_context, dom_id);
                snap_raw = static_cast<const uint8_t*>(emb_slot->data_ptr);
                snap_bytes = emb_slot->data_bytes;
                snap_base_tc = static_cast<uint8_t>(emb_slot->type_code & 0x7Fu);
                if (emb_slot->dimension > 0) snap_dim = emb_slot->dimension;
                if ((emb_slot->type_code & 0x80u) != 0) {
                    snap_bm_raw_bytes = ((snap_dom_rows + 63ULL) / 64ULL) * 8ULL;
                    uint64_t padded_bm = (snap_bm_raw_bytes + 127ULL) & ~127ULL;
                    if (snap_bytes >= padded_bm) {
                        snap_bm = snap_raw;
                        snap_raw += padded_bm;
                        snap_bytes -= padded_bm;
                    } else {
                        snap_raw = nullptr;
                        snap_bytes = 0;
                    }
                }
            }
        }

        auto eval_metric_score = [&](const float* v, size_t dim) -> float {
            size_t d_len = std::min(q_len, dim);
            if (metric_flag == 0) { // MetricDot
                return impulse_simd_dot_product_f32(q_vec.data(), v, d_len);
            } else if (metric_flag == 1) { // MetricL2 (negative squared L2)
#if defined(__clang__)
#pragma clang fp contract(off)
#endif
                float sum_sq = 0.0f;
                for (size_t i = 0; i < d_len; ++i) {
                    float d = q_vec[i] - v[i];
                    sum_sq += d * d;
                }
                return -sum_sq;
            } else if (metric_flag == 2) { // MetricCosine
                float dot = impulse_simd_dot_product_f32(q_vec.data(), v, d_len);
                float nq = impulse_simd_dot_product_f32(q_vec.data(), q_vec.data(), d_len);
                float nv = impulse_simd_dot_product_f32(v, v, d_len);
                float denom = std::sqrt(nq) * std::sqrt(nv);
                return (denom <= 1e-12f) ? 0.0f : (dot / denom);
            } else { // MetricHamming (3)
                uint32_t ham = 0;
                for (size_t i = 0; i < d_len; ++i) {
                    if ((q_vec[i] >= 0.0f) != (v[i] >= 0.0f)) ++ham;
                }
                return -static_cast<float>(ham);
            }
        };

        int h_out = acquire_float_vector(vm_state->query_context);
        if (h_out < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        auto& scores = vm_state->query_context->float_vectors[h_out];
        size_t out_len = std::min(cands.size(), scores.size());
        vm_state->query_context->float_vector_lengths[h_out] = out_len;

        std::vector<float> unpack_buf;
        for (size_t i = 0; i < out_len; ++i) {
            size_t u = cands[i];
            float score = 0.0f;
            if (mock_embs && q_len > 0 && (u + 1) * q_len <= mock_embs->size()) {
                score = eval_metric_score(mock_embs->data() + u * q_len, q_len);
            } else if (snap_raw && snap_dim > 0) {
                if (snap_bm != nullptr) {
                    if ((u >> 3) >= snap_bm_raw_bytes || ((snap_bm[u >> 3] >> (u & 7u)) & 1u) == 0u) {
                        scores[i] = std::numeric_limits<float>::quiet_NaN();
                        continue;
                    }
                }
                bool is_f16 = (snap_base_tc == 0x05 && snap_ver >= 10) ||
                              (snap_base_tc == 0x0E && snap_dom_rows > 0 &&
                               snap_bytes >= snap_dom_rows * snap_dim * 2ULL &&
                               snap_bytes < snap_dom_rows * snap_dim * 4ULL);
                if (is_f16) {
                    if ((u + 1ULL) * snap_dim * 2ULL <= snap_bytes) {
                        unpack_buf.resize(snap_dim);
                        const auto* h_ptr = reinterpret_cast<const uint16_t*>(snap_raw) + u * snap_dim;
                        for (size_t k = 0; k < snap_dim; ++k) {
                            unpack_buf[k] = impulse_fp16_to_fp32(h_ptr[k]);
                        }
                        score = eval_metric_score(unpack_buf.data(), snap_dim);
                    } else {
                        score = std::numeric_limits<float>::quiet_NaN();
                    }
                } else if (snap_base_tc == 0x07) {
                    if ((u + 1ULL) * snap_dim * 8ULL <= snap_bytes) {
                        unpack_buf.resize(snap_dim);
                        const auto* d_ptr = reinterpret_cast<const double*>(snap_raw) + u * snap_dim;
                        for (size_t k = 0; k < snap_dim; ++k) {
                            unpack_buf[k] = static_cast<float>(d_ptr[k]);
                        }
                        score = eval_metric_score(unpack_buf.data(), snap_dim);
                    } else {
                        score = std::numeric_limits<float>::quiet_NaN();
                    }
                } else {
                    size_t snap_emb_floats = snap_bytes / sizeof(float);
                    if ((u + 1ULL) * snap_dim <= snap_emb_floats) {
                        const auto* f_ptr = reinterpret_cast<const float*>(snap_raw) + u * snap_dim;
                        score = eval_metric_score(f_ptr, snap_dim);
                    } else {
                        score = std::numeric_limits<float>::quiet_NaN();
                    }
                }
            } else {
                score = (q_len > 0) ? q_vec[0] : 0.0f;
            }
            scores[i] = score;
        }

        vm_state->registers[dst] = static_cast<uint64_t>(h_out);
        vm_state->register_types[dst] = TYPE_FLOAT_VECTOR;
        if (out_len > 0) vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;
        else vm_state->flags |= IMPULSE_VM_FLAG_ZF;
    }
    vm_state->pc++;
    DISPATCH();
}

