#ifndef OP_94_CONTRACT_DEFINED
#define OP_94_CONTRACT_DEFINED
/**
 * @file op_94_dense_walk_reduce.inc
 * @brief Opcode 0x94: OP_DENSE_WALK_REDUCE — Prepares float vector destination register for dense reduction.
 *
 * Formal Opcode Contract:
 *  - Opcode: 0x94 (OP_DENSE_WALK_REDUCE)
 *  - Destination: dst_reg (RegFieldLayout::DST_REG)
 *  - Source: None
 *  - Allowed Source Types: None
 *  - Output Destination Types: TYPE_FLOAT_VECTOR
 *  - Flags: None
 *  - Requirements: Requires valid query_context
 *  - Modifies Destination: True
 */
#define OP_94_DEFINED_FLAGS        0x00u
#define OP_94_OUTPUT_DST_TYPES     (IMP_TYPE_BIT(TYPE_FLOAT_VECTOR))
#endif

// Opcode 0x94: OP_DENSE_WALK_REDUCE
op_DENSE_WALK_REDUCE: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    VALIDATE_REG(dst);
    if (!vm_state->query_context) return IMPULSE_VM_ERR_NULL_SNAPSHOT;

    if (vm_state->register_types[dst] != TYPE_FLOAT_VECTOR) {
        int h = acquire_float_vector(vm_state->query_context);
        if (h < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        vm_state->registers[dst] = static_cast<uint64_t>(h);
        vm_state->register_types[dst] = TYPE_FLOAT_VECTOR;
    }
    vm_state->pc++;
    DISPATCH();
}
