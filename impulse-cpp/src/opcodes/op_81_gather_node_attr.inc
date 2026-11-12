/**
 * @file op_81_gather_node_attr.inc
 * @brief Formal Contract for OP_GATHER_NODE_ATTR (0x81).
 *
 * Syntax:
 *   - flags=0: OP_GATHER_NODE_ATTR <dst_reg>, <frontier_reg> (payload: [domain:16 | attr:8 | frontier:8])
 *   - flags=0x80: OP_GATHER_NODE_ATTR <dst_reg>, <frontier_reg> (128-bit wide domain/attr)
 * Inputs:
 *   - dst_reg: Target register receiving gathered float vector
 *   - frontier_reg (payload_low8 or payload_low16): Frontier bitset or scalar node ID
 * Preconditions:
 *   - frontier_reg must be TYPE_BITSET_HANDLE, TYPE_NODE_ID, or TYPE_INT64
 * Outputs:
 *   - dst_reg set to TYPE_FLOAT_VECTOR
 *   - Flags: None
 */
#ifndef OP_81_CONTRACT_DEFINED
#define OP_81_CONTRACT_DEFINED
#define OP_81_DEFINED_FLAGS        0x80u
#define OP_81_ALLOWED_SRC_TYPES    (IMP_TYPE_BIT(TYPE_BITSET_HANDLE) | IMP_TYPES_INT_OR_NODE | IMP_TYPES_NUMERIC_VEC | IMP_TYPE_BIT(TYPE_NULL))
#define OP_81_OUTPUT_DST_TYPES     (IMP_TYPE_BIT(TYPE_FLOAT_VECTOR))
#endif

// Opcode 0x81: OP_GATHER_NODE_ATTR
op_GATHER_NODE_ATTR: {
    IMPULSE_ASSERT(vm_state != nullptr);
    const auto& inst = bytecode[vm_state->pc];
    const uint16_t dst = IMP_INST_DST(inst);
    uint16_t frontier_reg;
    uint16_t attr_idx;
    uint16_t domain;
    const bool is_ext = IMP_INST_IS_EXTENDED(inst);
    IMPULSE_TESTCASE(is_ext);
    if (is_ext) {
        if (vm_state->pc + 1 >= instruction_count) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        frontier_reg = IMP_INST_LOW16(inst);
        attr_idx = IMP_INST_HIGH16(inst);
        domain = IMP_INST_DST(bytecode[vm_state->pc + 1]);
        vm_state->pc++;
    } else {
        frontier_reg = IMP_INST_U8_0(inst);
        attr_idx = IMP_INST_U8_1(inst);
        domain = IMP_INST_HIGH16(inst);
    }
    IMP_VALIDATE_GPR(dst);
    IMP_VALIDATE_GPR(frontier_reg);

    IMP_REQUIRE_CONTEXT(vm_state);
    IMPULSE_ASSERT(vm_state->query_context != nullptr);
    IMPULSE_ASSERT(dst < IMP_VM_GPR_COUNT && frontier_reg < IMP_VM_GPR_COUNT);
    IMPULSE_CHECK_REG_TYPE(vm_state, frontier_reg, OP_81_ALLOWED_SRC_TYPES);

    const bool front_is_bs = (vm_state->register_types[frontier_reg] == TYPE_BITSET_HANDLE);
    const int h_front = front_is_bs ? static_cast<int>(vm_state->registers[frontier_reg]) : -1;
    const uint64_t front_scalar = !front_is_bs ? vm_state->registers[frontier_reg] : 0ULL;
    IMPULSE_TESTCASE(front_is_bs);
    if (front_is_bs && (h_front < 0 || static_cast<size_t>(h_front) >= vm_state->query_context->bitsets.size())) {
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }

    const size_t N = vm_state->query_context->max_nodes;
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

    const uint32_t mock_key = (domain != 0u) ? domain : attr_idx;
    const auto* ma_ptr = find_mock_node_attr(vm_state->query_context, mock_key);
    IMPULSE_TESTCASE(ma_ptr != nullptr);
    if (ma_ptr) {
        const auto& ma = *ma_ptr;
        const size_t len = ma.float_data.size();
        vm_state->query_context->float_vector_lengths[h_dst] = len;
        const size_t limit = (len > 0) ? len : N;
        for (size_t i = 0; i < limit; ++i) {
            const bool in_frontier = front_is_bs ? vm_state->query_context->bitsets[h_front].test(i) : true;
            if (in_frontier && (!ma.has_mask || ma.mask.test(i)) && i < ma.float_data.size()) {
                dst_vec[i] = ma.float_data[i];
            } else {
                dst_vec[i] = std::numeric_limits<float>::quiet_NaN();
            }
        }
    } else if (const BoundAttributeSlot* slot_ptr = resolve_node_attribute_slot(vm_state->query_context, domain, attr_idx);
               slot_ptr != nullptr) {
        const auto& slot = *slot_ptr;
        const bool is_null = IMP_ATTR_IS_NULLABLE(slot.type_code);
        IMPULSE_TESTCASE(is_null);
        const size_t dom_rows = resolve_domain_row_count(vm_state->query_context, domain);
        const size_t bm_bytes = is_null ? static_cast<size_t>(IMP_VALIDITY_PADDED_BYTES(dom_rows)) : 0;
        const uint8_t* raw_b = static_cast<const uint8_t*>(slot.data_ptr);
        const float* src_attr = (slot.data_ptr && slot.data_bytes > bm_bytes)
            ? reinterpret_cast<const float*>(raw_b + bm_bytes)
            : nullptr;
        const size_t attr_n = src_attr ? std::min(N, static_cast<size_t>((slot.data_bytes - bm_bytes) / sizeof(float))) : 0;
        IMPULSE_TESTCASE(attr_n == 0);
        vm_state->query_context->float_vector_lengths[h_dst] = attr_n;
        if (front_is_bs) {
            const auto& bs = vm_state->query_context->bitsets[h_front];
            #pragma omp parallel for schedule(static)
            for (size_t i = 0; i < attr_n; ++i) {
                const bool valid = !is_null || IMP_VALIDITY_IS_VALID(raw_b, i);
                dst_vec[i] = (bs.test(i) && valid) ? src_attr[i] : std::numeric_limits<float>::quiet_NaN();
            }
        } else {
            for (size_t i = 0; i < attr_n; ++i) {
                const bool valid = !is_null || IMP_VALIDITY_IS_VALID(raw_b, i);
                dst_vec[i] = (i == front_scalar && valid) ? src_attr[i] : std::numeric_limits<float>::quiet_NaN();
            }
        }
    } else {
        vm_state->query_context->float_vector_lengths[h_dst] = 0;
    }
    vm_state->pc++;
    DISPATCH();
}
