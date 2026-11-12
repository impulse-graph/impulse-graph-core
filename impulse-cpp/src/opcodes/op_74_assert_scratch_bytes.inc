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
#define OP_74_DEFINED_FLAGS             0x00u
#define OP_74_ALLOWED_SRC_TYPES         (IMP_TYPES_NONE)
#define OP_74_OUTPUT_DST_TYPES          (IMP_TYPE_BIT(TYPE_INT64))
#define OP_74_DEFAULT_SCRATCH_CAP_BYTES (512ULL * 1024ULL * 1024ULL)
#endif

// Opcode 0x74: OP_ASSERT_SCRATCH_BYTES
op_ASSERT_SCRATCH_BYTES: {
    IMPULSE_ASSERT(vm_state != nullptr);
    const impulse_instruction_t& inst = bytecode[vm_state->pc];
    const uint16_t dst = IMP_INST_DST(inst);
    const uint32_t req_bytes = IMP_INST_PAYLOAD_U32(inst);
    IMP_VALIDATE_GPR(dst);
    IMPULSE_ASSERT(dst < IMP_VM_GPR_COUNT);

    const uint64_t available_bytes = OP_74_DEFAULT_SCRATCH_CAP_BYTES;
    IMPULSE_TESTCASE(req_bytes == 0u);
    IMPULSE_TESTCASE(static_cast<uint64_t>(req_bytes) == available_bytes);
    IMPULSE_TESTCASE(static_cast<uint64_t>(req_bytes) > available_bytes);
    if (static_cast<uint64_t>(req_bytes) > available_bytes) {
        return IMPULSE_VM_ERR_ASSERTION_FAILED;
    }
    IMPULSE_ASSERT(static_cast<uint64_t>(req_bytes) <= available_bytes);

    vm_state->registers[dst] = available_bytes;
    vm_state->register_types[dst] = TYPE_INT64;
    IMP_VM_SET_ZF(vm_state, false);

    vm_state->pc++;
    DISPATCH();
}
