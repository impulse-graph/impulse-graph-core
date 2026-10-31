// Opcode 0x0E: OP_CSR_WALK_2HOP
op_CSR_WALK_2HOP: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    uint16_t src = 0;
    uint16_t rel1 = 0;
    uint16_t rel2 = 0;
    if (inst.flags & 0x80) {
        if (vm_state->pc + 1 >= instruction_count) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        src = inst.payload & 0xFF;
        rel1 = bytecode[vm_state->pc + 1].dst_reg;
        rel2 = (inst.payload >> 16) & 0xFFFF;
        if (rel2 == 0) rel2 = rel1;
        vm_state->pc++;
    } else {
        src = 0;
        rel1 = inst.payload & 0xFFFF;
        rel2 = (inst.payload >> 16) & 0xFFFF;
    }
    VALIDATE_REG(dst);
    VALIDATE_REG(src);

    if (rel1 >= vm_state->query_context->slots.size() || rel2 >= vm_state->query_context->slots.size()) {
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
    const auto& slot1 = vm_state->query_context->slots[rel1];
    const auto& slot2 = vm_state->query_context->slots[rel2];

    bool src_is_bitset = false;
    int h_src = -1;
    uint64_t scalar_src = 0;

    if (inst.flags & IMPULSE_VM_OP_FLAG_INPUT_SEED) {
        scalar_src = input_param;
        src_is_bitset = false;
    } else {
        src_is_bitset = (vm_state->register_types[src] == TYPE_BITSET_HANDLE);
        h_src = src_is_bitset ? static_cast<int>(vm_state->registers[src]) : -1;
        scalar_src = !src_is_bitset ? vm_state->registers[src] : 0;
    }

    int h_dst = -1;
    if (vm_state->register_types[dst] == TYPE_BITSET_HANDLE && !(dst == src && src_is_bitset)) {
        h_dst = static_cast<int>(vm_state->registers[dst]);
        vm_state->query_context->bitsets[h_dst].clear();
    } else {
        h_dst = acquire_bitset(vm_state->query_context);
        if (h_dst < 0) {
            return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        }
        vm_state->query_context->bitsets[h_dst].clear();
    }

    auto& bs_dst = vm_state->query_context->bitsets[h_dst];

    if (slot1.offsets_ptr && slot1.targets_ptr && slot2.offsets_ptr && slot2.targets_ptr) {
        if (src_is_bitset) {
            const auto& bs_src = vm_state->query_context->bitsets[h_src];
            for (size_t w = 0; w < vm_state->query_context->words_per_bitset; ++w) {
                uint64_t word = bs_src.words[w];
                while (word) {
                    int bit = std::countr_zero(word);
                    uint64_t u = w * 64 + bit;
                    word &= word - 1;
                    if (u < slot1.node_count) {
                        uint64_t start1 = slot1.get_csr_offset(u);
                        uint64_t end1 = slot1.get_csr_offset(u + 1);
                        for (uint64_t i = start1; i < end1; ++i) {
                            uint64_t v = slot1.get_csr_target(i);
                            if (v < slot2.node_count) {
                                uint64_t start2 = slot2.get_csr_offset(v);
                                uint64_t end2 = slot2.get_csr_offset(v + 1);
                                for (uint64_t j = start2; j < end2; ++j) {
                                    bitset_add(bs_dst, slot2.get_csr_target(j), vm_state->query_context->max_nodes);
                                }
                            }
                        }
                    }
                }
            }
        } else {
            uint64_t u = scalar_src;
            if (u < slot1.node_count) {
                uint64_t start1 = slot1.get_csr_offset(u);
                uint64_t end1 = slot1.get_csr_offset(u + 1);
                for (uint64_t i = start1; i < end1; ++i) {
                    uint64_t v = slot1.get_csr_target(i);
                    if (v < slot2.node_count) {
                        uint64_t start2 = slot2.get_csr_offset(v);
                        uint64_t end2 = slot2.get_csr_offset(v + 1);
                        for (uint64_t j = start2; j < end2; ++j) {
                            bitset_add(bs_dst, slot2.get_csr_target(j), vm_state->query_context->max_nodes);
                        }
                    }
                }
            }
        }
    }

    if (dst == src && src_is_bitset) {
        release_bitset(vm_state->query_context, h_src);
    }
    vm_state->registers[dst] = static_cast<uint64_t>(h_dst);
    vm_state->register_types[dst] = TYPE_BITSET_HANDLE;

    bool is_empty = true;
    for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
        if (bs_dst.words[i] != 0) {
            is_empty = false;
            break;
        }
    }
    if (inst.flags & IMPULSE_VM_OP_FLAG_HALT_ON_EMPTY) {
        IMPULSE_TESTCASE(is_empty);
        impulse_vm_branch_coverage_record(static_cast<uint32_t>(vm_state->pc), OP_CSR_WALK_2HOP, 2u,
                                          IMPULSE_COV_KIND_TWO_WAY, IMPULSE_COV_BOTH,
                                          is_empty ? 0u : 1u);
    }
    if (is_empty) {
        vm_state->flags |= IMPULSE_VM_FLAG_ZF;
        if (inst.flags & IMPULSE_VM_OP_FLAG_HALT_ON_EMPTY) {
            if (instruction_count > 0 && bytecode[instruction_count - 1].opcode == OP_HALT) {
                uint16_t halt_reg = bytecode[instruction_count - 1].dst_reg;
                if (halt_reg < 64 && halt_reg != dst) {
                    vm_state->registers[halt_reg] = static_cast<uint64_t>(h_dst);
                    vm_state->register_types[halt_reg] = TYPE_BITSET_HANDLE;
                }
            }
            vm_state->pc = instruction_count;
            return IMPULSE_VM_OK;
        }
    } else {
        vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;
    }

    vm_state->pc++;
    DISPATCH();
}

