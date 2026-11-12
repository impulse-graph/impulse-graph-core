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
#define OP_CB_VEC_METRIC_HAMMING   3u
#define OP_CB_SNAP_VER_V09         9u
#define OP_CB_SNAP_VER_V10         10u
#define OP_CB_COSINE_DENOM_EPS     1e-12f
#define OP_CB_FP16_ELEM_BYTES      2ULL
#define OP_CB_FP32_ELEM_BYTES      4ULL
#define OP_CB_FP64_ELEM_BYTES      8ULL
#define OP_CB_ALLOWED_SRC_TYPES    (IMP_TYPE_BIT(TYPE_BITSET_HANDLE) | IMP_TYPE_BIT(TYPE_LANE_FRONTIER))
#define OP_CB_ALLOWED_AUX1_TYPES   (IMP_TYPE_BIT(TYPE_FLOAT_VECTOR))
#define OP_CB_OUTPUT_DST_TYPES     (IMP_TYPE_BIT(TYPE_FLOAT_VECTOR))
#endif

// Opcode 0xCB: OP_VECTOR_RERANK_DOT
op_VECTOR_RERANK_DOT: {
    {
        const auto& inst = bytecode[vm_state->pc];
        uint16_t dst = IMP_INST_DST(inst);
        uint16_t cand_reg = IMP_INST_U8_0(inst);
        uint16_t q_reg = IMP_INST_U8_1(inst);
        uint16_t attr_id = IMP_INST_HIGH16(inst);
        uint8_t metric_flag = static_cast<uint8_t>(IMP_INST_FLAGS(inst) & IMP_FLAG_LOW2_MASK);
        IMP_VALIDATE_GPR(dst);
        IMP_VALIDATE_GPR(cand_reg);
        IMP_VALIDATE_GPR(q_reg);
        IMP_REQUIRE_CONTEXT(vm_state);
        IMPULSE_ASSERT(vm_state->query_context != nullptr);

        uint8_t t_cand = vm_state->register_types[cand_reg];
        uint8_t t_q = vm_state->register_types[q_reg];
        if (t_q != TYPE_FLOAT_VECTOR || (t_cand != TYPE_BITSET_HANDLE && t_cand != TYPE_LANE_FRONTIER)) {
            return IMPULSE_VM_ERR_INVALID_REGISTER;
        }
        uint64_t h_q = vm_state->registers[q_reg];
        IMPULSE_TESTCASE(h_q >= VM_MAX_VECTOR_HANDLES || !vm_state->query_context->float_vectors_allocated[h_q]);
        if (h_q >= VM_MAX_VECTOR_HANDLES || !vm_state->query_context->float_vectors_allocated[h_q]) {
            return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        }
        const auto& q_vec = vm_state->query_context->float_vectors[h_q];
        size_t q_len = vm_state->query_context->float_vector_lengths[h_q];
        if (q_len == 0 || q_len > q_vec.size()) q_len = q_vec.size();

        IMPULSE_TESTCASE(t_cand == TYPE_LANE_FRONTIER);
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
                    size_t u = static_cast<size_t>(IMP_WORD_BIT_TO_NODE(w_idx, bit));
                    word = IMP_CLEAR_LOWEST_BIT(word);
                    if (u >= lf.lanes.size()) break;
                    if ((lf.lanes[u] & lf.active_mask) != 0ULL) {
                        cands.push_back(static_cast<uint32_t>(u));
                    }
                }
            }
        }
        IMPULSE_TESTCASE(cands.empty());

        const auto* ma_ptr = find_mock_node_attr(vm_state->query_context, attr_id);
        if (!ma_ptr) ma_ptr = find_mock_node_attr(vm_state->query_context, 0);
        const std::vector<float>* mock_embs = (ma_ptr && !ma_ptr->float_data.empty()) ? &ma_ptr->float_data : nullptr;

        const uint8_t* snap_raw = nullptr;
        uint64_t snap_bytes = 0;
        const uint8_t* snap_bm = nullptr;
        uint64_t snap_bm_raw_bytes = 0;
        uint8_t snap_base_tc = 0;
        uint16_t snap_ver = OP_CB_SNAP_VER_V09;
        size_t snap_dim = q_len;
        size_t snap_dom_rows = 0;
        if (!mock_embs && vm_state->query_context->snapshot) {
            snap_ver = impulse_snapshot_version(vm_state->query_context->snapshot);
            uint16_t dom_id = IMP_U32_BYTE1(attr_id);
            uint16_t local_aid = IMP_U32_BYTE0(attr_id);
            const BoundAttributeSlot* emb_slot = resolve_node_attribute_slot(vm_state->query_context, dom_id, local_aid);
            if (!emb_slot && dom_id == 0 && local_aid != 0) {
                emb_slot = resolve_node_attribute_slot(vm_state->query_context, 0, 0);
            }
            if (emb_slot && emb_slot->data_ptr) {
                snap_dom_rows = resolve_domain_row_count(vm_state->query_context, dom_id);
                snap_raw = static_cast<const uint8_t*>(emb_slot->data_ptr);
                snap_bytes = emb_slot->data_bytes;
                snap_base_tc = IMP_ATTR_BASE_TYPE(emb_slot->type_code);
                if (emb_slot->dimension > 0) snap_dim = emb_slot->dimension;
                const bool is_nullable_emb = IMP_ATTR_IS_NULLABLE(emb_slot->type_code);
                IMPULSE_TESTCASE(is_nullable_emb);
                if (is_nullable_emb) {
                    snap_bm_raw_bytes = IMP_VALIDITY_RAW_BYTES(snap_dom_rows);
                    uint64_t padded_bm = IMP_VALIDITY_PADDED_BYTES(snap_dom_rows);
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
        IMPULSE_WELL_FORMED_ASSERT(!snap_raw || snap_dim > 0 || q_len == 0);

        IMPULSE_TESTCASE(metric_flag == IMP_VEC_METRIC_DOT);
        IMPULSE_TESTCASE(metric_flag == IMP_VEC_METRIC_NEG_L2_SQ);
        IMPULSE_TESTCASE(metric_flag == IMP_VEC_METRIC_COSINE);
        IMPULSE_TESTCASE(metric_flag == OP_CB_VEC_METRIC_HAMMING);

        auto eval_metric_score = [&](const float* v, size_t dim) -> float {
            size_t d_len = std::min(q_len, dim);
            if (metric_flag == IMP_VEC_METRIC_DOT) {
                return impulse_simd_dot_product_f32(q_vec.data(), v, d_len);
            } else if (metric_flag == IMP_VEC_METRIC_NEG_L2_SQ) {
#if defined(__clang__)
#pragma clang fp contract(off)
#endif
                float sum_sq = 0.0f;
                for (size_t i = 0; i < d_len; ++i) {
                    float d = q_vec[i] - v[i];
                    sum_sq += d * d;
                }
                return -sum_sq;
            } else if (metric_flag == IMP_VEC_METRIC_COSINE) {
                float dot = impulse_simd_dot_product_f32(q_vec.data(), v, d_len);
                float nq = impulse_simd_dot_product_f32(q_vec.data(), q_vec.data(), d_len);
                float nv = impulse_simd_dot_product_f32(v, v, d_len);
                float denom = std::sqrt(nq) * std::sqrt(nv);
                return (denom <= OP_CB_COSINE_DENOM_EPS) ? 0.0f : (dot / denom);
            } else {
                uint32_t ham = 0;
                for (size_t i = 0; i < d_len; ++i) {
                    if ((q_vec[i] >= 0.0f) != (v[i] >= 0.0f)) ++ham;
                }
                return -static_cast<float>(ham);
            }
        };

        int h_out = acquire_float_vector(vm_state->query_context);
        if (h_out < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        IMPULSE_ASSERT(static_cast<size_t>(h_out) < VM_MAX_VECTOR_HANDLES);
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
                    const bool is_null_vec = (IMP_VALIDITY_BYTE_OFFSET(u) >= snap_bm_raw_bytes) ||
                                             !IMP_VALIDITY_IS_VALID(snap_bm, u);
                    IMPULSE_TESTCASE(is_null_vec);
                    if (is_null_vec) {
                        scores[i] = std::numeric_limits<float>::quiet_NaN();
                        continue;
                    }
                }
                bool is_f16 = (snap_base_tc == IMP_TYPE_CODE_FLOAT16 && snap_ver >= OP_CB_SNAP_VER_V10) ||
                              (snap_base_tc == IMP_TYPE_CODE_INTERVAL_MS64 && snap_dom_rows > 0 &&
                               snap_bytes >= snap_dom_rows * snap_dim * OP_CB_FP16_ELEM_BYTES &&
                               snap_bytes < snap_dom_rows * snap_dim * OP_CB_FP32_ELEM_BYTES);
                IMPULSE_TESTCASE(is_f16);
                if (is_f16) {
                    if ((u + 1ULL) * snap_dim * OP_CB_FP16_ELEM_BYTES <= snap_bytes) {
                        unpack_buf.resize(snap_dim);
                        const auto* h_ptr = reinterpret_cast<const uint16_t*>(snap_raw) + u * snap_dim;
                        for (size_t k = 0; k < snap_dim; ++k) {
                            unpack_buf[k] = impulse_fp16_to_fp32(h_ptr[k]);
                        }
                        score = eval_metric_score(unpack_buf.data(), snap_dim);
                    } else {
                        score = std::numeric_limits<float>::quiet_NaN();
                    }
                } else if (snap_base_tc == IMP_TYPE_CODE_FLOAT64) {
                    if ((u + 1ULL) * snap_dim * OP_CB_FP64_ELEM_BYTES <= snap_bytes) {
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
        IMP_VM_SET_ZF(vm_state, out_len == 0u);
    }
    vm_state->pc++;
    DISPATCH();
}

