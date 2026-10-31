/**
 * @file op_74_assert_scratch_bytes.inc
 * @brief Formal Contract for OP_ASSERT_SCRATCH_BYTES (0x74).
 *
 * Syntax: OP_ASSERT_SCRATCH_BYTES <dst_reg> (payload: [requested_bytes:32])
 * Inputs:
 *   - dst_reg: Target register receiving available scratch memory capacity
 *   - payload: Required bytes to assert against scratch limit (512MB ceiling)
 * Preconditions:
 *   - dst_reg must be valid register index (0..63)
 * Outputs:
 *   - dst_reg set to available capacity as TYPE_INT64
 *   - Flags: ZF cleared
 */
#ifndef OP_74_CONTRACT_DEFINED
#define OP_74_CONTRACT_DEFINED
#define OP_74_DEFINED_FLAGS        0x00u
#define OP_74_ALLOWED_SRC_TYPES    (IMP_TYPES_NONE)
#define OP_74_OUTPUT_DST_TYPES     (IMP_TYPE_BIT(TYPE_INT64))
#endif

// Opcode 0x74: OP_ASSERT_SCRATCH_BYTES
op_ASSERT_SCRATCH_BYTES: {
    const impulse_instruction_t& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint32_t req_bytes = inst.payload;
    VALIDATE_REG(dst);

    uint64_t available_bytes = 512 * 1024 * 1024ULL; // 512 MB default scratch cap
    if (static_cast<uint64_t>(req_bytes) > available_bytes) {
        return IMPULSE_VM_ERR_ASSERTION_FAILED;
    }
    vm_state->registers[dst] = available_bytes;
    vm_state->register_types[dst] = TYPE_INT64;
    vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

    vm_state->pc++;
    DISPATCH();
}

