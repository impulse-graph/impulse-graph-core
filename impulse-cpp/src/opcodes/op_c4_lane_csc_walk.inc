/**
 * Formal Opcode Contract: OP_LANE_CSC_WALK (0xC4)
 *
 * Layout:
 *   dst_reg:          inst.dst_reg (DST_REG)
 *   src_reg:          inst.payload & 0xFF (PAYLOAD_LOW8)
 *   rel_id:           (inst.payload >> 16) & 0xFFFF
 *   allowed_src:      TYPE_LANE_FRONTIER
 *   output_dst:       TYPE_LANE_FRONTIER
 *   flags:
 *     0x00: Multi-Lane CSC Walk (overwrite destination)
 *     0x01: Multi-Lane CSC Walk with Accumulate (union with destination)
 *     ZF set if active frontier mask is empty
 *   requires_ctx:     true
 */
#ifndef OP_C4_CONTRACT_DEFINED
#define OP_C4_CONTRACT_DEFINED
#define OP_C4_DEFINED_FLAGS        0x03u
#define OP_C4_ALLOWED_SRC_TYPES    (IMP_TYPE_BIT(TYPE_LANE_FRONTIER))
#define OP_C4_OUTPUT_DST_TYPES     (IMP_TYPE_BIT(TYPE_LANE_FRONTIER))
#endif

// Opcode 0xC4: OP_LANE_CSC_WALK
op_LANE_CSC_WALK: {
    impulse_vm_status_t st = execute_lane_walk_common<true>(bytecode, vm_state);
    if (st != IMPULSE_VM_OK) return st;
    vm_state->pc++;
    DISPATCH();
}
