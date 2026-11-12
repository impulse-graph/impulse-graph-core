#ifndef OP_95_CONTRACT_DEFINED
#define OP_95_CONTRACT_DEFINED
/**
 * @file op_95_dense_walk_direct_store.inc
 * @brief Opcode 0x95: OP_DENSE_WALK_DIRECT_STORE — Direct store dense CSR frontier walk into bitset.
 *
 * Formal Opcode Contract:
 *  - Opcode: 0x95 (OP_DENSE_WALK_DIRECT_STORE)
 *  - Destination: dst_reg (RegFieldLayout::DST_REG)
 *  - Source: IMP_INST_U8_0(inst) (RegFieldLayout::PAYLOAD_LOW8)
 *  - Relation: IMP_INST_HIGH16(inst)
 *  - Allowed Source Types: TYPE_SOURCE_SET (Node ID, Int64, BitSet)
 *  - Output Destination Types: TYPE_BITSET_HANDLE
 *  - Flags: Sets IMPULSE_VM_FLAG_ZF if destination bitset is empty
 *  - Requirements: Requires valid query_context
 *  - Modifies Destination: True
 */
#define OP_95_DEFINED_FLAGS        0x00u
#define OP_95_ALLOWED_SRC_TYPES    (IMP_TYPES_SOURCE_SET)
#define OP_95_OUTPUT_DST_TYPES     (IMP_TYPE_BIT(TYPE_BITSET_HANDLE))
#endif

// Opcode 0x95: OP_DENSE_WALK_DIRECT_STORE
op_DENSE_WALK_DIRECT_STORE: {
    IMPULSE_ASSERT(vm_state != nullptr);
    IMPULSE_VVA_ONLY(const uint16_t dst = IMP_INST_DST(bytecode[vm_state->pc]);)
    const impulse_vm_status_t st = execute_dense_walk_common(bytecode, vm_state, OP_95_ALLOWED_SRC_TYPES);
    IMPULSE_TESTCASE(st != IMPULSE_VM_OK);
    if (st != IMPULSE_VM_OK) return st;
    IMPULSE_ASSERT(dst < IMP_VM_GPR_COUNT);
    IMPULSE_ASSERT((IMP_TYPE_BIT(vm_state->register_types[dst]) & OP_95_OUTPUT_DST_TYPES) != 0u);
    IMPULSE_TESTCASE(IMP_VM_IS_ZF(vm_state));
    vm_state->pc++;
    DISPATCH();
}
