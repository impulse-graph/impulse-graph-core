#ifndef OP_90_CONTRACT_DEFINED
#define OP_90_CONTRACT_DEFINED
/**
 * @file op_90_collect_bitset.inc
 * @brief Opcode 0x90: OP_COLLECT_BITSET — Collect active frontier bitset into destination register.
 *
 * Formal Opcode Contract:
 *  - Opcode: 0x90 (OP_COLLECT_BITSET)
 *  - Destination: dst_reg (RegFieldLayout::DST_REG)
 *  - Source: payload & 0xFFFF (RegFieldLayout::PAYLOAD_LOW16)
 *  - Allowed Source Types: TYPE_BITSET_HANDLE, TYPE_LANE_FRONTIER, TYPE_NODE_VECTOR, TYPE_INT_OR_NODE
 *  - Output Destination Types: Matches source register type
 *  - Flags: Preserved
 *  - Requirements: None
 *  - Modifies Destination: True
 */
#define OP_90_DEFINED_FLAGS        0x00u
#define OP_90_ALLOWED_SRC_TYPES    (IMP_TYPE_BIT(TYPE_NULL) | IMP_TYPE_BIT(TYPE_BITSET_HANDLE) | IMP_TYPE_BIT(TYPE_LANE_FRONTIER) | IMP_TYPES_NODE_VEC | IMP_TYPES_INT_OR_NODE)
#define OP_90_OUTPUT_DST_TYPES     (IMP_TYPE_BIT(TYPE_NULL) | IMP_TYPE_BIT(TYPE_BITSET_HANDLE) | IMP_TYPE_BIT(TYPE_LANE_FRONTIER) | IMP_TYPES_NODE_VEC | IMP_TYPES_INT_OR_NODE)
#endif

// Opcode 0x90: OP_COLLECT_BITSET
op_COLLECT_BITSET: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src = inst.payload & 0xFFFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src);
    IMPULSE_CHECK_REG_TYPE(vm_state, src, OP_90_ALLOWED_SRC_TYPES);

    uint64_t val = vm_state->registers[src];
    uint8_t type = vm_state->register_types[src];
    vm_state->registers[dst] = val;
    vm_state->register_types[dst] = type;

    vm_state->pc++;
    DISPATCH();
}
