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
#endif

// Opcode 0x75: OP_SET_MAX_DOP
op_SET_MAX_DOP: {
    const impulse_instruction_t& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint32_t req_dop = inst.payload;
    VALIDATE_REG(dst);

    int host_ceiling = 8;
#if defined(_OPENMP)
    host_ceiling = omp_get_max_threads();
#endif
    const char* env_dop = std::getenv("IMPULSE_MAX_DOP");
    if (!env_dop) env_dop = std::getenv("IMPULSE_MAX_THREADS");
    if (env_dop) {
        int parsed = std::atoi(env_dop);
        if (parsed > 0) host_ceiling = parsed;
    }

    int effective_dop = std::max(1, std::min(req_dop > 0 ? static_cast<int>(req_dop) : host_ceiling, host_ceiling));
    if (vm_state->query_context) {
        vm_state->query_context->max_threads = effective_dop;
    }
    vm_state->registers[dst] = effective_dop;
    vm_state->register_types[dst] = TYPE_INT64;
    vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

    vm_state->pc++;
    DISPATCH();
}

