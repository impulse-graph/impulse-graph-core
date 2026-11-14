/**
 * @file op_72_load_indirect.inc
 * @brief Formal Contract for OP_LOAD_INDIRECT (0x72).
 *
 * Syntax:
 *   - flags=0: OP_LOAD_INDIRECT <dst_reg>, <src_param_reg> (register dereference)
 *   - flags=1: OP_LOAD_INDIRECT <dst_reg>, <src_vec_reg>, <idx_reg> (vector element dereference)
 * Inputs:
 *   - dst_reg: Destination register
 *   - src_param_reg (payload_low16): Register holding register index OR vector handle
 *   - idx_reg (payload_high16): Element index within vector (when flags=1)
 * Preconditions:
 *   - flags=0: src_param_reg must be TYPE_INT64 or TYPE_NODE_ID
 *   - flags=1: src_param_reg must be TYPE_FLOAT_VECTOR, idx_reg must be TYPE_INT64 or TYPE_NODE_ID
 * Outputs:
 *   - flags=0: dst_reg receives value/type from dereferenced register
 *   - flags=1: dst_reg set to TYPE_FLOAT
 *   - Flags: None
 */
#ifndef OP_72_CONTRACT_DEFINED
#define OP_72_CONTRACT_DEFINED
#define OP_72_DEFINED_FLAGS        0x01u
#define OP_72_FLAG0_SRC_TYPES      (IMP_TYPES_INT_OR_NODE)
#define OP_72_FLAG1_SRC_TYPES      (IMP_TYPE_BIT(TYPE_FLOAT_VECTOR) | IMP_TYPES_INT_OR_NODE)
#define OP_72_FLAG1_AUX1_TYPES     (IMP_TYPES_INT_OR_NODE)
#define OP_72_OUTPUT_FLOAT_DST     (IMP_TYPE_BIT(TYPE_FLOAT))
#define OP_72_FLAG_VECTOR_ELEM     IMP_FLAG_BIT0
#endif

// Opcode 0x72: OP_LOAD_INDIRECT
op_LOAD_INDIRECT: {
    IMPULSE_ASSERT(vm_state != nullptr);
    const impulse_instruction_t& inst = bytecode[vm_state->pc];
    const uint16_t dst = IMP_INST_DST(inst);
    const uint16_t src_param = IMP_INST_LOW16(inst);
    const uint16_t index_reg = IMP_INST_HIGH16(inst);
    IMP_VALIDATE_GPR(dst);
    IMPULSE_ASSERT(dst < IMP_VM_GPR_COUNT);

    if (IMP_INST_FLAGS(inst) != IMP_FLAG_NONE && IMP_INST_FLAGS(inst) != OP_72_FLAG_VECTOR_ELEM) {
        return IMPULSE_VM_ERR_INVALID_OPCODE;
    }
    const bool reg_deref_mode = (IMP_INST_FLAGS(inst) == IMP_FLAG_NONE);
    IMPULSE_TESTCASE(reg_deref_mode);

    if (reg_deref_mode) {
        IMP_VALIDATE_GPR(src_param);
        IMPULSE_CHECK_REG_TYPE(vm_state, src_param, OP_72_FLAG0_SRC_TYPES);
        if (vm_state->register_types[src_param] != TYPE_INT64 &&
            vm_state->register_types[src_param] != TYPE_NODE_ID) {
            return IMPULSE_VM_ERR_INVALID_REGISTER;
        }
        const uint64_t target_reg_idx = vm_state->registers[src_param];
        IMP_VALIDATE_GPR(target_reg_idx);
        if (target_reg_idx >= IMP_VM_GPR_COUNT) {
            return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        }
        IMPULSE_ASSERT(target_reg_idx < IMP_VM_GPR_COUNT);
        IMPULSE_TESTCASE(target_reg_idx == dst);
        const uint64_t val = vm_state->registers[target_reg_idx];
        const uint8_t type = vm_state->register_types[target_reg_idx];
        vm_state->registers[dst] = val;
        vm_state->register_types[dst] = type;
    } else {
        IMP_VALIDATE_GPR(src_param);
        IMP_VALIDATE_GPR(index_reg);
        IMP_REQUIRE_CONTEXT(vm_state);
        IMPULSE_ASSERT(vm_state->query_context != nullptr);
        IMPULSE_CHECK_REG_TYPE(vm_state, src_param, OP_72_FLAG1_SRC_TYPES);
        IMPULSE_CHECK_REG_TYPE(vm_state, index_reg, OP_72_FLAG1_AUX1_TYPES);
        const int h_vec = static_cast<int>(vm_state->registers[src_param]);
        const uint64_t idx = vm_state->registers[index_reg];
        if (h_vec < 0 || static_cast<size_t>(h_vec) >= vm_state->query_context->float_vectors.size()) {
            return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        }
        const auto& vec = vm_state->query_context->float_vectors[h_vec];
        IMPULSE_TESTCASE(idx == 0ULL);
        IMPULSE_TESTCASE(idx >= vec.size());
        if (idx >= vec.size()) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        IMPULSE_ASSERT(idx < vec.size());
        const float val = vec[idx];
        uint32_t raw_bits = 0u;
        std::memcpy(&raw_bits, &val, sizeof(uint32_t));
        vm_state->registers[dst] = raw_bits;
        vm_state->register_types[dst] = TYPE_FLOAT;
    }

    vm_state->pc++;
    DISPATCH();
}
