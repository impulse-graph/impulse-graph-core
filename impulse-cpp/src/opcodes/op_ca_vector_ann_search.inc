/**
 * Formal Opcode Contract: OP_VECTOR_ANN_SEARCH (0xCA)
 *
 * Layout:
 *   dst_reg:          inst.dst_reg (DST_REG)
 *   q_reg (src):      inst.payload & 0xFF (PAYLOAD_LOW8)
 *   top_k:            (inst.payload >> 8) & 0xFF
 *   allowed_src:      TYPE_FLOAT_VECTOR
 *   flags:
 *     0x00: Vamana ANN Search emitting BitSet (output: TYPE_BITSET_HANDLE)
 *     0x01: Vamana ANN Search emitting Fused Multi-Lane (output: TYPE_LANE_FRONTIER)
 *     ZF set if output matches are empty
 *   requires_ctx:     true
 */
#ifndef OP_CA_CONTRACT_DEFINED
#define OP_CA_CONTRACT_DEFINED
#define OP_CA_DEFINED_FLAGS             0x01u
#define OP_CA_FLAG_EMIT_MULTILANE       IMP_FLAG_BIT0
#define OP_CA_ALLOWED_SRC_TYPES         (IMP_TYPE_BIT(TYPE_FLOAT_VECTOR))
#define OP_CA_FLAG0_OUTPUT_DST          (IMP_TYPE_BIT(TYPE_BITSET_HANDLE))
#define OP_CA_FLAG1_OUTPUT_DST          (IMP_TYPE_BIT(TYPE_LANE_FRONTIER))

#define OP_CA_VAMANA_MIN_OFFSET         4096u
#define OP_CA_VAMANA_HDR_BYTES          64u
#define OP_CA_VAMANA_MAGIC              0x56414D41u
#define OP_CA_VAMANA_VERSION            1u
#define OP_CA_VAMANA_ADJ_U32_BYTES      4ULL

#define OP_CA_VHDR_OFF_MAGIC            0u
#define OP_CA_VHDR_OFF_VERSION          4u
#define OP_CA_VHDR_OFF_METRIC           6u
#define OP_CA_VHDR_OFF_QUANT_TYPE       7u
#define OP_CA_VHDR_OFF_DOMAIN_ID        8u
#define OP_CA_VHDR_OFF_VEC_ATTR_ID      10u
#define OP_CA_VHDR_OFF_DIMENSION        12u
#define OP_CA_VHDR_OFF_NODE_COUNT       16u
#define OP_CA_VHDR_OFF_MAX_DEGREE_R     24u
#define OP_CA_VHDR_OFF_MEDOID_ID        28u
#define OP_CA_VHDR_OFF_EF_CONSTRUCT     36u
#define OP_CA_VHDR_OFF_ADJ_SLAB_OFF     40u
#define OP_CA_VHDR_OFF_ADJ_SLAB_BYTES   48u
#define OP_CA_VHDR_OFF_QUANT_CODE_OFF   56u

#define OP_CA_VAMANA_METRIC_L2          0u
#define OP_CA_VAMANA_METRIC_COSINE      1u
#define OP_CA_VAMANA_METRIC_DOT         2u
#define OP_CA_VAMANA_METRIC_HAMMING     3u
#define OP_CA_VAMANA_METRIC_MAX         3u

#define OP_CA_QUANT_NONE                0u
#define OP_CA_QUANT_BQ1                 1u
#define OP_CA_QUANT_SQ8                 2u
#define OP_CA_QUANT_FP16                3u
#define OP_CA_QUANT_MAX                 3u

#define OP_CA_MAX_BEAM_CAP              256u
#define OP_CA_PREFETCH_LOOKAHEAD        2u
#define OP_CA_SQ8_ZERO_BIAS             128.0f
#define OP_CA_SQ8_SCALE_DIV             127.0f
#define OP_CA_COSINE_DENOM_EPS          1e-12f

#define OP_CA_FP16_SIGN_MASK            0x8000u
#define OP_CA_FP16_SIGN_SHIFT           16u
#define OP_CA_FP16_EXP_SHIFT            10u
#define OP_CA_FP16_EXP_MASK             0x1Fu
#define OP_CA_FP16_FRAC_MASK            0x03FFu
#define OP_CA_FP32_EXP_BIAS_DELTA       112u
#define OP_CA_FP32_SUBNORM_EXP_BASE     113u
#define OP_CA_FP32_SUBNORM_LZ_ADJ       21u
#define OP_CA_FP32_EXP_SHIFT            23u
#define OP_CA_FP32_FRAC_SHIFT           13u
#define OP_CA_FP32_INF_NAN_EXP_BITS     0x7F800000u
#endif

