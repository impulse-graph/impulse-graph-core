/**
 * Formal Opcode Contract: OP_LANE_REDUCE_ATTR (0xC8)
 *
 * Layout:
 *   Variant 0 (flags & 0x01 == 0): Snapshot Attribute Scalar Reduction
 *     dst_reg:        inst.dst_reg (DST_REG)
 *     src_reg:        inst.payload & 0xFF (PAYLOAD_LOW8)
 *     attr_id (imm):  (inst.payload >> 8) & 0xFF
 *     red_op (imm):   (inst.payload >> 16) & 0xFFFF
 *     allowed_src:    TYPE_LANE_FRONTIER
 *     allowed_aux1:   none (immediate attribute id)
 *     output_dst:     TYPE_FLOAT_VECTOR
 *     flags:          ZF set if output vector is zero
 *     requires_ctx:   true
 *
 *   Variant 1 (flags & 0x01 == 1): Register Vector Scalar/Argmin Reduction
 *     dst_reg:        inst.dst_reg (DST_REG)
 *     src_reg:        inst.payload & 0xFF (PAYLOAD_LOW8)
 *     vec_reg (aux1): (inst.payload >> 8) & 0xFF (PAYLOAD_BYTE1)
 *     red_op (imm):   (inst.payload >> 16) & 0xFFFF
 *     allowed_src:    TYPE_LANE_FRONTIER
 *     allowed_aux1:   TYPE_FLOAT_VECTOR | TYPE_DOUBLE_VECTOR | TYPE_NODE_VECTOR | TYPE_UINT64_VECTOR
 *     output_dst:     TYPE_FLOAT_VECTOR (for SUM/MIN/MAX) or TYPE_UINT64_VECTOR (for ARGMIN/ARGMAX)
 *     flags:          ZF set if output is zero
 *     requires_ctx:   true
 */
#ifndef OP_C8_CONTRACT_DEFINED
#define OP_C8_CONTRACT_DEFINED
#define OP_C8_DEFINED_FLAGS        0x01u
#define OP_C8_ALLOWED_SRC_TYPES    (IMP_TYPE_BIT(TYPE_LANE_FRONTIER))
#define OP_C8_FLAG0_AUX1_TYPES     IMP_TYPES_NONE
#define OP_C8_FLAG1_AUX1_TYPES     (IMP_TYPES_FLOAT_OR_DBL | IMP_TYPES_NODE_VEC)
#define OP_C8_OUTPUT_SCALAR_DST    (IMP_TYPE_BIT(TYPE_FLOAT_VECTOR))
#define OP_C8_OUTPUT_ARG_DST       (IMP_TYPE_BIT(TYPE_UINT64_VECTOR))
#endif

