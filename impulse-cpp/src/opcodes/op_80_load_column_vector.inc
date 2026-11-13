/**
 * @file op_80_load_column_vector.inc
 * @brief Formal Contract for OP_LOAD_COLUMN_VECTOR (0x80).
 *
 * Syntax: OP_LOAD_COLUMN_VECTOR <dst_reg> (payload: [attr_idx:16 | domain_or_rel:16])
 * Inputs:
 *   - dst_reg: Target register receiving off-heap float/double column vector
 *   - payload: High 16 bits = attribute index, low 16 bits = domain / relation ID
 * Preconditions:
 *   - Snapshot attribute slot must exist and contain float/double data
 * Outputs:
 *   - dst_reg set to TYPE_FLOAT_VECTOR or TYPE_DOUBLE_VECTOR
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
    IMPULSE_ASSERT(vm_state != nullptr);
    const auto& inst = bytecode[vm_state->pc];
    const uint16_t dst = IMP_INST_DST(inst);
    const uint16_t domain_or_rel = IMP_INST_LOW16(inst);
    const uint16_t attr_idx = IMP_INST_HIGH16(inst);
    IMP_VALIDATE_GPR(dst);

    IMP_REQUIRE_CONTEXT(vm_state);
    IMPULSE_ASSERT(vm_state->query_context != nullptr);
    IMPULSE_ASSERT(dst < IMP_VM_GPR_COUNT);

    const size_t N = vm_state->query_context->max_nodes;

    const BoundAttributeSlot* slot_ptr = resolve_node_attribute_slot(vm_state->query_context, domain_or_rel, attr_idx);
    IMPULSE_TESTCASE(slot_ptr != nullptr);
    if (slot_ptr == nullptr) {
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
    if (slot_ptr != nullptr) {
        const auto& slot = *slot_ptr;
        const uint8_t base_tc = IMP_ATTR_BASE_TYPE(slot.type_code);
        const bool is_nullable = IMP_ATTR_IS_NULLABLE(slot.type_code);
        IMPULSE_TESTCASE(is_nullable);
        IMPULSE_TESTCASE(base_tc == IMP_TYPE_CODE_FLOAT64);

        const size_t elem_size = (base_tc == IMP_TYPE_CODE_FLOAT64) ? sizeof(double) : sizeof(float);
        size_t bitmap_bytes = 0;
        if (is_nullable && slot.data_bytes > IMP_SNAP_SECTION_ALIGN) {
            for (size_t b = static_cast<size_t>(IMP_SNAP_SECTION_ALIGN);
                 b < slot.data_bytes;
                 b += static_cast<size_t>(IMP_SNAP_SECTION_ALIGN)) {
                const size_t rem = slot.data_bytes - b;
                const size_t cnt = rem / elem_size;
                const size_t expected_b = static_cast<size_t>(IMP_VALIDITY_PADDED_BYTES(cnt));
                if (expected_b == b) {
                    bitmap_bytes = b;
                    break;
                }
            }
            if (bitmap_bytes == 0) bitmap_bytes = static_cast<size_t>(IMP_SNAP_SECTION_ALIGN);
        }
        if (base_tc == IMP_TYPE_CODE_FLOAT64) {
            if (vm_state->register_types[dst] != TYPE_DOUBLE_VECTOR ||
                vm_state->registers[dst] >= vm_state->query_context->double_vectors.size()) {
                const int h = acquire_double_vector(vm_state->query_context);
                if (h < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                vm_state->registers[dst] = static_cast<uint64_t>(h);
                vm_state->register_types[dst] = TYPE_DOUBLE_VECTOR;
            }
            const int h_dst = static_cast<int>(vm_state->registers[dst]);
            IMPULSE_ASSERT(h_dst >= 0 && static_cast<size_t>(h_dst) < vm_state->query_context->double_vectors.size());
            double* dst_vec = vm_state->query_context->double_vectors[h_dst].data();
            size_t loaded_len = 0;
            if (slot.data_ptr && slot.data_bytes > bitmap_bytes) {
                const uint8_t* raw_bytes = static_cast<const uint8_t*>(slot.data_ptr);
                const double* src_attr = reinterpret_cast<const double*>(raw_bytes + bitmap_bytes);
                loaded_len = std::min(N, static_cast<size_t>((slot.data_bytes - bitmap_bytes) / sizeof(double)));
                std::memcpy(dst_vec, src_attr, loaded_len * sizeof(double));
                if (is_nullable) {
                    for (size_t i = 0; i < loaded_len; ++i) {
                        if (!IMP_VALIDITY_IS_VALID(raw_bytes, i)) {
                            dst_vec[i] = std::numeric_limits<double>::quiet_NaN();
                        }
                    }
                }
            }
            IMPULSE_TESTCASE(loaded_len == 0);
            vm_state->query_context->double_vector_lengths[h_dst] = loaded_len;
            vm_state->pc++;
            DISPATCH();
        } else {
            if (vm_state->register_types[dst] != TYPE_FLOAT_VECTOR ||
                vm_state->registers[dst] >= vm_state->query_context->float_vectors.size()) {
                const int h = acquire_float_vector(vm_state->query_context);
                if (h < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
                vm_state->registers[dst] = static_cast<uint64_t>(h);
                vm_state->register_types[dst] = TYPE_FLOAT_VECTOR;
            }
            const int h_dst = static_cast<int>(vm_state->registers[dst]);
            IMPULSE_ASSERT(h_dst >= 0 && static_cast<size_t>(h_dst) < vm_state->query_context->float_vectors.size());
            float* dst_vec = vm_state->query_context->float_vectors[h_dst].data();
            size_t loaded_len = 0;
            if (slot.data_ptr && slot.data_bytes > bitmap_bytes) {
                const uint8_t* raw_bytes = static_cast<const uint8_t*>(slot.data_ptr);
                const float* src_attr = reinterpret_cast<const float*>(raw_bytes + bitmap_bytes);
                loaded_len = std::min(N, static_cast<size_t>((slot.data_bytes - bitmap_bytes) / sizeof(float)));
                std::memcpy(dst_vec, src_attr, loaded_len * sizeof(float));
                if (is_nullable) {
                    for (size_t i = 0; i < loaded_len; ++i) {
                        if (!IMP_VALIDITY_IS_VALID(raw_bytes, i)) {
                            dst_vec[i] = std::numeric_limits<float>::quiet_NaN();
                        }
                    }
                }
            }
            IMPULSE_TESTCASE(loaded_len == 0);
            vm_state->query_context->float_vector_lengths[h_dst] = loaded_len;
            vm_state->pc++;
            DISPATCH();
        }
    }

    if (vm_state->register_types[dst] != TYPE_FLOAT_VECTOR ||
        vm_state->registers[dst] >= vm_state->query_context->float_vectors.size()) {
        const int h = acquire_float_vector(vm_state->query_context);
        if (h < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        vm_state->registers[dst] = static_cast<uint64_t>(h);
        vm_state->register_types[dst] = TYPE_FLOAT_VECTOR;
    }
    const int h_dst = static_cast<int>(vm_state->registers[dst]);
    IMPULSE_ASSERT(h_dst >= 0 && static_cast<size_t>(h_dst) < vm_state->query_context->float_vectors.size());
    float* dst_vec = vm_state->query_context->float_vectors[h_dst].data();
    size_t loaded_len = 0;
    const auto* ma_ptr = find_mock_node_attr(vm_state->query_context, attr_idx);
    if (!ma_ptr) ma_ptr = find_mock_node_attr(vm_state->query_context, domain_or_rel);
    if (ma_ptr && !ma_ptr->float_data.empty()) {
        loaded_len = std::min(N, ma_ptr->float_data.size());
        std::memcpy(dst_vec, ma_ptr->float_data.data(), loaded_len * sizeof(float));
    }
    IMPULSE_TESTCASE(loaded_len == 0);
    vm_state->query_context->float_vector_lengths[h_dst] = loaded_len;
    vm_state->pc++;
    DISPATCH();
}
