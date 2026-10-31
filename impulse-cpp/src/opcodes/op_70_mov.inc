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
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src = inst.payload & 0xFFFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src);

    if (vm_state->register_types[src] == TYPE_BITSET_HANDLE) {
        int h_src = static_cast<int>(vm_state->registers[src]);
        int h_dst = -1;
        if (vm_state->register_types[dst] == TYPE_BITSET_HANDLE) {
            h_dst = static_cast<int>(vm_state->registers[dst]);
        } else {
            h_dst = acquire_bitset(vm_state->query_context);
            if (h_dst < 0) {
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
            vm_state->registers[dst] = h_dst;
            vm_state->register_types[dst] = TYPE_BITSET_HANDLE;
        }
        if (h_dst != h_src) {
            std::memcpy(vm_state->query_context->bitsets[h_dst].words,
                        vm_state->query_context->bitsets[h_src].words,
                        vm_state->query_context->words_per_bitset * sizeof(uint64_t));
        }
    } else {
        if (vm_state->register_types[dst] == TYPE_BITSET_HANDLE) {
            release_bitset(vm_state->query_context, static_cast<int>(vm_state->registers[dst]));
        }
        vm_state->registers[dst] = vm_state->registers[src];
        vm_state->register_types[dst] = vm_state->register_types[src];
    }
    vm_state->pc++;
    DISPATCH();
}

