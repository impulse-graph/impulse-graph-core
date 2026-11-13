/**
 * @file op_17_csr_walk_reduce.inc
 * @brief Formal Contract for OP_CSR_WALK_REDUCE (0x17).
 *
 * Mnemonic: OP_CSR_WALK_REDUCE
 * Opcode: 0x17
 * Description: Generalized CSR traversal with user-specified binary reduction monoid.
 *
 * Operands:
 *  - dst = inst.dst_reg, src = payload & 0xFF, op = (payload >> 8) & 0xFF, rel = (payload >> 16) & 0xFF
 *
 * Inputs:
 *  - src: TYPE_FLOAT_VECTOR | TYPE_DOUBLE_VECTOR | TYPE_INT64 | TYPE_NODE_ID
 *
 * Outputs:
 *  - dst: TYPE_FLOAT_VECTOR | TYPE_DOUBLE_VECTOR
 *
 * Flags:
 *  - ZF: Set if result is all zeros
 *
 * Context Requirement:
 *  - Requires context (query_context snapshot and vector pool)
 */
#ifndef OP_17_CONTRACT_DEFINED
#define OP_17_CONTRACT_DEFINED 1
#define OP_17_ALLOWED_SRC_TYPES (IMP_TYPES_FLOAT_OR_DBL | IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_BITSET_HANDLE))
#define OP_17_REDUCE_MIN        IMP_CSR_REDUCE_OP_MIN
#define OP_17_REDUCE_MAX        IMP_CSR_REDUCE_OP_MAX
#define OP_17_REDUCE_SUM        IMP_CSR_REDUCE_OP_SUM
#endif

// Opcode 0x17: OP_CSR_WALK_REDUCE
op_CSR_WALK_REDUCE: {
    const auto& inst = bytecode[vm_state->pc];
    const uint16_t dst = IMP_INST_DST(inst);
    const uint8_t src = IMP_INST_U8_0(inst);
    const uint8_t reduce_op = IMP_INST_U8_1(inst);
    const uint8_t rel_id = IMP_INST_U8_2(inst);
    VALIDATE_REG(dst);
    VALIDATE_REG(src);
    IMPULSE_ASSERT(dst < IMP_VM_GPR_COUNT && src < IMP_VM_GPR_COUNT);
    IMP_REQUIRE_CONTEXT(vm_state);
    IMPULSE_ASSERT(vm_state->query_context != nullptr);
    IMPULSE_CHECK_REG_TYPE(vm_state, src, OP_17_ALLOWED_SRC_TYPES);
    if (vm_state->register_types[src] == TYPE_NULL ||
        ((1ULL << vm_state->register_types[src]) & OP_17_ALLOWED_SRC_TYPES) == 0) {
        return IMPULSE_VM_ERR_INVALID_REGISTER;
    }

    IMP_REQUIRE_REL_SLOT(vm_state, rel_id);
    const auto& slot = vm_state->query_context->slots[rel_id];

    bool src_is_double = (vm_state->register_types[src] == TYPE_DOUBLE_VECTOR);
    size_t max_nodes = vm_state->query_context->max_nodes;
    IMPULSE_TESTCASE(src_is_double);
    IMPULSE_TESTCASE(reduce_op == OP_17_REDUCE_MIN);
    IMPULSE_TESTCASE(reduce_op == OP_17_REDUCE_MAX);

    if (vm_state->register_types[src] == TYPE_NODE_ID || vm_state->register_types[src] == TYPE_INT64) {
        int h_dst = acquire_float_vector(vm_state->query_context);
        if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        IMPULSE_ASSERT(h_dst >= 0 && static_cast<size_t>(h_dst) < vm_state->query_context->float_vectors.size());
        vm_state->registers[dst] = static_cast<uint64_t>(h_dst);
        vm_state->register_types[dst] = TYPE_FLOAT_VECTOR;
        IMP_VM_SET_ZF(vm_state, true);
        vm_state->pc++;
        DISPATCH();
    }
    int h_dst = acquire_float_vector(vm_state->query_context);
    if (h_dst < 0) {
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
    IMPULSE_ASSERT(h_dst >= 0 && static_cast<size_t>(h_dst) < vm_state->query_context->float_vectors.size());
    float* dst_vec = vm_state->query_context->float_vectors[h_dst].data();
    std::fill(dst_vec, dst_vec + max_nodes, 0.0f);

    if (slot.offsets_ptr && slot.targets_ptr) {
        if (src_is_double) {
            const double* src_vec = vm_state->query_context->double_vectors[vm_state->registers[src]].data();
            for (uint64_t u = 0; u < slot.node_count; ++u) {
                double src_val = src_vec[u];
                uint64_t start = slot.get_csr_offset(u);
                uint64_t end   = slot.get_csr_offset(u + 1);
                IMPULSE_WELL_FORMED_ASSERT(start <= end);
                for (uint64_t idx = start; idx < end; ++idx) {
                    uint64_t target_node = slot.get_csr_target(idx);
                    if (target_node < max_nodes) {
                        float val = static_cast<float>(src_val);
                        if (reduce_op == OP_17_REDUCE_MIN) {
                            if (dst_vec[target_node] == 0.0f || val < dst_vec[target_node]) dst_vec[target_node] = val;
                        } else if (reduce_op == OP_17_REDUCE_MAX) {
                            if (val > dst_vec[target_node]) dst_vec[target_node] = val;
                        } else {
                            dst_vec[target_node] = val;
                        }
                    }
                }
            }
        } else if (vm_state->register_types[src] == TYPE_FLOAT_VECTOR) {
            const float* src_vec = vm_state->query_context->float_vectors[vm_state->registers[src]].data();
            for (uint64_t u = 0; u < slot.node_count; ++u) {
                float src_val = src_vec[u];
                uint64_t start = slot.get_csr_offset(u);
                uint64_t end   = slot.get_csr_offset(u + 1);
                IMPULSE_WELL_FORMED_ASSERT(start <= end);
                for (uint64_t idx = start; idx < end; ++idx) {
                    uint64_t target_node = slot.get_csr_target(idx);
                    if (target_node < max_nodes) {
                        float val = src_val;
                        if (reduce_op == OP_17_REDUCE_MIN) {
                            if (dst_vec[target_node] == 0.0f || val < dst_vec[target_node]) dst_vec[target_node] = val;
                        } else if (reduce_op == OP_17_REDUCE_MAX) {
                            if (val > dst_vec[target_node]) dst_vec[target_node] = val;
                        } else {
                            dst_vec[target_node] = val;
                        }
                    }
                }
            }
        }
    }

    vm_state->registers[dst] = static_cast<uint64_t>(h_dst);
    vm_state->register_types[dst] = TYPE_FLOAT_VECTOR;

    bool is_empty = true;
    for (size_t i = 0; i < max_nodes; ++i) {
        if (dst_vec[i] != 0.0f) { is_empty = false; break; }
    }
    IMP_VM_SET_ZF(vm_state, is_empty);

    vm_state->pc++;
    DISPATCH();
}

