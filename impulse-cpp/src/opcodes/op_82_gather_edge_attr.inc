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
#define OP_82_ALLOWED_SRC_TYPES    (IMP_TYPE_BIT(TYPE_BITSET_HANDLE) | IMP_TYPES_INT_OR_NODE)
#define OP_82_OUTPUT_DST_TYPES     (IMP_TYPE_BIT(TYPE_FLOAT_VECTOR))
#endif

// Opcode 0x82: OP_GATHER_EDGE_ATTR
op_GATHER_EDGE_ATTR: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src = (inst.flags & 0x80) ? (inst.payload & 0xFFFF) : (inst.payload & 0xFF);
    if (inst.flags & 0x80) {
        if (vm_state->pc + 1 >= instruction_count) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        vm_state->pc++;
    }
    VALIDATE_REG(dst);
    VALIDATE_REG(src);

    if (!vm_state->query_context) return IMPULSE_VM_ERR_NULL_SNAPSHOT;
    IMPULSE_CHECK_REG_TYPE(vm_state, src, OP_82_ALLOWED_SRC_TYPES);

    if (vm_state->register_types[dst] != TYPE_FLOAT_VECTOR) {
        int h = acquire_float_vector(vm_state->query_context);
        if (h < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        vm_state->registers[dst] = h;
        vm_state->register_types[dst] = TYPE_FLOAT_VECTOR;
    }
    vm_state->pc++;
    DISPATCH();
}

