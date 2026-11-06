/**
 * @file op_80_load_column_vector.inc
 * @brief Formal Contract for OP_LOAD_COLUMN_VECTOR (0x80).
 *
 * Syntax: OP_LOAD_COLUMN_VECTOR <dst_reg> (payload: [attr_idx:16 | domain_or_rel:16])
 * Inputs:
 *   - dst_reg: Target register receiving off-heap float column vector
 *   - payload: High 16 bits = attribute index, low 16 bits = domain / relation ID
 * Preconditions:
 *   - Snapshot attribute slot must exist and contain float data
 * Outputs:
 *   - dst_reg set to TYPE_FLOAT_VECTOR
 *   - Flags: None
 */
#ifndef OP_80_CONTRACT_DEFINED
#define OP_80_CONTRACT_DEFINED
#define OP_80_DEFINED_FLAGS        0x00u
#define OP_80_ALLOWED_SRC_TYPES    (IMP_TYPES_NONE)
#define OP_80_OUTPUT_DST_TYPES     (IMP_TYPE_BIT(TYPE_FLOAT_VECTOR) | IMP_TYPE_BIT(TYPE_DOUBLE_VECTOR))
#endif

// Opcode 0x80: OP_LOAD_COLUMN_VECTOR
op_LOAD_COLUMN_VECTOR: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t domain_or_rel = inst.payload & 0xFFFF;
    uint16_t attr_idx = (inst.payload >> 16) & 0xFFFF;
    VALIDATE_REG(dst);

    if (!vm_state->query_context) return IMPULSE_VM_ERR_NULL_SNAPSHOT;

    size_t N = vm_state->query_context->max_nodes;

    if (domain_or_rel < vm_state->query_context->attribute_slots.size() &&
        attr_idx < vm_state->query_context->attribute_slots[domain_or_rel].size()) {
        const auto& slot = vm_state->query_context->attribute_slots[domain_or_rel][attr_idx];
        uint8_t base_tc = slot.type_code & 0x7F;
        bool is_nullable = (slot.type_code & 0x80) != 0;
        size_t elem_size = (base_tc == 7) ? sizeof(double) : sizeof(float);
        size_t bitmap_bytes = 0;
        if (is_nullable && slot.data_bytes > 128) {
            for (size_t b = 128; b < slot.data_bytes; b += 128) {
                size_t rem = slot.data_bytes - b;
                size_t cnt = rem / elem_size;
                size_t expected_b = ((((cnt + 63) / 64) * 8) + 127) & ~size_t(127);
                if (expected_b == b) {
                    bitmap_bytes = b;
                    break;
                }
            }
            if (bitmap_bytes == 0) bitmap_bytes = 128;
        }
        if (base_tc == 7) {
            if (vm_state->register_types[dst] != TYPE_DOUBLE_VECTOR) {
                int h = acquire_double_vector(vm_state->query_context);
                if (h < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                vm_state->registers[dst] = h;
                vm_state->register_types[dst] = TYPE_DOUBLE_VECTOR;
            }
            int h_dst = static_cast<int>(vm_state->registers[dst]);
            double* dst_vec = vm_state->query_context->double_vectors[h_dst].data();
            size_t loaded_len = 0;
            if (slot.data_ptr && slot.data_bytes > bitmap_bytes) {
                const uint8_t* raw_bytes = static_cast<const uint8_t*>(slot.data_ptr);
                const double* src_attr = reinterpret_cast<const double*>(raw_bytes + bitmap_bytes);
                loaded_len = std::min(N, static_cast<size_t>((slot.data_bytes - bitmap_bytes) / sizeof(double)));
                std::memcpy(dst_vec, src_attr, loaded_len * sizeof(double));
                if (is_nullable) {
                    for (size_t i = 0; i < loaded_len; ++i) {
                        if (((raw_bytes[i >> 3] >> (i & 7)) & 1U) == 0U) {
                            dst_vec[i] = std::numeric_limits<double>::quiet_NaN();
                        }
                    }
                }
            }
            vm_state->query_context->double_vector_lengths[h_dst] = loaded_len;
            vm_state->pc++;
            DISPATCH();
        } else {
            if (vm_state->register_types[dst] != TYPE_FLOAT_VECTOR) {
                int h = acquire_float_vector(vm_state->query_context);
                if (h < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                vm_state->registers[dst] = h;
                vm_state->register_types[dst] = TYPE_FLOAT_VECTOR;
            }
            int h_dst = static_cast<int>(vm_state->registers[dst]);
            float* dst_vec = vm_state->query_context->float_vectors[h_dst].data();
            size_t loaded_len = 0;
            if (slot.data_ptr && slot.data_bytes > bitmap_bytes) {
                const uint8_t* raw_bytes = static_cast<const uint8_t*>(slot.data_ptr);
                const float* src_attr = reinterpret_cast<const float*>(raw_bytes + bitmap_bytes);
                loaded_len = std::min(N, static_cast<size_t>((slot.data_bytes - bitmap_bytes) / sizeof(float)));
                std::memcpy(dst_vec, src_attr, loaded_len * sizeof(float));
                if (is_nullable) {
                    for (size_t i = 0; i < loaded_len; ++i) {
                        if (((raw_bytes[i >> 3] >> (i & 7)) & 1U) == 0U) {
                            dst_vec[i] = std::numeric_limits<float>::quiet_NaN();
                        }
                    }
                }
            }
            vm_state->query_context->float_vector_lengths[h_dst] = loaded_len;
            vm_state->pc++;
            DISPATCH();
        }
    }

    if (vm_state->register_types[dst] != TYPE_FLOAT_VECTOR) {
        int h = acquire_float_vector(vm_state->query_context);
        if (h < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        vm_state->registers[dst] = h;
        vm_state->register_types[dst] = TYPE_FLOAT_VECTOR;
    }
    int h_dst = static_cast<int>(vm_state->registers[dst]);
    float* dst_vec = vm_state->query_context->float_vectors[h_dst].data();
    size_t loaded_len = 0;
    const auto* ma_ptr = find_mock_node_attr(vm_state->query_context, attr_idx);
    if (!ma_ptr) ma_ptr = find_mock_node_attr(vm_state->query_context, domain_or_rel);
    if (ma_ptr && !ma_ptr->float_data.empty()) {
        loaded_len = std::min(N, ma_ptr->float_data.size());
        std::memcpy(dst_vec, ma_ptr->float_data.data(), loaded_len * sizeof(float));
    }
    vm_state->query_context->float_vector_lengths[h_dst] = loaded_len;
    vm_state->pc++;
    DISPATCH();
}

