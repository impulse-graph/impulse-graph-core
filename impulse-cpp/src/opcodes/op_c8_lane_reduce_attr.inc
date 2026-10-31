// Opcode 0xC8: OP_LANE_REDUCE_ATTR
op_LANE_REDUCE_ATTR: {
    {
        const auto& inst = bytecode[vm_state->pc];
        uint16_t dst = inst.dst_reg;
        uint16_t src_reg = inst.payload & 0xFF;
        uint16_t attr_or_reg = (inst.payload >> 8) & 0xFF;
        uint16_t red_op = (inst.payload >> 16) & 0xFFFF;
        uint8_t flags = inst.flags;
        VALIDATE_REG(dst);
        VALIDATE_REG(src_reg);
        if (!vm_state->query_context) return IMPULSE_VM_ERR_NULL_SNAPSHOT;

        if (vm_state->register_types[src_reg] != TYPE_LANE_FRONTIER) {
            return IMPULSE_VM_ERR_INVALID_REGISTER;
        }
        size_t h_src = static_cast<size_t>(vm_state->registers[src_reg]);
        if (h_src >= VM_MAX_LANE_FRONTIER_HANDLES || h_src >= vm_state->query_context->lane_frontiers_count) {
            return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        }
        const auto& lf = vm_state->query_context->lane_frontiers[h_src];

        const auto* ma_ptr = ((flags & 0x01) == 0) ? find_mock_node_attr(vm_state->query_context, attr_or_reg) : nullptr;
        auto get_attr_val = [&](size_t u) -> float {
            if ((flags & 0x01) != 0) {
                if (attr_or_reg < 64) {
                    uint8_t t = vm_state->register_types[attr_or_reg];
                    uint64_t h = vm_state->registers[attr_or_reg];
                    if (t == TYPE_FLOAT_VECTOR && h < VM_MAX_VECTOR_HANDLES && vm_state->query_context->float_vectors_allocated[h]) {
                        const auto& fv = vm_state->query_context->float_vectors[h];
                        return (u < fv.size()) ? fv[u] : 0.0f;
                    } else if (t == TYPE_DOUBLE_VECTOR && h < VM_MAX_VECTOR_HANDLES && vm_state->query_context->double_vectors_allocated[h]) {
                        const auto& dv = vm_state->query_context->double_vectors[h];
                        return (u < dv.size()) ? static_cast<float>(dv[u]) : 0.0f;
                    } else if (t == TYPE_NODE_VECTOR || t == TYPE_UINT64_VECTOR) {
                        const uint64_t* ptr = nullptr;
                        size_t len = 0;
                        if (get_node_vector_slice(vm_state->query_context, h, &ptr, &len) && u < len) {
                            return static_cast<float>(ptr[u]);
                        }
                    }
                }
                return static_cast<float>(u);
            } else {
                if (ma_ptr) {
                    const auto& ma = *ma_ptr;
                    if (u < ma.float_data.size()) return ma.float_data[u];
                    if (u < ma.int_data.size()) return static_cast<float>(ma.int_data[u]);
                    return 0.0f;
                }
                for (const auto& dom_attrs : vm_state->query_context->attribute_slots) {
                    if (attr_or_reg < dom_attrs.size() && dom_attrs[attr_or_reg].data_ptr) {
                        const auto& slot = dom_attrs[attr_or_reg];
                        uint8_t base_t = slot.type_code & 0x7F;
                        if (base_t == 0x05 && (u + 1) * sizeof(float) <= slot.data_bytes) {
                            return static_cast<const float*>(slot.data_ptr)[u];
                        } else if (base_t == 0x06 && (u + 1) * sizeof(double) <= slot.data_bytes) {
                            return static_cast<float>(static_cast<const double*>(slot.data_ptr)[u]);
                        } else if (base_t == 0x03 && (u + 1) * sizeof(int32_t) <= slot.data_bytes) {
                            return static_cast<float>(static_cast<const int32_t*>(slot.data_ptr)[u]);
                        } else if (base_t == 0x04 && (u + 1) * sizeof(int64_t) <= slot.data_bytes) {
                            return static_cast<float>(static_cast<const int64_t*>(slot.data_ptr)[u]);
                        }
                    }
                }
                return static_cast<float>(u);
            }
        };

        uint32_t k = lf.num_lanes;
        size_t n_nodes = lf.lanes.size();

        if (red_op == BINARY_OP_ARGMIN || red_op == BINARY_OP_ARGMAX) {
            int h_out = acquire_node_vector(vm_state->query_context);
            if (h_out < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
            vm_state->query_context->node_vector_lengths[h_out] = k;
            vm_state->query_context->node_vector_length_explicit[h_out] = true;
            auto& out_vec = vm_state->query_context->node_vectors[h_out];

            std::array<float, 64> best_val;
            best_val.fill(red_op == BINARY_OP_ARGMIN
                ? std::numeric_limits<float>::infinity()
                : -std::numeric_limits<float>::infinity());
            for (uint32_t b = 0; b < k; ++b) {
                out_vec[b] = ~0ULL;
            }

            for (size_t u = 0; u < n_nodes; ++u) {
                uint64_t w = lf.lanes[u] & lf.active_mask;
                if (w == 0ULL) continue;
                float v = get_attr_val(u);
                uint64_t m = w;
                while (m != 0ULL) {
                    uint32_t b = static_cast<uint32_t>(std::countr_zero(m));
                    if (b < k) {
                        bool better = (red_op == BINARY_OP_ARGMIN) ? (v < best_val[b]) : (v > best_val[b]);
                        if (better || out_vec[b] == ~0ULL) {
                            best_val[b] = v;
                            out_vec[b] = static_cast<uint64_t>(u);
                        }
                    }
                    m &= (m - 1ULL);
                }
            }

            vm_state->registers[dst] = static_cast<uint64_t>(h_out);
            vm_state->register_types[dst] = TYPE_UINT64_VECTOR;
            if (k == 0) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
            else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;
        } else {
            int h_out = acquire_float_vector(vm_state->query_context);
            if (h_out < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
            vm_state->query_context->float_vector_lengths[h_out] = k;
            auto& out_vec = vm_state->query_context->float_vectors[h_out];

            std::array<bool, 64> seen{};
            for (uint32_t b = 0; b < k; ++b) {
                if (red_op == BINARY_OP_MUL) out_vec[b] = 1.0f;
                else if (red_op == BINARY_OP_MIN) out_vec[b] = std::numeric_limits<float>::infinity();
                else if (red_op == BINARY_OP_MAX) out_vec[b] = -std::numeric_limits<float>::infinity();
                else out_vec[b] = 0.0f;
            }

            for (size_t u = 0; u < n_nodes; ++u) {
                uint64_t w = lf.lanes[u] & lf.active_mask;
                if (w == 0ULL) continue;
                float v = (red_op == 4u) ? 1.0f : get_attr_val(u);
                uint64_t m = w;
                while (m != 0ULL) {
                    uint32_t b = static_cast<uint32_t>(std::countr_zero(m));
                    if (b < k) {
                        seen[b] = true;
                        if (red_op == BINARY_OP_ADD || red_op == 4u) out_vec[b] += v;
                        else if (red_op == BINARY_OP_MUL) out_vec[b] *= v;
                        else if (red_op == BINARY_OP_MIN) { if (v < out_vec[b]) out_vec[b] = v; }
                        else if (red_op == BINARY_OP_MAX) { if (v > out_vec[b]) out_vec[b] = v; }
                    }
                    m &= (m - 1ULL);
                }
            }
            for (uint32_t b = 0; b < k; ++b) {
                if (!seen[b] && (red_op == BINARY_OP_MUL || red_op == BINARY_OP_MIN || red_op == BINARY_OP_MAX)) {
                    out_vec[b] = 0.0f;
                }
            }

            vm_state->registers[dst] = static_cast<uint64_t>(h_out);
            vm_state->register_types[dst] = TYPE_FLOAT_VECTOR;
            if (k == 0) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
            else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;
        }
    }
    vm_state->pc++;
    DISPATCH();
}

