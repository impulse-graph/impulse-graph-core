#ifndef OP_95_CONTRACT_DEFINED
#define OP_95_CONTRACT_DEFINED
/**
 * @file op_95_dense_walk_direct_store.inc
 * @brief Opcode 0x95: OP_DENSE_WALK_DIRECT_STORE — Direct store dense CSR frontier walk into bitset.
 *
 * Formal Opcode Contract:
 *  - Opcode: 0x95 (OP_DENSE_WALK_DIRECT_STORE)
 *  - Destination: dst_reg (RegFieldLayout::DST_REG)
 *  - Source: payload & 0xFF (RegFieldLayout::PAYLOAD_LOW8)
 *  - Relation: (payload >> 16) & 0xFFFF
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
    impulse_vm_status_t st = execute_dense_walk_common(bytecode, vm_state, OP_95_ALLOWED_SRC_TYPES);
    if (st != IMPULSE_VM_OK) return st;
    vm_state->pc++;
    DISPATCH();
}
