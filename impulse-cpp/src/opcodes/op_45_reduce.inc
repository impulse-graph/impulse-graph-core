/**
 * Formal Opcode Contract: OP_REDUCE (0x45)
 *
 * Layout:
 *   dst_reg:          inst.dst_reg (DST_REG)
 *   src_reg:          inst.payload & 0xFF (PAYLOAD_LOW8, src_vec)
 *   op_id:            (inst.payload >> 16) & 0xFFFF
 *   allowed_src:      IMP_TYPES_FLOAT_OR_DBL | IMP_TYPES_NODE_VEC
 *   output_dst:       TYPE_FLOAT | TYPE_DOUBLE | TYPE_NODE_ID
 *   flags:            ZF set if ARGMIN/ARGMAX index == 0
 *   requires_ctx:     true
 */
#ifndef OP_45_CONTRACT_DEFINED
#define OP_45_CONTRACT_DEFINED
#define OP_45_DEFINED_FLAGS        0x00u
#define OP_45_ALLOWED_SRC_TYPES    (IMP_TYPES_FLOAT_OR_DBL | IMP_TYPES_NODE_VEC)
#define OP_45_OUTPUT_DST_TYPES     (IMP_TYPE_BIT(TYPE_FLOAT) | IMP_TYPE_BIT(TYPE_DOUBLE) | IMP_TYPE_BIT(TYPE_NODE_ID))
#endif

