/**
 * @file op_03_init_input_set.inc
 * @brief Formal Contract for OP_INIT_INPUT_SET (0x03).
 *
 * Mnemonic: OP_INIT_INPUT_SET
 * Opcode: 0x03
 * Description: Initializes destination register with a BitSet frontier populated from input pointer or empty.
 *
 * Operands:
 *  - dst = inst.dst_reg
 *
 * Inputs:
 *  - input_param (uint64_t* bitmap words pointer)
 *
 * Outputs:
 *  - dst: TYPE_BITSET_HANDLE
 *
 * Flags:
 *  - ZF: Set if frontier is empty, cleared otherwise
 *
 * Context Requirement:
 *  - Requires context (query_context off-heap bitset pool)
 */
#ifndef OP_03_CONTRACT_DEFINED
#define OP_03_CONTRACT_DEFINED 1
#define OP_03_DEFINED_FLAGS IMP_FLAG_NONE
#endif

// Opcode 0x03: OP_INIT_INPUT_SET
op_INIT_INPUT_SET: {
    const auto& inst = bytecode[vm_state->pc];
    const uint16_t dst = IMP_INST_DST(inst);
    VALIDATE_REG(dst);
    IMPULSE_ASSERT(dst < IMP_VM_GPR_COUNT);
    IMP_REQUIRE_CONTEXT(vm_state);
    IMPULSE_ASSERT(vm_state->query_context != nullptr);

    int handle = acquire_bitset(vm_state->query_context);
    if (handle < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    IMPULSE_ASSERT(handle >= 0 && static_cast<size_t>(handle) < vm_state->query_context->bitsets.size());

    vm_state->registers[dst] = static_cast<uint64_t>(handle);
    vm_state->register_types[dst] = TYPE_BITSET_HANDLE;

    auto& bs = vm_state->query_context->bitsets[handle];
    bs.clear();

    bool is_empty = true;
    IMPULSE_TESTCASE(input_param == 0u);
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

    IMP_VM_SET_ZF(vm_state, is_empty);

    vm_state->pc++;
    DISPATCH();
}

