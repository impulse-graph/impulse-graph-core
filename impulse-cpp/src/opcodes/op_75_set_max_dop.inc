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