// Opcode 0x45: OP_REDUCE
op_REDUCE: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src_vec = inst.payload & 0xFF;
    uint16_t op_id = (inst.payload >> 16) & 0xFFFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src_vec);
    if (!vm_state->query_context) return IMPULSE_VM_ERR_NULL_SNAPSHOT;
    IMPULSE_CHECK_REG_TYPE(vm_state, src_vec, OP_45_ALLOWED_SRC_TYPES);

    size_t N = vm_state->query_context->max_nodes;
    if (vm_state->register_types[src_vec] == TYPE_DOUBLE_VECTOR) {
        int handle = static_cast<int>(vm_state->registers[src_vec]);
        if (handle < 0 || handle >= static_cast<int>(VM_MAX_VECTOR_HANDLES) || !vm_state->query_context->double_vectors_allocated[handle]) {
            return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        }
        size_t active_len = vm_state->query_context->double_vector_lengths[handle];
        if (active_len > 0 && active_len < N) N = active_len;
        const double* vec = vm_state->query_context->double_vectors[handle].data();
        if (op_id == BINARY_OP_ARGMIN || op_id == BINARY_OP_ARGMAX) {
            double best_val = (op_id == BINARY_OP_ARGMIN)
                ? std::numeric_limits<double>::infinity()
                : -std::numeric_limits<double>::infinity();
            int64_t best_idx = (N > 0) ? 0 : -1;
            for (size_t i = 0; i < N; ++i) {
                double v = vec[i];
                if (std::isnan(v)) continue;
                if ((op_id == BINARY_OP_ARGMIN && v < best_val) ||
                    (op_id == BINARY_OP_ARGMAX && v > best_val)) {
                    best_val = v;
                    best_idx = static_cast<int64_t>(i);
                }
            }
            vm_state->registers[dst] = static_cast<uint64_t>(best_idx);
            vm_state->register_types[dst] = TYPE_NODE_ID;
            if (best_idx == 0) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
            else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;
        } else {
            double res = 0.0;
            size_t valid_count = 0;
            if (op_id == BINARY_OP_ADD) {
                for (size_t i = 0; i < N; ++i) {
                    if (!std::isnan(vec[i])) { res += vec[i]; valid_count++; }
                }
            } else if (op_id == BINARY_OP_MUL) {
                res = (N > 0) ? 1.0 : 0.0;
                for (size_t i = 0; i < N; ++i) {
                    if (!std::isnan(vec[i])) { res *= vec[i]; valid_count++; }
                }
                if (valid_count == 0) res = 0.0;
            } else if (op_id == BINARY_OP_MIN) {
                res = std::numeric_limits<double>::infinity();
                for (size_t i = 0; i < N; ++i) {
                    if (!std::isnan(vec[i])) { res = std::min(res, vec[i]); valid_count++; }
                }
            } else if (op_id == BINARY_OP_MAX) {
                res = -std::numeric_limits<double>::infinity();
                for (size_t i = 0; i < N; ++i) {
                    if (!std::isnan(vec[i])) { res = std::max(res, vec[i]); valid_count++; }
                }
            } else {
                for (size_t i = 0; i < N; ++i) {
                    if (!std::isnan(vec[i])) { res += vec[i]; valid_count++; }
                }
            }
            vm_state->registers[dst] = reinterpret_cast<uint64_t&>(res);
            vm_state->register_types[dst] = TYPE_DOUBLE;
            if (res == 0.0 || valid_count == 0) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
            else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;
        }
    } else if (vm_state->register_types[src_vec] == TYPE_FLOAT_VECTOR) {
        int handle = static_cast<int>(vm_state->registers[src_vec]);
        if (handle < 0 || handle >= static_cast<int>(VM_MAX_VECTOR_HANDLES) || !vm_state->query_context->float_vectors_allocated[handle]) {
            return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        }
        size_t active_len = vm_state->query_context->float_vector_lengths[handle];
        if (active_len > 0 && active_len < N) N = active_len;
        const float* vec = vm_state->query_context->float_vectors[handle].data();
        if (op_id == BINARY_OP_ARGMIN || op_id == BINARY_OP_ARGMAX) {
            float best_val = (op_id == BINARY_OP_ARGMIN)
                ? std::numeric_limits<float>::infinity()
                : -std::numeric_limits<float>::infinity();
            int64_t best_idx = (N > 0) ? 0 : -1;
            for (size_t i = 0; i < N; ++i) {
                float v = vec[i];
                if (std::isnan(v)) continue;
                if ((op_id == BINARY_OP_ARGMIN && v < best_val) ||
                    (op_id == BINARY_OP_ARGMAX && v > best_val)) {
                    best_val = v;
                    best_idx = static_cast<int64_t>(i);
                }
            }
            vm_state->registers[dst] = static_cast<uint64_t>(best_idx);
            vm_state->register_types[dst] = TYPE_NODE_ID;
            if (best_idx == 0) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
            else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;
        } else {
            float res = 0.0f;
            size_t valid_count = 0;
            if (op_id == BINARY_OP_ADD) {
                for (size_t i = 0; i < N; ++i) {
                    if (!std::isnan(vec[i])) { res += vec[i]; valid_count++; }
                }
            } else if (op_id == BINARY_OP_MUL) {
                res = (N > 0) ? 1.0f : 0.0f;
                for (size_t i = 0; i < N; ++i) {
                    if (!std::isnan(vec[i])) { res *= vec[i]; valid_count++; }
                }
                if (valid_count == 0) res = 0.0f;
            } else if (op_id == BINARY_OP_MIN) {
                res = std::numeric_limits<float>::infinity();
                for (size_t i = 0; i < N; ++i) {
                    if (!std::isnan(vec[i])) { res = std::min(res, vec[i]); valid_count++; }
                }
            } else if (op_id == BINARY_OP_MAX) {
                res = -std::numeric_limits<float>::infinity();
                for (size_t i = 0; i < N; ++i) {
                    if (!std::isnan(vec[i])) { res = std::max(res, vec[i]); valid_count++; }
                }
            } else {
                for (size_t i = 0; i < N; ++i) {
                    if (!std::isnan(vec[i])) { res += vec[i]; valid_count++; }
                }
            }
            uint32_t raw_bits = 0;
            std::memcpy(&raw_bits, &res, sizeof(float));
            vm_state->registers[dst] = static_cast<uint64_t>(raw_bits);
            vm_state->register_types[dst] = TYPE_FLOAT;
            if (res == 0.0f || valid_count == 0) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
            else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;
        }
    } else if (vm_state->register_types[src_vec] == TYPE_UINT64_VECTOR || vm_state->register_types[src_vec] == TYPE_NODE_VECTOR) {
        int handle = static_cast<int>(vm_state->registers[src_vec]);
        if (handle < 0 || handle >= static_cast<int>(VM_MAX_VECTOR_HANDLES) || !vm_state->query_context->node_vectors_allocated[handle]) {
            return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        }
        size_t active_len = vm_state->query_context->node_vector_lengths[handle];
        if (active_len > 0 && active_len < N) N = active_len;
        const uint64_t* vec = vm_state->query_context->node_vectors[handle].data();
        if (op_id == BINARY_OP_ARGMIN || op_id == BINARY_OP_ARGMAX) {
            uint64_t best_val = (N > 0) ? vec[0] : 0;
            int64_t best_idx = (N > 0) ? 0 : -1;
            for (size_t i = 1; i < N; ++i) {
                uint64_t v = vec[i];
                if ((op_id == BINARY_OP_ARGMIN && v < best_val) ||
                    (op_id == BINARY_OP_ARGMAX && v > best_val)) {
                    best_val = v;
                    best_idx = static_cast<int64_t>(i);
                }
            }
            vm_state->registers[dst] = static_cast<uint64_t>(best_idx);
            vm_state->register_types[dst] = TYPE_NODE_ID;
            if (best_idx == 0) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
            else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;
        } else {
            uint64_t res = 0;
            if (op_id == BINARY_OP_ADD) {
                for (size_t i = 0; i < N; ++i) res += vec[i];
            } else if (op_id == BINARY_OP_MUL) {
                res = (N > 0) ? 1 : 0;
                for (size_t i = 0; i < N; ++i) res *= vec[i];
            } else if (op_id == BINARY_OP_MIN) {
                res = (N > 0) ? vec[0] : 0;
                for (size_t i = 1; i < N; ++i) res = std::min(res, vec[i]);
            } else if (op_id == BINARY_OP_MAX) {
                res = (N > 0) ? vec[0] : 0;
                for (size_t i = 1; i < N; ++i) res = std::max(res, vec[i]);
            } else {
                for (size_t i = 0; i < N; ++i) res += vec[i];
            }
            vm_state->registers[dst] = res;
            vm_state->register_types[dst] = TYPE_INT64;
            if (res == 0) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
            else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;
        }
    } else {
        vm_state->registers[dst] = vm_state->registers[src_vec];
        vm_state->register_types[dst] = TYPE_INT64;
        if (vm_state->registers[dst] == 0) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
        else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;
    }

    vm_state->pc++;
    DISPATCH();
}

