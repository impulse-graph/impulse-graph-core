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
#define OP_73_SCRATCH_ALIGN_BYTES  64ULL
#define OP_73_SCRATCH_ALIGN_MASK   (OP_73_SCRATCH_ALIGN_BYTES - 1ULL)
#endif

// Opcode 0x73: OP_ALLOC_SCRATCH
op_ALLOC_SCRATCH: {
    IMPULSE_ASSERT(vm_state != nullptr);
    const impulse_instruction_t& inst = bytecode[vm_state->pc];
    const uint16_t dst = IMP_INST_DST(inst);
    const uint32_t req_bytes = IMP_INST_PAYLOAD_U32(inst);
    IMP_VALIDATE_GPR(dst);
    IMPULSE_ASSERT(dst < IMP_VM_GPR_COUNT);

    IMPULSE_TESTCASE(req_bytes == 0u);
    IMPULSE_TESTCASE((static_cast<uint64_t>(req_bytes) & OP_73_SCRATCH_ALIGN_MASK) == 0ULL);

    const uint64_t aligned = (static_cast<uint64_t>(req_bytes) + OP_73_SCRATCH_ALIGN_MASK) & ~OP_73_SCRATCH_ALIGN_MASK;
    IMPULSE_ASSERT((aligned & OP_73_SCRATCH_ALIGN_MASK) == 0ULL);
    IMPULSE_ASSERT(aligned >= static_cast<uint64_t>(req_bytes));

    vm_state->registers[dst] = aligned;
    vm_state->register_types[dst] = TYPE_INT64;
    IMP_VM_SET_ZF(vm_state, false);

    vm_state->pc++;
    DISPATCH();
}