// Opcode 0xCA: OP_VECTOR_ANN_SEARCH
op_VECTOR_ANN_SEARCH: {
    {
        const auto& inst = bytecode[vm_state->pc];
        uint16_t dst = IMP_INST_DST(inst);
        uint16_t q_reg = IMP_INST_U8_0(inst);
        uint32_t p_u8_1 = IMP_INST_U8_1(inst);
        uint16_t p_u16_high = IMP_INST_HIGH16(inst);
        uint32_t top_k = (p_u8_1 > 0u) ? p_u8_1 : ((p_u16_high > 0u) ? p_u16_high : 1u);
        uint16_t vhdr_off = p_u16_high;
        const bool emit_multilane = IMP_INST_HAS_FLAG(inst, OP_CA_FLAG_EMIT_MULTILANE);
        IMP_VALIDATE_GPR(dst);
        IMP_VALIDATE_GPR(q_reg);
        IMP_REQUIRE_CONTEXT(vm_state);
        IMPULSE_ASSERT(vm_state->query_context != nullptr);

        if (vm_state->register_types[q_reg] != TYPE_FLOAT_VECTOR) {
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

        auto decode_fp16 = [](uint16_t h) -> float {
            uint32_t sign = static_cast<uint32_t>(h & OP_CA_FP16_SIGN_MASK) << OP_CA_FP16_SIGN_SHIFT;
            uint32_t exp = (h >> OP_CA_FP16_EXP_SHIFT) & OP_CA_FP16_EXP_MASK;
            uint32_t frac = h & OP_CA_FP16_FRAC_MASK;
            uint32_t bits = 0;
            if (IMPULSE_LIKELY(exp != 0u && exp != OP_CA_FP16_EXP_MASK)) {
                bits = sign | ((exp + OP_CA_FP32_EXP_BIAS_DELTA) << OP_CA_FP32_EXP_SHIFT) | (frac << OP_CA_FP32_FRAC_SHIFT);
            } else if (exp == 0u) {
                if (frac == 0u) {
                    bits = sign;
                } else {
                    uint32_t lz = static_cast<uint32_t>(std::countl_zero(frac)) - OP_CA_FP32_SUBNORM_LZ_ADJ;
                    uint32_t norm_frac = (frac << lz) & OP_CA_FP16_FRAC_MASK;
                    uint32_t norm_exp = OP_CA_FP32_SUBNORM_EXP_BASE - lz;
                    bits = sign | (norm_exp << OP_CA_FP32_EXP_SHIFT) | (norm_frac << OP_CA_FP32_FRAC_SHIFT);
                }
            } else {
                bits = sign | OP_CA_FP32_INF_NAN_EXP_BITS | (frac << OP_CA_FP32_FRAC_SHIFT);
            }
            float out = 0.0f;
            std::memcpy(&out, &bits, sizeof(float));
            return out;
        };

        auto compute_metric_score_dist = [](uint8_t metric_code, const float* q, const float* v, size_t dim, float* out_score, float* out_dist) {
            if (metric_code == OP_CA_VAMANA_METRIC_L2) {
                float sum_sq = 0.0f;
                for (size_t i = 0; i < dim; ++i) {
                    float d = q[i] - v[i];
                    sum_sq += d * d;
                }
                if (out_score) *out_score = -sum_sq;
                if (out_dist) *out_dist = sum_sq;
            } else if (metric_code == OP_CA_VAMANA_METRIC_DOT) {
                float dot = impulse_simd_dot_product_f32(q, v, dim);
                if (out_score) *out_score = dot;
                if (out_dist) *out_dist = -dot;
            } else if (metric_code == OP_CA_VAMANA_METRIC_COSINE) {
                float dot = impulse_simd_dot_product_f32(q, v, dim);
                float nq = impulse_simd_dot_product_f32(q, q, dim);
                float nv = impulse_simd_dot_product_f32(v, v, dim);
                float denom = std::sqrt(nq) * std::sqrt(nv);
                float cos_sim = (denom <= OP_CA_COSINE_DENOM_EPS) ? 0.0f : (dot / denom);
                if (out_score) *out_score = cos_sim;
                if (out_dist) *out_dist = 1.0f - cos_sim;
            } else {
                uint32_t ham = 0;
                for (size_t i = 0; i < dim; ++i) {
                    bool bq = (q[i] >= 0.0f);
                    bool bv = (v[i] >= 0.0f);
                    if (bq != bv) ++ham;
                }
                float d = static_cast<float>(ham);
                if (out_score) *out_score = -d;
                if (out_dist) *out_dist = d;
            }
        };

        auto& top_nodes = vm_state->query_context->scratch_u64_a;
        top_nodes.clear();

        if (vm_state->query_context->snapshot && vhdr_off >= OP_CA_VAMANA_MIN_OFFSET) {
            const impulse_snapshot_t* snap = vm_state->query_context->snapshot;
            if ((vhdr_off % IMP_SNAP_SECTION_ALIGN) != 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
            const uint8_t* hdr = static_cast<const uint8_t*>(impulse_snapshot_get_buffer(snap, vhdr_off, OP_CA_VAMANA_HDR_BYTES));
            if (!hdr) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;

            uint32_t v_magic = 0;
            uint16_t v_version = 0;
            uint8_t v_metric = hdr[OP_CA_VHDR_OFF_METRIC];
            uint8_t v_quant_type = hdr[OP_CA_VHDR_OFF_QUANT_TYPE];
            uint16_t v_domain_id = 0;
            uint16_t v_vec_attr_id = 0;
            uint32_t v_dimension = 0;
            uint64_t v_node_count = 0;
            uint32_t v_max_degree_r = 0;
            uint32_t v_medoid_node_id = 0;
            uint32_t v_ef_construction = 0;
            uint64_t v_adj_slab_offset = 0;
            uint64_t v_adj_slab_bytes = 0;
            uint64_t v_quant_code_offset = 0;

            std::memcpy(&v_magic, hdr + OP_CA_VHDR_OFF_MAGIC, sizeof(uint32_t));
            std::memcpy(&v_version, hdr + OP_CA_VHDR_OFF_VERSION, sizeof(uint16_t));
            std::memcpy(&v_domain_id, hdr + OP_CA_VHDR_OFF_DOMAIN_ID, sizeof(uint16_t));
            std::memcpy(&v_vec_attr_id, hdr + OP_CA_VHDR_OFF_VEC_ATTR_ID, sizeof(uint16_t));
            std::memcpy(&v_dimension, hdr + OP_CA_VHDR_OFF_DIMENSION, sizeof(uint32_t));
            std::memcpy(&v_node_count, hdr + OP_CA_VHDR_OFF_NODE_COUNT, sizeof(uint64_t));
            std::memcpy(&v_max_degree_r, hdr + OP_CA_VHDR_OFF_MAX_DEGREE_R, sizeof(uint32_t));
            std::memcpy(&v_medoid_node_id, hdr + OP_CA_VHDR_OFF_MEDOID_ID, sizeof(uint32_t));
            std::memcpy(&v_ef_construction, hdr + OP_CA_VHDR_OFF_EF_CONSTRUCT, sizeof(uint32_t));
            std::memcpy(&v_adj_slab_offset, hdr + OP_CA_VHDR_OFF_ADJ_SLAB_OFF, sizeof(uint64_t));
            std::memcpy(&v_adj_slab_bytes, hdr + OP_CA_VHDR_OFF_ADJ_SLAB_BYTES, sizeof(uint64_t));
            std::memcpy(&v_quant_code_offset, hdr + OP_CA_VHDR_OFF_QUANT_CODE_OFF, sizeof(uint64_t));
            (void)v_domain_id;

            if (v_magic != OP_CA_VAMANA_MAGIC || v_version != OP_CA_VAMANA_VERSION ||
                v_metric > OP_CA_VAMANA_METRIC_MAX || v_quant_type > OP_CA_QUANT_MAX) {
                return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
            }
            if (v_dimension == 0 || v_max_degree_r == 0) {
                return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
            }
            if (v_node_count > 0 && v_medoid_node_id >= v_node_count) {
                return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
            }
            uint64_t deg_plus_one = static_cast<uint64_t>(v_max_degree_r) + 1ULL;
            uint64_t row_bytes_u64 = 0;
            uint64_t expected_slab_bytes = 0;
            if (__builtin_mul_overflow(deg_plus_one, OP_CA_VAMANA_ADJ_U32_BYTES, &row_bytes_u64) ||
                __builtin_mul_overflow(v_node_count, row_bytes_u64, &expected_slab_bytes)) {
                return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
            }
            if ((v_adj_slab_offset % IMP_SNAP_SECTION_ALIGN) != 0ULL || v_adj_slab_bytes < expected_slab_bytes) {
                return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
            }
            const uint8_t* adj_slab = static_cast<const uint8_t*>(
                impulse_snapshot_get_buffer(snap, v_adj_slab_offset, v_adj_slab_bytes)
            );
            if (!adj_slab) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;

            uint64_t dim_u64 = static_cast<uint64_t>(v_dimension);
            uint64_t quant_row_bytes = (v_quant_type == OP_CA_QUANT_BQ1) ? ((dim_u64 + 7ULL) / 8ULL)
                : ((v_quant_type == OP_CA_QUANT_SQ8) ? dim_u64 : ((v_quant_type == OP_CA_QUANT_FP16) ? (dim_u64 * 2ULL) : 0ULL));
            const uint8_t* quant_slab = nullptr;
            if (v_quant_code_offset > 0ULL) {
                if ((v_quant_code_offset % IMP_SNAP_SECTION_ALIGN) != 0ULL) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                if (v_quant_type != OP_CA_QUANT_NONE) {
                    uint64_t expected_quant_bytes = 0;
                    if (__builtin_mul_overflow(v_node_count, quant_row_bytes, &expected_quant_bytes)) {
                        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                    }
                    quant_slab = static_cast<const uint8_t*>(
                        impulse_snapshot_get_buffer(snap, v_quant_code_offset, expected_quant_bytes)
                    );
                    if (!quant_slab) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                }
            }

            const BoundAttributeSlot* attr_ptr = resolve_node_attribute_slot(
                vm_state->query_context, v_domain_id, v_vec_attr_id
            );
            if (!attr_ptr && v_domain_id != 0) {
                attr_ptr = resolve_node_attribute_slot(vm_state->query_context, 0, v_vec_attr_id);
            }
            if (!attr_ptr) {
                return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
            }
            const auto& attr = *attr_ptr;
            uint32_t attr_dim = std::max<uint32_t>(1u, attr.dimension);
            uint64_t vec_row_bytes = static_cast<uint64_t>(attr_dim) * sizeof(float);
            uint64_t req_emb_bytes = 0;
            if (__builtin_mul_overflow(v_node_count, vec_row_bytes, &req_emb_bytes)) {
                return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
            }
            uint64_t emb_offset_bytes = 0;
            if (IMP_ATTR_IS_NULLABLE(attr.type_code) && v_node_count > 0) {
                uint64_t words = (v_node_count >> 6) + ((v_node_count & 63ULL) != 0ULL ? 1ULL : 0ULL);
                uint64_t raw_bm = 0;
                if (__builtin_mul_overflow(words, 8ULL, &raw_bm) ||
                    __builtin_add_overflow(raw_bm, 127ULL, &emb_offset_bytes)) {
                    return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                }
                emb_offset_bytes &= ~127ULL;
            }
            uint64_t total_req_attr_bytes = 0;
            if (__builtin_add_overflow(emb_offset_bytes, req_emb_bytes, &total_req_attr_bytes) ||
                !attr.data_ptr || attr.data_bytes < total_req_attr_bytes ||
                (reinterpret_cast<uintptr_t>(static_cast<const uint8_t*>(attr.data_ptr) + emb_offset_bytes) % alignof(float)) != 0u) {
                return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
            }
            IMPULSE_WELL_FORMED_ASSERT(adj_slab != nullptr && attr.data_ptr != nullptr);
            const float* emb_table = reinterpret_cast<const float*>(
                static_cast<const uint8_t*>(attr.data_ptr) + emb_offset_bytes
            );
            size_t dim = std::min<size_t>(v_dimension, std::min<size_t>(attr_dim, q_len));
            IMPULSE_ASSERT(dim <= q_len && dim <= v_dimension);

            if (v_node_count > 0 && top_k > 0) {
                size_t n_nodes = static_cast<size_t>(v_node_count);
                size_t ef = std::min<size_t>(OP_CA_MAX_BEAM_CAP, std::max<size_t>(top_k, std::max<size_t>(1, v_ef_construction)));
                IMPULSE_ASSERT(ef >= 1u && ef <= OP_CA_MAX_BEAM_CAP);
                size_t num_words = (n_nodes + IMP_WORD_BIT_MASK) >> IMP_WORD_BIT_SHIFT;
                auto& visited_words = vm_state->query_context->bfs_visited_words[0];
                visited_words.clear();
                visited_words.resize(num_words, 0ULL);
                auto& expanded_words = vm_state->query_context->scratch_lanes;
                expanded_words.clear();
                expanded_words.resize(num_words, 0ULL);

                bool use_quant = (v_quant_type != OP_CA_QUANT_NONE && quant_slab != nullptr);
                IMPULSE_TESTCASE(use_quant);
                const float* q_ptr = q_vec.data();
                const float q_norm = (v_metric == OP_CA_VAMANA_METRIC_COSINE)
                    ? std::sqrt(impulse_simd_dot_product_f32(q_ptr, q_ptr, dim))
                    : 0.0f;

                size_t bq1_num_words = (dim + IMP_WORD_BIT_MASK) >> IMP_WORD_BIT_SHIFT;
                auto& q_bq1_words = vm_state->query_context->scratch_lanes_b;
                if (use_quant && v_quant_type == OP_CA_QUANT_BQ1) {
                    q_bq1_words.assign(bq1_num_words, 0ULL);
                    for (size_t d = 0; d < dim; ++d) {
                        if (q_ptr[d] >= 0.0f) {
                            q_bq1_words[IMP_WORD_INDEX(d)] |= IMP_BIT_MASK64(d);
                        }
                    }
                }

                auto eval_beam_dist = [&](uint32_t u) -> float {
                    if (!use_quant) {
                        float dist = 0.0f;
                        compute_metric_score_dist(v_metric, q_ptr, emb_table + static_cast<size_t>(u) * attr_dim, dim, nullptr, &dist);
                        return dist;
                    }
                    const uint8_t* row_ptr = quant_slab + static_cast<size_t>(u) * static_cast<size_t>(quant_row_bytes);
                    if (v_quant_type == OP_CA_QUANT_BQ1) { // QuantBQ1: 64-bit word-packed XOR + hardware popcount
                        IMPULSE_TESTCASE(v_quant_type == OP_CA_QUANT_BQ1);
                        uint32_t ham = 0;
                        size_t full_words = dim >> IMP_WORD_BIT_SHIFT;
                        for (size_t w = 0; w < full_words; ++w) {
                            uint64_t v_word = 0ULL;
                            std::memcpy(&v_word, row_ptr + (w * sizeof(uint64_t)), sizeof(uint64_t));
                            ham += static_cast<uint32_t>(std::popcount(q_bq1_words[w] ^ v_word));
                        }
                        size_t rem_bits = dim & IMP_WORD_BIT_MASK;
                        if (rem_bits > 0) {
                            uint64_t v_tail = 0ULL;
                            size_t tail_bytes = (rem_bits + 7u) >> 3;
                            std::memcpy(&v_tail, row_ptr + (full_words * sizeof(uint64_t)), tail_bytes);
                            uint64_t tail_mask = (1ULL << rem_bits) - 1ULL;
                            ham += static_cast<uint32_t>(std::popcount((q_bq1_words[full_words] ^ v_tail) & tail_mask));
                        }
                        return static_cast<float>(ham);
                    } else if (v_quant_type == OP_CA_QUANT_SQ8) { // QuantSQ8: fused single-pass dequantization + distance
                        IMPULSE_TESTCASE(v_quant_type == OP_CA_QUANT_SQ8);
                        constexpr float inv_127 = 1.0f / OP_CA_SQ8_SCALE_DIV;
                        if (v_metric == OP_CA_VAMANA_METRIC_L2) {
                            float sum_sq = 0.0f;
                            for (size_t d = 0; d < dim; ++d) {
                                float vf = (static_cast<float>(row_ptr[d]) - OP_CA_SQ8_ZERO_BIAS) * inv_127;
                                float diff = q_ptr[d] - vf;
                                sum_sq += diff * diff;
                            }
                            return sum_sq;
                        } else if (v_metric == OP_CA_VAMANA_METRIC_DOT) {
                            float dot = 0.0f;
                            for (size_t d = 0; d < dim; ++d) {
                                float vf = (static_cast<float>(row_ptr[d]) - OP_CA_SQ8_ZERO_BIAS) * inv_127;
                                dot += q_ptr[d] * vf;
                            }
                            return -dot;
                        } else if (v_metric == OP_CA_VAMANA_METRIC_COSINE) {
                            float dot = 0.0f;
                            float nv = 0.0f;
                            for (size_t d = 0; d < dim; ++d) {
                                float vf = (static_cast<float>(row_ptr[d]) - OP_CA_SQ8_ZERO_BIAS) * inv_127;
                                dot += q_ptr[d] * vf;
                                nv += vf * vf;
                            }
                            float denom = q_norm * std::sqrt(nv);
                            return 1.0f - ((denom <= OP_CA_COSINE_DENOM_EPS) ? 0.0f : (dot / denom));
                        } else {
                            uint32_t ham = 0;
                            for (size_t d = 0; d < dim; ++d) {
                                float vf = (static_cast<float>(row_ptr[d]) - OP_CA_SQ8_ZERO_BIAS) * inv_127;
                                if ((q_ptr[d] >= 0.0f) != (vf >= 0.0f)) ++ham;
                            }
                            return static_cast<float>(ham);
                        }
                    } else { // QuantFP16: fused single-pass dequantization + distance
                        IMPULSE_TESTCASE(v_quant_type == OP_CA_QUANT_FP16);
                        if (v_metric == OP_CA_VAMANA_METRIC_L2) {
                            float sum_sq = 0.0f;
                            for (size_t d = 0; d < dim; ++d) {
                                uint16_t h16 = 0;
                                std::memcpy(&h16, row_ptr + (d * sizeof(uint16_t)), sizeof(uint16_t));
                                float vf = decode_fp16(h16);
                                float diff = q_ptr[d] - vf;
                                sum_sq += diff * diff;
                            }
                            return sum_sq;
                        } else if (v_metric == OP_CA_VAMANA_METRIC_DOT) {
                            float dot = 0.0f;
                            for (size_t d = 0; d < dim; ++d) {
                                uint16_t h16 = 0;
                                std::memcpy(&h16, row_ptr + (d * sizeof(uint16_t)), sizeof(uint16_t));
                                float vf = decode_fp16(h16);
                                dot += q_ptr[d] * vf;
                            }
                            return -dot;
                        } else if (v_metric == OP_CA_VAMANA_METRIC_COSINE) {
                            float dot = 0.0f;
                            float nv = 0.0f;
                            for (size_t d = 0; d < dim; ++d) {
                                uint16_t h16 = 0;
                                std::memcpy(&h16, row_ptr + (d * sizeof(uint16_t)), sizeof(uint16_t));
                                float vf = decode_fp16(h16);
                                dot += q_ptr[d] * vf;
                                nv += vf * vf;
                            }
                            float denom = q_norm * std::sqrt(nv);
                            return 1.0f - ((denom <= OP_CA_COSINE_DENOM_EPS) ? 0.0f : (dot / denom));
                        } else {
                            uint32_t ham = 0;
                            for (size_t d = 0; d < dim; ++d) {
                                uint16_t h16 = 0;
                                std::memcpy(&h16, row_ptr + (d * sizeof(uint16_t)), sizeof(uint16_t));
                                float vf = decode_fp16(h16);
                                if ((q_ptr[d] >= 0.0f) != (vf >= 0.0f)) ++ham;
                            }
                            return static_cast<float>(ham);
                        }
                    }
                };

                std::array<uint32_t, OP_CA_MAX_BEAM_CAP> cand_u{};
                std::array<float, OP_CA_MAX_BEAM_CAP> cand_d{};
                size_t cand_len = 0;

                auto insert_cand = [&](uint32_t u, float d, size_t cap) {
                    auto better = [](uint32_t u1, float d1, uint32_t u2, float d2) {
                        return (d1 < d2) || (d1 == d2 && u1 < u2);
                    };
                    if (cand_len == cap && !better(u, d, cand_u[cand_len - 1], cand_d[cand_len - 1])) {
                        return;
                    }
                    size_t pos = cand_len;
                    if (cand_len < cap) ++cand_len;
                    else pos = cap - 1;
                    while (pos > 0 && better(u, d, cand_u[pos - 1], cand_d[pos - 1])) {
                        cand_u[pos] = cand_u[pos - 1];
                        cand_d[pos] = cand_d[pos - 1];
                        --pos;
                    }
                    cand_u[pos] = u;
                    cand_d[pos] = d;
                };

                visited_words[IMP_WORD_INDEX(v_medoid_node_id)] |= IMP_BIT_MASK64(v_medoid_node_id);
                insert_cand(v_medoid_node_id, eval_beam_dist(v_medoid_node_id), ef);

                size_t row_stride = static_cast<size_t>(v_max_degree_r + 1u) * static_cast<size_t>(OP_CA_VAMANA_ADJ_U32_BYTES);
                while (true) {
                    int next_idx = -1;
                    for (size_t i = 0; i < cand_len; ++i) {
                        uint32_t u = cand_u[i];
                        if ((expanded_words[IMP_WORD_INDEX(u)] & IMP_BIT_MASK64(u)) == 0ULL) {
                            next_idx = static_cast<int>(i);
                            break;
                        }
                    }
                    if (next_idx < 0) break;
                    uint32_t u = cand_u[static_cast<size_t>(next_idx)];
                    expanded_words[IMP_WORD_INDEX(u)] |= IMP_BIT_MASK64(u);

                    const uint8_t* row_ptr = adj_slab + static_cast<size_t>(u) * row_stride;
                    uint32_t deg = 0;
                    std::memcpy(&deg, row_ptr, sizeof(uint32_t));
                    if (deg > v_max_degree_r) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                    for (uint32_t j = 0; j < deg; ++j) {
                        if (j + OP_CA_PREFETCH_LOOKAHEAD < deg) {
                            uint32_t pref_nb = 0;
                            std::memcpy(&pref_nb, row_ptr + (j + OP_CA_PREFETCH_LOOKAHEAD + 1u) * sizeof(uint32_t), sizeof(uint32_t));
                            if (pref_nb < n_nodes) {
                                if (use_quant) {
                                    IMPULSE_PREFETCH_L2(
                                        quant_slab + static_cast<size_t>(pref_nb) * static_cast<size_t>(quant_row_bytes)
                                    );
                                } else {
                                    IMPULSE_PREFETCH_L2(
                                        emb_table + static_cast<size_t>(pref_nb) * attr_dim
                                    );
                                }
                            }
                        }
                        uint32_t nb = 0;
                        std::memcpy(&nb, row_ptr + (j + 1u) * sizeof(uint32_t), sizeof(uint32_t));
                        if (nb < n_nodes && (visited_words[IMP_WORD_INDEX(nb)] & IMP_BIT_MASK64(nb)) == 0ULL) {
                            visited_words[IMP_WORD_INDEX(nb)] |= IMP_BIT_MASK64(nb);
                            insert_cand(nb, eval_beam_dist(nb), ef);
                        }
                    }
                }

                if (use_quant) {
                    // Stage 2: Exact Full-Precision Float32 Reranking of the ef beam candidates
                    std::array<uint32_t, OP_CA_MAX_BEAM_CAP> orig_u{};
                    size_t orig_len = cand_len;
                    for (size_t i = 0; i < orig_len; ++i) orig_u[i] = cand_u[i];
                    cand_len = 0;
                    for (size_t i = 0; i < orig_len; ++i) {
                        uint32_t u = orig_u[i];
                        float exact_dist = 0.0f;
                        compute_metric_score_dist(v_metric, q_vec.data(), emb_table + static_cast<size_t>(u) * attr_dim, dim, nullptr, &exact_dist);
                        insert_cand(u, exact_dist, orig_len);
                    }
                }

                IMPULSE_ASSERT(cand_len <= ef);
                IMPULSE_TESTCASE(top_k >= cand_len);
                size_t take = std::min<size_t>(top_k, cand_len);
                for (size_t i = 0; i < take; ++i) {
                    top_nodes.push_back(cand_u[i]);
                }
            }
        } else {
            // Fallback: check mock_node_attrs(vhdr_off) or mock_node_attrs(0)
            const auto* ma_ptr = find_mock_node_attr(vm_state->query_context, vhdr_off);
            if (!ma_ptr) ma_ptr = find_mock_node_attr(vm_state->query_context, 0);
            if (ma_ptr && !ma_ptr->float_data.empty() && q_len > 0 && ma_ptr->float_data.size() >= q_len) {
                const auto& flat_embs = ma_ptr->float_data;
                size_t dim = q_len;
                size_t n_nodes = flat_embs.size() / dim;
                size_t take_cap = std::min<size_t>(OP_CA_MAX_BEAM_CAP, std::min<size_t>(top_k, n_nodes));
                std::array<uint32_t, OP_CA_MAX_BEAM_CAP> best_u{};
                std::array<float, OP_CA_MAX_BEAM_CAP> best_d{};
                size_t count = 0;

                for (size_t u = 0; u < n_nodes; ++u) {
                    float dist = 0.0f;
                    compute_metric_score_dist(OP_CA_VAMANA_METRIC_L2, q_vec.data(), flat_embs.data() + u * dim, dim, nullptr, &dist);
                    if (take_cap == 0) continue;
                    auto better = [](uint32_t u1, float d1, uint32_t u2, float d2) {
                        return (d1 < d2) || (d1 == d2 && u1 < u2);
                    };
                    if (count < take_cap || better(static_cast<uint32_t>(u), dist, best_u[count - 1], best_d[count - 1])) {
                        size_t pos = count;
                        if (count < take_cap) ++count;
                        else pos = take_cap - 1;
                        while (pos > 0 && better(static_cast<uint32_t>(u), dist, best_u[pos - 1], best_d[pos - 1])) {
                            best_u[pos] = best_u[pos - 1];
                            best_d[pos] = best_d[pos - 1];
                            --pos;
                        }
                        best_u[pos] = static_cast<uint32_t>(u);
                        best_d[pos] = dist;
                    }
                }
                for (size_t i = 0; i < count; ++i) {
                    top_nodes.push_back(best_u[i]);
                }
            } else {
                top_nodes.push_back(0ULL);
            }
        }

        IMPULSE_TESTCASE(emit_multilane);
        if (emit_multilane) {
            // Fused Vector-to-Multi-Lane Handshake: directly initialize TYPE_LANE_FRONTIER
            uint32_t k = static_cast<uint32_t>(std::min<size_t>(IMP_VM_MAX_LANES, top_nodes.size()));
            IMPULSE_ASSERT(k <= IMP_VM_MAX_LANES);
            IMPULSE_TESTCASE(k == 0u);
            IMPULSE_TESTCASE(k == IMP_VM_MAX_LANES);
            uint64_t max_n = 0;
            for (uint64_t u : top_nodes) {
                if (u > max_n) max_n = u;
            }
            size_t node_count = resolve_multilane_node_count(vm_state->query_context, 0, static_cast<size_t>(max_n + 1));
            int h_lf = acquire_lane_frontier(vm_state->query_context, node_count, k);
            if (h_lf < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
            IMPULSE_ASSERT(static_cast<size_t>(h_lf) < VM_MAX_LANE_FRONTIER_HANDLES);
            auto& lf = vm_state->query_context->lane_frontiers[h_lf];
            for (uint32_t b = 0; b < k; ++b) {
                uint64_t u = top_nodes[b];
                if (u < node_count) {
                    lf.lanes[u] |= IMP_BIT_MASK64(b);
                    lf.active_words[IMP_WORD_INDEX(u)] |= IMP_BIT_MASK64(u);
                }
            }
            vm_state->registers[dst] = static_cast<uint64_t>(h_lf);
            vm_state->register_types[dst] = TYPE_LANE_FRONTIER;
            IMP_VM_SET_ZF(vm_state, k == 0u);
        } else {
            int h_bs = acquire_bitset(vm_state->query_context);
            if (h_bs < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
            IMPULSE_ASSERT(static_cast<size_t>(h_bs) < VM_MAX_BITSET_HANDLES);
            auto& bs = vm_state->query_context->bitsets[h_bs];
            bs.clear();
            for (uint64_t u : top_nodes) {
                bs.set(u);
            }
            vm_state->registers[dst] = static_cast<uint64_t>(h_bs);
            vm_state->register_types[dst] = TYPE_BITSET_HANDLE;
            IMP_VM_SET_ZF(vm_state, top_nodes.empty());
        }
    }
    vm_state->pc++;
    DISPATCH();
}

