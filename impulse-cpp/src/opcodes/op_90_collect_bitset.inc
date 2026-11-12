#ifndef OP_90_CONTRACT_DEFINED
#define OP_90_CONTRACT_DEFINED
/**
 * @file op_90_collect_bitset.inc
 * @brief Opcode 0x90: OP_COLLECT_BITSET — Collect active frontier bitset into destination register.
 *
 * Formal Opcode Contract:
 *  - Opcode: 0x90 (OP_COLLECT_BITSET)
 *  - Destination: dst_reg (RegFieldLayout::DST_REG)
 *  - Source: IMP_INST_LOW16(inst) (RegFieldLayout::PAYLOAD_LOW16)
 *  - Allowed Source Types: TYPE_BITSET_HANDLE, TYPE_LANE_FRONTIER, TYPE_NODE_VECTOR, TYPE_INT_OR_NODE
 *  - Output Destination Types: Matches source register type
 *  - Flags: Preserved
 *  - Requirements: None
 *  - Modifies Destination: True
 */
#define OP_90_DEFINED_FLAGS        0x00u
#define OP_90_ALLOWED_SRC_TYPES    (IMP_TYPE_BIT(TYPE_NULL) | IMP_TYPE_BIT(TYPE_BITSET_HANDLE) | IMP_TYPE_BIT(TYPE_LANE_FRONTIER) | IMP_TYPES_NODE_VEC | IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_FRONTIER_STATE))
#define OP_90_OUTPUT_DST_TYPES     (IMP_TYPE_BIT(TYPE_NULL) | IMP_TYPE_BIT(TYPE_BITSET_HANDLE) | IMP_TYPE_BIT(TYPE_LANE_FRONTIER) | IMP_TYPES_NODE_VEC | IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_FRONTIER_STATE))
#endif

// Opcode 0x90: OP_COLLECT_BITSET
op_COLLECT_BITSET: {
    IMPULSE_ASSERT(vm_state != nullptr);
    const auto& inst = bytecode[vm_state->pc];
    const uint16_t dst = IMP_INST_DST(inst);
    const uint16_t src = IMP_INST_LOW16(inst);
    IMP_VALIDATE_GPR(dst);
    IMP_VALIDATE_GPR(src);
    IMPULSE_ASSERT(dst < IMP_VM_GPR_COUNT && src < IMP_VM_GPR_COUNT);
    IMPULSE_CHECK_REG_TYPE(vm_state, src, OP_90_ALLOWED_SRC_TYPES);

    const uint64_t val = vm_state->registers[src];
    const uint8_t type = vm_state->register_types[src];
    IMPULSE_TESTCASE(dst == src);
    IMPULSE_TESTCASE(type == TYPE_NULL);
    vm_state->registers[dst] = val;
    vm_state->register_types[dst] = type;

    vm_state->pc++;
    DISPATCH();
}
