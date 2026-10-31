/**
 * @file op_71_clear_reg.inc
 * @brief Formal Contract for OP_CLEAR_REG (0x71).
 *
 * Syntax: OP_CLEAR_REG <dst_reg>
 * Inputs:
 *   - dst_reg: Register to reset to 0 and TYPE_NULL
 * Preconditions:
 *   - dst_reg must be valid register index (0..63)
 * Outputs:
 *   - dst_reg set to 0 and TYPE_NULL; associated bitset handle released if applicable
 *   - Flags: None
 */
#ifndef OP_71_CONTRACT_DEFINED
#define OP_71_CONTRACT_DEFINED
#define OP_71_DEFINED_FLAGS        0x00u
#define OP_71_ALLOWED_SRC_TYPES    (IMP_TYPES_NONE)
#define OP_71_OUTPUT_DST_TYPES     (IMP_TYPE_BIT(TYPE_NULL))
#endif

// Opcode 0x71: OP_CLEAR_REG
op_CLEAR_REG: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = inst.dst_reg;
    VALIDATE_REG(dst);
    if (vm_state->register_types[dst] == TYPE_BITSET_HANDLE) {
        release_bitset(vm_state->query_context, static_cast<int>(vm_state->registers[dst]));
    }
    vm_state->registers[dst] = 0;
    vm_state->register_types[dst] = TYPE_NULL;
    vm_state->pc++;
    DISPATCH();
}



