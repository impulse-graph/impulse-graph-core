// Opcode 0xCA: OP_VECTOR_ANN_SEARCH
op_VECTOR_ANN_SEARCH: {
    {
        const auto& inst = bytecode[vm_state->pc];
        uint16_t dst = inst.dst_reg;
        uint16_t q_reg = inst.payload & 0xFF;
        uint32_t p_u8_1 = (inst.payload >> 8) & 0xFF;
        uint16_t p_u16_high = (inst.payload >> 16) & 0xFFFF;
        uint32_t top_k = (p_u8_1 > 0) ? p_u8_1 : ((p_u16_high > 0) ? p_u16_high : 1u);
        uint16_t vhdr_off = p_u16_high;
        uint8_t flags = inst.flags;
        VALIDATE_REG(dst);
        VALIDATE_REG(q_reg);
        if (!vm_state->query_context) return IMPULSE_VM_ERR_NULL_SNAPSHOT;

        if (vm_state->register_types[q_reg] != TYPE_FLOAT_VECTOR) {
            return IMPULSE_VM_ERR_INVALID_REGISTER;
        }
        uint64_t h_q = vm_state->registers[q_reg];
        if (h_q >= VM_MAX_VECTOR_HANDLES || !vm_state->query_context->float_vectors_allocated[h_q]) {
            return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        }
        const auto& q_vec = vm_state->query_context->float_vectors[h_q];
        size_t q_len = vm_state->query_context->float_vector_lengths[h_q];
        if (q_len == 0 || q_len > q_vec.size()) q_len = q_vec.size();

        auto decode_fp16 = [](uint16_t h) -> float {
            uint32_t sign = static_cast<uint32_t>(h & 0x8000u) << 16;
            uint32_t exp = (h >> 10) & 0x1Fu;
            uint32_t frac = h & 0x03FFu;
            uint32_t bits = 0;
            if (IMPULSE_LIKELY(exp != 0u && exp != 0x1Fu)) {
                bits = sign | ((exp + 112u) << 23) | (frac << 13);
            } else if (exp == 0u) {
                if (frac == 0u) {
                    bits = sign;
                } else {
                    uint32_t lz = static_cast<uint32_t>(std::countl_zero(frac)) - 21u;
                    uint32_t norm_frac = (frac << lz) & 0x03FFu;
                    uint32_t norm_exp = 113u - lz;
                    bits = sign | (norm_exp << 23) | (norm_frac << 13);
                }
            } else {
                bits = sign | 0x7F800000u | (frac << 13);
            }
            float out = 0.0f;
            std::memcpy(&out, &bits, sizeof(float));
            return out;
        };

        auto compute_metric_score_dist = [](uint8_t metric_code, const float* q, const float* v, size_t dim, float* out_score, float* out_dist) {
            if (metric_code == 0) { // MetricL2: squared L2 distance
                float sum_sq = 0.0f;
                for (size_t i = 0; i < dim; ++i) {
                    float d = q[i] - v[i];
                    sum_sq += d * d;
                }
                if (out_score) *out_score = -sum_sq;
                if (out_dist) *out_dist = sum_sq;
            } else if (metric_code == 2) { // MetricDot
                float dot = impulse_simd_dot_product_f32(q, v, dim);
                if (out_score) *out_score = dot;
                if (out_dist) *out_dist = -dot;
            } else if (metric_code == 1) { // MetricCosine
                float dot = impulse_simd_dot_product_f32(q, v, dim);
                float nq = impulse_simd_dot_product_f32(q, q, dim);
                float nv = impulse_simd_dot_product_f32(v, v, dim);
                float denom = std::sqrt(nq) * std::sqrt(nv);
                float cos_sim = (denom <= 1e-12f) ? 0.0f : (dot / denom);
                if (out_score) *out_score = cos_sim;
                if (out_dist) *out_dist = 1.0f - cos_sim;
            } else { // MetricHamming (3)
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

        if (vm_state->query_context->snapshot && vhdr_off >= 4096) {
            const impulse_snapshot_t* snap = vm_state->query_context->snapshot;
            if ((vhdr_off % 128) != 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
            const uint8_t* hdr = static_cast<const uint8_t*>(impulse_snapshot_get_buffer(snap, vhdr_off, 64));
            if (!hdr) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;

            uint32_t v_magic = 0;
            uint16_t v_version = 0;
            uint8_t v_metric = hdr[6];
            uint8_t v_quant_type = hdr[7];
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

            std::memcpy(&v_magic, hdr + 0, 4);
            std::memcpy(&v_version, hdr + 4, 2);
            std::memcpy(&v_domain_id, hdr + 8, 2);
            std::memcpy(&v_vec_attr_id, hdr + 10, 2);
            std::memcpy(&v_dimension, hdr + 12, 4);
            std::memcpy(&v_node_count, hdr + 16, 8);
            std::memcpy(&v_max_degree_r, hdr + 24, 4);
            std::memcpy(&v_medoid_node_id, hdr + 28, 4);
            std::memcpy(&v_ef_construction, hdr + 36, 4);
            std::memcpy(&v_adj_slab_offset, hdr + 40, 8);
            std::memcpy(&v_adj_slab_bytes, hdr + 48, 8);
            std::memcpy(&v_quant_code_offset, hdr + 56, 8);
            (void)v_domain_id;

            if (v_magic != 0x56414D41u || v_version != 1u || v_metric > 3u || v_quant_type > 3u) {
                return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
            }
            if (v_dimension == 0 || v_max_degree_r == 0) {
                return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
            }
            if (v_node_count > 0 && v_medoid_node_id >= v_node_count) {
                return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
            }
            uint64_t expected_slab_bytes = v_node_count * static_cast<uint64_t>(v_max_degree_r + 1u) * 4ULL;
            if ((v_adj_slab_offset % 128ULL) != 0ULL || v_adj_slab_bytes < expected_slab_bytes) {
                return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
            }
            const uint8_t* adj_slab = static_cast<const uint8_t*>(
                impulse_snapshot_get_buffer(snap, v_adj_slab_offset, v_adj_slab_bytes)
            );
            if (!adj_slab) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;

            uint64_t quant_row_bytes = (v_quant_type == 1) ? ((v_dimension + 7u) / 8u)
                : ((v_quant_type == 2) ? v_dimension : ((v_quant_type == 3) ? (v_dimension * 2u) : 0u));
            const uint8_t* quant_slab = nullptr;
            if (v_quant_code_offset > 0ULL) {
                if ((v_quant_code_offset % 128ULL) != 0ULL) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                if (v_quant_type != 0) {
                    quant_slab = static_cast<const uint8_t*>(
                        impulse_snapshot_get_buffer(snap, v_quant_code_offset, v_node_count * quant_row_bytes)
                    );
                    if (!quant_slab) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                }
            }

            if (vm_state->query_context->attribute_slots.empty() ||
                v_vec_attr_id >= vm_state->query_context->attribute_slots[0].size()) {
                return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
            }
            const auto& attr = vm_state->query_context->attribute_slots[0][v_vec_attr_id];
            uint32_t attr_dim = std::max<uint32_t>(1u, attr.dimension);
            if (!attr.data_ptr || attr.data_bytes < v_node_count * static_cast<uint64_t>(attr_dim) * sizeof(float)) {
                return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
            }
            const float* emb_table = static_cast<const float*>(attr.data_ptr);
            size_t dim = std::min<size_t>(v_dimension, std::min<size_t>(attr_dim, q_len));

            if (v_node_count > 0 && top_k > 0) {
                size_t n_nodes = static_cast<size_t>(v_node_count);
                size_t ef = std::min<size_t>(256, std::max<size_t>(top_k, std::max<size_t>(1, v_ef_construction)));
                size_t num_words = (n_nodes + 63) / 64;
                auto& visited_words = vm_state->query_context->bfs_visited_words[0];
                visited_words.clear();
                visited_words.resize(num_words, 0ULL);
                auto& expanded_words = vm_state->query_context->scratch_lanes;
                expanded_words.clear();
                expanded_words.resize(num_words, 0ULL);

                bool use_quant = (v_quant_type != 0 && quant_slab != nullptr);
                const float* q_ptr = q_vec.data();
                const float q_norm = (v_metric == 1)
                    ? std::sqrt(impulse_simd_dot_product_f32(q_ptr, q_ptr, dim))
                    : 0.0f;

                size_t bq1_num_words = (dim + 63) / 64;
                auto& q_bq1_words = vm_state->query_context->scratch_lanes_b;
                if (use_quant && v_quant_type == 1) {
                    q_bq1_words.assign(bq1_num_words, 0ULL);
                    for (size_t d = 0; d < dim; ++d) {
                        if (q_ptr[d] >= 0.0f) {
                            q_bq1_words[d >> 6] |= (1ULL << (d & 63));
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
                    if (v_quant_type == 1) { // QuantBQ1: 64-bit word-packed XOR + hardware popcount
                        IMPULSE_TESTCASE(v_quant_type == 1);
                        uint32_t ham = 0;
                        size_t full_words = dim >> 6;
                        for (size_t w = 0; w < full_words; ++w) {
                            uint64_t v_word = 0ULL;
                            std::memcpy(&v_word, row_ptr + (w << 3), sizeof(uint64_t));
                            ham += static_cast<uint32_t>(std::popcount(q_bq1_words[w] ^ v_word));
                        }
                        size_t rem_bits = dim & 63;
                        if (rem_bits > 0) {
                            uint64_t v_tail = 0ULL;
                            size_t tail_bytes = (rem_bits + 7) >> 3;
                            std::memcpy(&v_tail, row_ptr + (full_words << 3), tail_bytes);
                            uint64_t tail_mask = (1ULL << rem_bits) - 1ULL;
                            ham += static_cast<uint32_t>(std::popcount((q_bq1_words[full_words] ^ v_tail) & tail_mask));
                        }
                        return static_cast<float>(ham);
                    } else if (v_quant_type == 2) { // QuantSQ8: fused single-pass dequantization + distance
                        IMPULSE_TESTCASE(v_quant_type == 2);
                        constexpr float inv_127 = 1.0f / 127.0f;
                        if (v_metric == 0) { // MetricL2
                            float sum_sq = 0.0f;
                            for (size_t d = 0; d < dim; ++d) {
                                float vf = (static_cast<float>(row_ptr[d]) - 128.0f) * inv_127;
                                float diff = q_ptr[d] - vf;
                                sum_sq += diff * diff;
                            }
                            return sum_sq;
                        } else if (v_metric == 2) { // MetricDot
                            float dot = 0.0f;
                            for (size_t d = 0; d < dim; ++d) {
                                float vf = (static_cast<float>(row_ptr[d]) - 128.0f) * inv_127;
                                dot += q_ptr[d] * vf;
                            }
                            return -dot;
                        } else if (v_metric == 1) { // MetricCosine
                            float dot = 0.0f;
                            float nv = 0.0f;
                            for (size_t d = 0; d < dim; ++d) {
                                float vf = (static_cast<float>(row_ptr[d]) - 128.0f) * inv_127;
                                dot += q_ptr[d] * vf;
                                nv += vf * vf;
                            }
                            float denom = q_norm * std::sqrt(nv);
                            return 1.0f - ((denom <= 1e-12f) ? 0.0f : (dot / denom));
                        } else { // MetricHamming
                            uint32_t ham = 0;
                            for (size_t d = 0; d < dim; ++d) {
                                float vf = (static_cast<float>(row_ptr[d]) - 128.0f) * inv_127;
                                if ((q_ptr[d] >= 0.0f) != (vf >= 0.0f)) ++ham;
                            }
                            return static_cast<float>(ham);
                        }
                    } else { // QuantFP16: fused single-pass dequantization + distance
                        IMPULSE_TESTCASE(v_quant_type == 3);
                        if (v_metric == 0) { // MetricL2
                            float sum_sq = 0.0f;
                            for (size_t d = 0; d < dim; ++d) {
                                uint16_t h16 = 0;
                                std::memcpy(&h16, row_ptr + (d << 1), sizeof(uint16_t));
                                float vf = decode_fp16(h16);
                                float diff = q_ptr[d] - vf;
                                sum_sq += diff * diff;
                            }
                            return sum_sq;
                        } else if (v_metric == 2) { // MetricDot
                            float dot = 0.0f;
                            for (size_t d = 0; d < dim; ++d) {
                                uint16_t h16 = 0;
                                std::memcpy(&h16, row_ptr + (d << 1), sizeof(uint16_t));
                                float vf = decode_fp16(h16);
                                dot += q_ptr[d] * vf;
                            }
                            return -dot;
                        } else if (v_metric == 1) { // MetricCosine
                            float dot = 0.0f;
                            float nv = 0.0f;
                            for (size_t d = 0; d < dim; ++d) {
                                uint16_t h16 = 0;
                                std::memcpy(&h16, row_ptr + (d << 1), sizeof(uint16_t));
                                float vf = decode_fp16(h16);
                                dot += q_ptr[d] * vf;
                                nv += vf * vf;
                            }
                            float denom = q_norm * std::sqrt(nv);
                            return 1.0f - ((denom <= 1e-12f) ? 0.0f : (dot / denom));
                        } else { // MetricHamming
                            uint32_t ham = 0;
                            for (size_t d = 0; d < dim; ++d) {
                                uint16_t h16 = 0;
                                std::memcpy(&h16, row_ptr + (d << 1), sizeof(uint16_t));
                                float vf = decode_fp16(h16);
                                if ((q_ptr[d] >= 0.0f) != (vf >= 0.0f)) ++ham;
                            }
                            return static_cast<float>(ham);
                        }
                    }
                };

                std::array<uint32_t, 256> cand_u{};
                std::array<float, 256> cand_d{};
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

                visited_words[v_medoid_node_id >> 6] |= (1ULL << (v_medoid_node_id & 63));
                insert_cand(v_medoid_node_id, eval_beam_dist(v_medoid_node_id), ef);

                size_t row_stride = static_cast<size_t>(v_max_degree_r + 1u) * 4u;
                while (true) {
                    int next_idx = -1;
                    for (size_t i = 0; i < cand_len; ++i) {
                        uint32_t u = cand_u[i];
                        if ((expanded_words[u >> 6] & (1ULL << (u & 63))) == 0ULL) {
                            next_idx = static_cast<int>(i);
                            break;
                        }
                    }
                    if (next_idx < 0) break;
                    uint32_t u = cand_u[static_cast<size_t>(next_idx)];
                    expanded_words[u >> 6] |= (1ULL << (u & 63));

                    const uint8_t* row_ptr = adj_slab + static_cast<size_t>(u) * row_stride;
                    uint32_t deg = 0;
                    std::memcpy(&deg, row_ptr, 4);
                    if (deg > v_max_degree_r) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                    for (uint32_t j = 0; j < deg; ++j) {
                        if (j + 2u < deg) {
                            uint32_t pref_nb = 0;
                            std::memcpy(&pref_nb, row_ptr + (j + 3u) * 4u, 4);
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
                        std::memcpy(&nb, row_ptr + (j + 1u) * 4u, 4);
                        if (nb < n_nodes && (visited_words[nb >> 6] & (1ULL << (nb & 63))) == 0ULL) {
                            visited_words[nb >> 6] |= (1ULL << (nb & 63));
                            insert_cand(nb, eval_beam_dist(nb), ef);
                        }
                    }
                }

                if (use_quant) {
                    // Stage 2: Exact Full-Precision Float32 Reranking of the ef beam candidates
                    std::array<uint32_t, 256> orig_u{};
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
                size_t take_cap = std::min<size_t>(256, std::min<size_t>(top_k, n_nodes));
                std::array<uint32_t, 256> best_u{};
                std::array<float, 256> best_d{};
                size_t count = 0;

                for (size_t u = 0; u < n_nodes; ++u) {
                    float dist = 0.0f;
                    compute_metric_score_dist(0 /* MetricL2 */, q_vec.data(), flat_embs.data() + u * dim, dim, nullptr, &dist);
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

        if ((flags & 0x01) != 0) {
            // Fused Vector-to-Multi-Lane Handshake: directly initialize TYPE_LANE_FRONTIER
            uint32_t k = static_cast<uint32_t>(std::min<size_t>(64, top_nodes.size()));
            uint64_t max_n = 0;
            for (uint64_t u : top_nodes) {
                if (u > max_n) max_n = u;
            }
            size_t node_count = resolve_multilane_node_count(vm_state->query_context, 0, static_cast<size_t>(max_n + 1));
            int h_lf = acquire_lane_frontier(vm_state->query_context, node_count, k);
            if (h_lf < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
            auto& lf = vm_state->query_context->lane_frontiers[h_lf];
            for (uint32_t b = 0; b < k; ++b) {
                uint64_t u = top_nodes[b];
                if (u < node_count) {
                    lf.lanes[u] |= (1ULL << b);
                    lf.active_words[u >> 6] |= (1ULL << (u & 63));
                }
            }
            vm_state->registers[dst] = static_cast<uint64_t>(h_lf);
            vm_state->register_types[dst] = TYPE_LANE_FRONTIER;
            if (k == 0) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
            else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;
        } else {
            int h_bs = acquire_bitset(vm_state->query_context);
            if (h_bs < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
            auto& bs = vm_state->query_context->bitsets[h_bs];
            bs.clear();
            for (uint64_t u : top_nodes) {
                bs.set(u);
            }
            vm_state->registers[dst] = static_cast<uint64_t>(h_bs);
            vm_state->register_types[dst] = TYPE_BITSET_HANDLE;
            if (top_nodes.empty()) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
            else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;
        }
    }
    vm_state->pc++;
    DISPATCH();
}

