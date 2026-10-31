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
#endif

// Opcode 0x72: OP_LOAD_INDIRECT
op_LOAD_INDIRECT: {
    const impulse_instruction_t& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src_param = inst.payload & 0xFFFF;
    uint16_t index_reg = (inst.payload >> 16) & 0xFFFF;
    VALIDATE_REG(dst);

    if (inst.flags == 0) {
        VALIDATE_REG(src_param);
        IMPULSE_CHECK_REG_TYPE(vm_state, src_param, OP_72_FLAG0_SRC_TYPES);
        uint64_t target_reg_idx = vm_state->registers[src_param];
        VALIDATE_REG(target_reg_idx);
        uint64_t val = vm_state->registers[target_reg_idx];
        uint8_t type = vm_state->register_types[target_reg_idx];
        vm_state->registers[dst] = val;
        vm_state->register_types[dst] = type;
    } else {
        VALIDATE_REG(src_param);
        VALIDATE_REG(index_reg);
        if (!vm_state->query_context) return IMPULSE_VM_ERR_NULL_SNAPSHOT;
        IMPULSE_CHECK_REG_TYPE(vm_state, src_param, OP_72_FLAG1_SRC_TYPES);
        IMPULSE_CHECK_REG_TYPE(vm_state, index_reg, OP_72_FLAG1_AUX1_TYPES);
        int h_vec = static_cast<int>(vm_state->registers[src_param]);
        uint64_t idx = vm_state->registers[index_reg];
        if (h_vec < 0 || h_vec >= static_cast<int>(vm_state->query_context->float_vectors.size())) {
            return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        }
        const auto& vec = vm_state->query_context->float_vectors[h_vec];
        if (idx >= vec.size()) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        float val = vec[idx];
        vm_state->registers[dst] = *reinterpret_cast<uint32_t*>(&val);
        vm_state->register_types[dst] = TYPE_FLOAT;
    }

    vm_state->pc++;
    DISPATCH();
}

