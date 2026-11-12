/**
 * Formal Opcode Contract: OP_LANE_CSR_WALK (0xC3)
 *
 * Layout:
 *   dst_reg:          inst.dst_reg (DST_REG)
 *   src_reg:          inst.payload & 0xFF (PAYLOAD_LOW8)
 *   rel_id:           (inst.payload >> 16) & 0xFFFF
 *   allowed_src:      TYPE_LANE_FRONTIER
 *   output_dst:       TYPE_LANE_FRONTIER
 *   flags:
 *     0x00: Multi-Lane CSR Walk (overwrite destination)
 *     0x01: Multi-Lane CSR Walk with Accumulate (union with destination)
 *     ZF set if active frontier mask is empty
 *   requires_ctx:     true
 */
#ifndef OP_C3_CONTRACT_DEFINED
#define OP_C3_CONTRACT_DEFINED
#define OP_C3_DEFINED_FLAGS        0x03u
#define OP_C3_FLAG_ACCUMULATE      IMP_FLAG_ACCUMULATE_DEST
#define OP_C3_FLAG_FIXPOINT        IMP_FLAG_DELTA_FIXPOINT
#define OP_C3_ALLOWED_SRC_TYPES    (IMP_TYPE_BIT(TYPE_LANE_FRONTIER))
#define OP_C3_OUTPUT_DST_TYPES     (IMP_TYPE_BIT(TYPE_LANE_FRONTIER))
#endif

// Opcode 0xC3: OP_LANE_CSR_WALK
op_LANE_CSR_WALK: {
    IMPULSE_TESTCASE(IMP_INST_HAS_FLAG(bytecode[vm_state->pc], OP_C3_FLAG_FIXPOINT));
    impulse_vm_status_t st = execute_lane_walk_common<false>(bytecode, vm_state);
    if (st != IMPULSE_VM_OK) return st;
    IMPULSE_ASSERT(vm_state->register_types[IMP_INST_DST(bytecode[vm_state->pc])] == TYPE_LANE_FRONTIER);
    vm_state->pc++;
    DISPATCH();
}