// Opcode 0xC8: OP_LANE_REDUCE_ATTR
op_LANE_REDUCE_ATTR: {
    {
        const auto& inst = bytecode[vm_state->pc];
        uint16_t dst = IMP_INST_DST(inst);
        uint16_t src_reg = IMP_INST_U8_0(inst);
        uint16_t attr_or_reg = IMP_INST_U8_1(inst);
        uint16_t red_op = IMP_INST_HIGH16(inst);
        const bool attr_is_reg = IMP_INST_HAS_FLAG(inst, IMP_FLAG_ATTR_IS_REG);
        IMP_VALIDATE_GPR(dst);
        IMP_VALIDATE_GPR(src_reg);
        IMPULSE_TESTCASE(attr_is_reg);
        if (attr_is_reg) {
            IMP_VALIDATE_GPR(attr_or_reg);
            IMPULSE_CHECK_REG_TYPE(vm_state, attr_or_reg, OP_C8_FLAG1_AUX1_TYPES);
        }
        IMP_REQUIRE_CONTEXT(vm_state);
        IMPULSE_ASSERT(vm_state->query_context != nullptr);

        if (vm_state->register_types[src_reg] != TYPE_LANE_FRONTIER) {
            return IMPULSE_VM_ERR_INVALID_REGISTER;
        }
        size_t h_src = static_cast<size_t>(vm_state->registers[src_reg]);
        IMPULSE_TESTCASE(h_src >= VM_MAX_LANE_FRONTIER_HANDLES || h_src >= vm_state->query_context->lane_frontiers_count);
        if (h_src >= VM_MAX_LANE_FRONTIER_HANDLES || h_src >= vm_state->query_context->lane_frontiers_count) {
            return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        }
        const auto& lf = vm_state->query_context->lane_frontiers[h_src];

        const auto* ma_ptr = !attr_is_reg ? find_mock_node_attr(vm_state->query_context, attr_or_reg) : nullptr;
        auto get_attr_val = [&](size_t u) -> float {
            if (attr_is_reg) {
                if (attr_or_reg < IMP_VM_GPR_COUNT) {
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
                auto read_slot_float = [&](const BoundAttributeSlot& slot, float& out_f) -> bool {
                    uint8_t base_t = IMP_ATTR_BASE_TYPE(slot.type_code);
                    if (base_t == IMP_TYPE_CODE_FLOAT32 && (u + 1) * sizeof(float) <= slot.data_bytes) {
                        out_f = static_cast<const float*>(slot.data_ptr)[u];
                        return true;
                    } else if (base_t == IMP_TYPE_CODE_FLOAT64 && (u + 1) * sizeof(double) <= slot.data_bytes) {
                        out_f = static_cast<float>(static_cast<const double*>(slot.data_ptr)[u]);
                        return true;
                    } else if (base_t == IMP_LEGACY_V09_FLOAT32 && (u + 1) * sizeof(float) <= slot.data_bytes) {
                        out_f = static_cast<const float*>(slot.data_ptr)[u];
                        return true;
                    } else if (base_t == IMP_TYPE_CODE_INT32 && (u + 1) * sizeof(int32_t) <= slot.data_bytes) {
                        out_f = static_cast<float>(static_cast<const int32_t*>(slot.data_ptr)[u]);
                        return true;
                    } else if (base_t == IMP_TYPE_CODE_INT64 && (u + 1) * sizeof(int64_t) <= slot.data_bytes) {
                        out_f = static_cast<float>(static_cast<const int64_t*>(slot.data_ptr)[u]);
                        return true;
                    }
                    return false;
                };
                for (const auto& dom_attrs : vm_state->query_context->domain_attribute_slots) {
                    if (attr_or_reg < dom_attrs.size() && dom_attrs[attr_or_reg].data_ptr) {
                        float v = 0.0f;
                        if (read_slot_float(dom_attrs[attr_or_reg], v)) return v;
                    }
                }
                for (const auto& dom_attrs : vm_state->query_context->attribute_slots) {
                    if (attr_or_reg < dom_attrs.size() && dom_attrs[attr_or_reg].data_ptr) {
                        float v = 0.0f;
                        if (read_slot_float(dom_attrs[attr_or_reg], v)) return v;
                    }
                }
                return static_cast<float>(u);
            }
        };

        uint32_t k = lf.num_lanes;
        size_t n_nodes = lf.lanes.size();
        IMPULSE_ASSERT(k <= IMP_VM_MAX_LANES);
        IMPULSE_TESTCASE(k == 0u);
        IMPULSE_TESTCASE(k == IMP_VM_MAX_LANES);
        IMPULSE_TESTCASE(red_op == IMP_LANE_REDUCE_COUNT);

        if (red_op == BINARY_OP_ARGMIN || red_op == BINARY_OP_ARGMAX) {
            int h_out = acquire_node_vector(vm_state->query_context);
            if (h_out < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
            IMPULSE_ASSERT(static_cast<size_t>(h_out) < VM_MAX_VECTOR_HANDLES);
            vm_state->query_context->node_vector_lengths[h_out] = k;
            vm_state->query_context->node_vector_length_explicit[h_out] = true;
            auto& out_vec = vm_state->query_context->node_vectors[h_out];

            std::array<float, IMP_VM_MAX_LANES> best_val;
            best_val.fill(red_op == BINARY_OP_ARGMIN
                ? std::numeric_limits<float>::infinity()
                : -std::numeric_limits<float>::infinity());
            for (uint32_t b = 0; b < k; ++b) {
                out_vec[b] = IMP_INVALID_NODE_U64;
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
                        if (better || out_vec[b] == IMP_INVALID_NODE_U64) {
                            best_val[b] = v;
                            out_vec[b] = static_cast<uint64_t>(u);
                        }
                    }
                    m = IMP_CLEAR_LOWEST_BIT(m);
                }
            }

            vm_state->registers[dst] = static_cast<uint64_t>(h_out);
            vm_state->register_types[dst] = TYPE_UINT64_VECTOR;
            IMP_VM_SET_ZF(vm_state, k == 0u);
        } else {
            int h_out = acquire_float_vector(vm_state->query_context);
            if (h_out < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
            IMPULSE_ASSERT(static_cast<size_t>(h_out) < VM_MAX_VECTOR_HANDLES);
            vm_state->query_context->float_vector_lengths[h_out] = k;
            auto& out_vec = vm_state->query_context->float_vectors[h_out];

            std::array<bool, IMP_VM_MAX_LANES> seen{};
            for (uint32_t b = 0; b < k; ++b) {
                if (red_op == BINARY_OP_MUL) out_vec[b] = 1.0f;
                else if (red_op == BINARY_OP_MIN) out_vec[b] = std::numeric_limits<float>::infinity();
                else if (red_op == BINARY_OP_MAX) out_vec[b] = -std::numeric_limits<float>::infinity();
                else out_vec[b] = 0.0f;
            }

            for (size_t u = 0; u < n_nodes; ++u) {
                uint64_t w = lf.lanes[u] & lf.active_mask;
                if (w == 0ULL) continue;
                float v = (red_op == IMP_LANE_REDUCE_COUNT) ? 1.0f : get_attr_val(u);
                uint64_t m = w;
                while (m != 0ULL) {
                    uint32_t b = static_cast<uint32_t>(std::countr_zero(m));
                    if (b < k) {
                        seen[b] = true;
                        if (red_op == BINARY_OP_ADD || red_op == IMP_LANE_REDUCE_COUNT) out_vec[b] += v;
                        else if (red_op == BINARY_OP_MUL) out_vec[b] *= v;
                        else if (red_op == BINARY_OP_MIN) { if (v < out_vec[b]) out_vec[b] = v; }
                        else if (red_op == BINARY_OP_MAX) { if (v > out_vec[b]) out_vec[b] = v; }
                    }
                    m = IMP_CLEAR_LOWEST_BIT(m);
                }
            }
            for (uint32_t b = 0; b < k; ++b) {
                if (!seen[b] && (red_op == BINARY_OP_MUL || red_op == BINARY_OP_MIN || red_op == BINARY_OP_MAX)) {
                    out_vec[b] = 0.0f;
                }
            }

            vm_state->registers[dst] = static_cast<uint64_t>(h_out);
            vm_state->register_types[dst] = TYPE_FLOAT_VECTOR;
            IMP_VM_SET_ZF(vm_state, k == 0u);
        }
    }
    vm_state->pc++;
    DISPATCH();
}

