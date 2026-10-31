// Opcode 0x03: OP_INIT_INPUT_SET
op_INIT_INPUT_SET: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    VALIDATE_REG(dst);

    int handle = acquire_bitset(vm_state->query_context);
    if (handle < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;

    vm_state->registers[dst] = static_cast<uint64_t>(handle);
    vm_state->register_types[dst] = TYPE_BITSET_HANDLE;

    auto& bs = vm_state->query_context->bitsets[handle];
    bs.clear();

    bool is_empty = true;
    if (input_param) {
        const uint64_t* src_words = reinterpret_cast<const uint64_t*>(input_param);
        std::memcpy(bs.words, src_words, vm_state->query_context->words_per_bitset * sizeof(uint64_t));
        for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
            if (bs.words[i] != 0) {
                is_empty = false;
                break;
            }
        }
    }

    if (is_empty) {
        vm_state->flags |= IMPULSE_VM_FLAG_ZF;
    } else {
        vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;
    }

    vm_state->pc++;
    DISPATCH();
}

