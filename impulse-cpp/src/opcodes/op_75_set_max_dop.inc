/**
 * @file op_75_set_max_dop.inc
 * @brief Formal Contract for OP_SET_MAX_DOP (0x75).
 *
 * Syntax: OP_SET_MAX_DOP <dst_reg> (payload: [requested_dop:32])
 * Inputs:
 *   - dst_reg: Target register receiving active thread degree of parallelism
 *   - payload: Requested worker concurrency limit
 * Preconditions:
 *   - dst_reg must be valid register index (0..63)
 * Outputs:
 *   - dst_reg set to effective thread count as TYPE_INT64
 *   - Flags: ZF cleared
 */
#ifndef OP_75_CONTRACT_DEFINED
#define OP_75_CONTRACT_DEFINED
#define OP_75_DEFINED_FLAGS        0x00u
#define OP_75_ALLOWED_SRC_TYPES    (IMP_TYPES_NONE)
#define OP_75_OUTPUT_DST_TYPES     (IMP_TYPE_BIT(TYPE_INT64))
#define OP_75_DEFAULT_HOST_CEILING 8
#define OP_75_MIN_DOP              1
#endif

// Opcode 0x75: OP_SET_MAX_DOP
op_SET_MAX_DOP: {
    IMPULSE_ASSERT(vm_state != nullptr);
    const impulse_instruction_t& inst = bytecode[vm_state->pc];
    const uint16_t dst = IMP_INST_DST(inst);
    const uint32_t req_dop = IMP_INST_PAYLOAD_U32(inst);
    IMP_VALIDATE_GPR(dst);
    IMPULSE_ASSERT(dst < IMP_VM_GPR_COUNT);

    int host_ceiling = OP_75_DEFAULT_HOST_CEILING;
#if defined(_OPENMP)
    host_ceiling = omp_get_max_threads();
#endif
    const char* env_dop = std::getenv("IMPULSE_MAX_DOP");
    if (!env_dop) env_dop = std::getenv("IMPULSE_MAX_THREADS");
    if (env_dop) {
        const int parsed = std::atoi(env_dop);
        if (parsed > 0) host_ceiling = parsed;
    }

    IMPULSE_TESTCASE(req_dop == 0u);
    IMPULSE_TESTCASE(req_dop == static_cast<uint32_t>(OP_75_MIN_DOP));
    IMPULSE_TESTCASE(req_dop > static_cast<uint32_t>(host_ceiling));

    const int effective_dop = std::max(
        OP_75_MIN_DOP,
        std::min(req_dop > 0u ? static_cast<int>(req_dop) : host_ceiling, host_ceiling));
    IMPULSE_ASSERT(effective_dop >= OP_75_MIN_DOP);

    if (vm_state->query_context) {
        vm_state->query_context->max_threads = effective_dop;
    }
    vm_state->registers[dst] = static_cast<uint64_t>(effective_dop);
    vm_state->register_types[dst] = TYPE_INT64;
    IMP_VM_SET_ZF(vm_state, false);

    vm_state->pc++;
    DISPATCH();
}
