/**
 * @file op_82_gather_edge_attr.inc
 * @brief Formal Contract for OP_GATHER_EDGE_ATTR (0x82).
 *
 * Syntax: OP_GATHER_EDGE_ATTR <dst_reg>, <src_reg>
 * Inputs:
 *   - dst_reg: Target register receiving gathered edge attribute vector
 *   - src_reg (payload_low8 or payload_low16): Source edge frontier or handle
 * Preconditions:
 *   - src_reg must be TYPE_BITSET_HANDLE, TYPE_NODE_ID, or TYPE_INT64
 * Outputs:
 *   - dst_reg set to TYPE_FLOAT_VECTOR
 *   - Flags: None
 */
#ifndef OP_82_CONTRACT_DEFINED
#define OP_82_CONTRACT_DEFINED
#define OP_82_DEFINED_FLAGS        0x80u
#define OP_82_ALLOWED_SRC_TYPES    (IMP_TYPE_BIT(TYPE_BITSET_HANDLE) | IMP_TYPES_INT_OR_NODE | IMP_TYPES_NUMERIC_VEC | IMP_TYPE_BIT(TYPE_NULL))
#define OP_82_OUTPUT_DST_TYPES     (IMP_TYPE_BIT(TYPE_FLOAT_VECTOR))
#endif

// Opcode 0x82: OP_GATHER_EDGE_ATTR
op_GATHER_EDGE_ATTR: {
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
    IMPULSE_CHECK_REG_TYPE(vm_state, frontier_reg, OP_82_ALLOWED_SRC_TYPES);

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
    } else if (domain < vm_state->query_context->attribute_slots.size() &&
               attr_idx < vm_state->query_context->attribute_slots[domain].size() &&
               vm_state->query_context->attribute_slots[domain][attr_idx].data_ptr) {
        const auto& slot = vm_state->query_context->attribute_slots[domain][attr_idx];
        const float* src_attr = static_cast<const float*>(slot.data_ptr);
        const size_t attr_n = std::min(N, static_cast<size_t>(slot.data_bytes / sizeof(float)));
        IMPULSE_TESTCASE(attr_n == 0);
        vm_state->query_context->float_vector_lengths[h_dst] = attr_n;
        if (front_is_bs) {
            const auto& bs = vm_state->query_context->bitsets[h_front];
            #pragma omp parallel for schedule(static)
            for (size_t i = 0; i < attr_n; ++i) {
                dst_vec[i] = bs.test(i) ? src_attr[i] : std::numeric_limits<float>::quiet_NaN();
            }
        } else {
            for (size_t i = 0; i < attr_n; ++i) {
                dst_vec[i] = (i == front_scalar) ? src_attr[i] : std::numeric_limits<float>::quiet_NaN();
            }
        }
    } else {
        vm_state->query_context->float_vector_lengths[h_dst] = 0;
    }
    vm_state->pc++;
    DISPATCH();
}
