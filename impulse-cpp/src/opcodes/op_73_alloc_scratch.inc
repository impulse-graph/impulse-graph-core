/**
 * @file op_73_alloc_scratch.inc
 * @brief Formal Contract for OP_ALLOC_SCRATCH (0x73).
 *
 * Syntax: OP_ALLOC_SCRATCH <dst_reg> (payload: [requested_bytes:32])
 * Inputs:
 *   - dst_reg: Target register receiving 64-byte aligned scratch allocation size
 *   - payload: Requested allocation size in bytes
 * Preconditions:
 *   - dst_reg must be valid register index (0..63)
 * Outputs:
 *   - dst_reg set to aligned byte size as TYPE_INT64
 *   - Flags: ZF cleared
 */
#ifndef OP_73_CONTRACT_DEFINED
#define OP_73_CONTRACT_DEFINED
#define OP_73_DEFINED_FLAGS        0x00u
#define OP_73_ALLOWED_SRC_TYPES    (IMP_TYPES_NONE)
#define OP_73_OUTPUT_DST_TYPES     (IMP_TYPE_BIT(TYPE_INT64))
#endif

// Opcode 0x73: OP_ALLOC_SCRATCH
op_ALLOC_SCRATCH: {
    const impulse_instruction_t& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint32_t req_bytes = inst.payload;
    VALIDATE_REG(dst);

    uint64_t aligned = (static_cast<uint64_t>(req_bytes) + 63ULL) & ~63ULL;
    vm_state->registers[dst] = aligned;
    vm_state->register_types[dst] = TYPE_INT64;
    vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

    vm_state->pc++;
    DISPATCH();
}

