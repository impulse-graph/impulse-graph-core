/**
 * @file op_70_mov.inc
 * @brief Formal Contract for OP_MOV (0x70).
 *
 * Syntax: OP_MOV <dst_reg>, <src_reg>
 * Inputs:
 *   - dst_reg: Destination register
 *   - src_reg (payload_low16): Source register to copy from
 * Preconditions:
 *   - src_reg can be any allocated register type (deep-clones bitsets)
 * Outputs:
 *   - dst_reg receives exact value and type tag of src_reg
 *   - Flags: None
 */
#ifndef OP_70_CONTRACT_DEFINED
#define OP_70_CONTRACT_DEFINED
#define OP_70_DEFINED_FLAGS        0x00u
#define OP_70_ALLOWED_SRC_TYPES    (IMP_TYPES_ANY)
#define OP_70_OUTPUT_DST_TYPES     (IMP_TYPES_ANY)
#endif

// Opcode 0x70: OP_MOV
op_MOV: {
    IMPULSE_ASSERT(vm_state != nullptr);
    const auto& inst = bytecode[vm_state->pc];
    const uint16_t dst = IMP_INST_DST(inst);
    const uint16_t src = IMP_INST_LOW16(inst);
    IMP_VALIDATE_GPR(dst);
    IMP_VALIDATE_GPR(src);
    IMPULSE_ASSERT(dst < IMP_VM_GPR_COUNT && src < IMP_VM_GPR_COUNT);

    const bool src_is_bitset = (vm_state->register_types[src] == TYPE_BITSET_HANDLE);
    IMPULSE_TESTCASE(dst == src);
    IMPULSE_TESTCASE(src_is_bitset);

    if (src_is_bitset) {
        IMP_REQUIRE_CONTEXT(vm_state);
        IMPULSE_ASSERT(vm_state->query_context != nullptr);
        const int h_src = static_cast<int>(vm_state->registers[src]);
        if (h_src < 0 || static_cast<size_t>(h_src) >= vm_state->query_context->bitsets.size()) {
            return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        }
        int h_dst = -1;
        if (vm_state->register_types[dst] == TYPE_BITSET_HANDLE) {
            h_dst = static_cast<int>(vm_state->registers[dst]);
        }
        if (h_dst < 0 || static_cast<size_t>(h_dst) >= vm_state->query_context->bitsets.size()) {
            h_dst = acquire_bitset(vm_state->query_context);
            if (h_dst < 0) {
                return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
            }
            vm_state->registers[dst] = static_cast<uint64_t>(h_dst);
            vm_state->register_types[dst] = TYPE_BITSET_HANDLE;
        }
        IMPULSE_ASSERT(h_dst >= 0 && static_cast<size_t>(h_dst) < vm_state->query_context->bitsets.size());
        if (h_dst != h_src) {
            std::memcpy(vm_state->query_context->bitsets[h_dst].words,
                        vm_state->query_context->bitsets[h_src].words,
                        vm_state->query_context->words_per_bitset * sizeof(uint64_t));
        }
    } else {
        vm_state->registers[dst] = vm_state->registers[src];
        vm_state->register_types[dst] = vm_state->register_types[src];
    }
    vm_state->pc++;
    DISPATCH();
}
