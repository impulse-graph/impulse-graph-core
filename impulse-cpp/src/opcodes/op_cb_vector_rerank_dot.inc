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

        const float* snap_embs = nullptr;
        size_t snap_emb_floats = 0;
        size_t snap_dim = q_len;
        if (!mock_embs && vm_state->query_context->snapshot && !vm_state->query_context->attribute_slots.empty()) {
            const auto& attrs0 = vm_state->query_context->attribute_slots[0];
            uint16_t a_idx = (attr_id < attrs0.size()) ? attr_id : 0u;
            if (a_idx < attrs0.size() && attrs0[a_idx].data_ptr) {
                snap_embs = static_cast<const float*>(attrs0[a_idx].data_ptr);
                snap_emb_floats = attrs0[a_idx].data_bytes / sizeof(float);
                if (attrs0[a_idx].dimension > 0) snap_dim = attrs0[a_idx].dimension;
            }
        }

        auto eval_metric_score = [&](const float* v, size_t dim) -> float {
            size_t d_len = std::min(q_len, dim);
            if (metric_flag == 0) { // MetricDot
                return impulse_simd_dot_product_f32(q_vec.data(), v, d_len);
            } else if (metric_flag == 1) { // MetricL2 (negative squared L2)
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

        for (size_t i = 0; i < out_len; ++i) {
            size_t u = cands[i];
            float score = 0.0f;
            if (mock_embs && q_len > 0 && (u + 1) * q_len <= mock_embs->size()) {
                score = eval_metric_score(mock_embs->data() + u * q_len, q_len);
            } else if (snap_embs && snap_dim > 0 && (u + 1) * snap_dim <= snap_emb_floats) {
                score = eval_metric_score(snap_embs + u * snap_dim, snap_dim);
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

