/**
 * @file op_89_swap_reg.inc
 * @brief Formal Contract for OP_SWAP_REG (0x89).
 *
 * Syntax: OP_SWAP_REG <reg1>, <reg2>
 * Inputs:
 *   - dst_reg: First register
 *   - payload_low16: Second register
 * Preconditions:
 *   - Both registers can hold any valid register type
 * Outputs:
 *   - Values and type tags of reg1 and reg2 are atomically exchanged
 *   - Flags: None
 */
#ifndef OP_89_CONTRACT_DEFINED
#define OP_89_CONTRACT_DEFINED
#define OP_89_DEFINED_FLAGS        0x00u
#define OP_89_ALLOWED_SRC_TYPES    (IMP_TYPES_ANY)
#define OP_89_OUTPUT_DST_TYPES     (IMP_TYPES_ANY)
#endif

// Opcode 0x89: OP_SWAP_REG
op_SWAP_REG: {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t r1 = inst.dst_reg;
    uint16_t r2 = inst.payload & 0xFFFF;
    VALIDATE_REG(r1);
    VALIDATE_REG(r2);

    std::swap(vm_state->registers[r1], vm_state->registers[r2]);
    std::swap(vm_state->register_types[r1], vm_state->register_types[r2]);

    vm_state->pc++;
    DISPATCH();
}

