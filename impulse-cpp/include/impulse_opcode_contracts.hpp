/**
 * @file impulse_opcode_contracts.hpp
 * @brief Formal Executable Opcode Contracts, Type Bitmasks, and Operand Layout Specification for ImpulseVM.
 *
 * Provides:
 *  1. 17-bit register type masks (IMP_TYPES_*) mapping impulse_register_type_t to uint32_t bitmasks.
 *  2. Branchless 2-cycle register type checking macro (IMPULSE_CHECK_REG_TYPE).
 *  3. Operand position descriptors (RegFieldLayout) and dynamic instruction register injection/extraction.
 *  4. Flag-Variant-Aware Opcode Contract Struct (ImpulseOpcodeVariantContract).
 *  5. Complete constexpr contract table covering all ISA opcodes (0x00..0xCB) partitioned across chunks 1..5.
 */

#ifndef IMPULSE_OPCODE_CONTRACTS_HPP
#define IMPULSE_OPCODE_CONTRACTS_HPP

#include "impulse_vm.h"
#include <cstdint>
#include <vector>
#include <string>
#include <array>
#include <cstring>

namespace impulse::vm {

// ============================================================================
// 1. Register Type Bitmasks (0x00 .. 0x10)
// ============================================================================
#define IMP_TYPE_BIT(t) (1u << static_cast<uint32_t>(t))

constexpr uint32_t IMP_TYPES_NONE           = 0u;
constexpr uint32_t IMP_TYPES_ANY            = 0x1FFFFu;
constexpr uint32_t IMP_TYPES_INT_OR_NODE    = IMP_TYPE_BIT(TYPE_INT64) | IMP_TYPE_BIT(TYPE_NODE_ID);
constexpr uint32_t IMP_TYPES_NODE_VEC       = IMP_TYPE_BIT(TYPE_NODE_VECTOR) | IMP_TYPE_BIT(TYPE_UINT64_VECTOR);
constexpr uint32_t IMP_TYPES_FLOAT_OR_DBL   = IMP_TYPE_BIT(TYPE_FLOAT_VECTOR) | IMP_TYPE_BIT(TYPE_DOUBLE_VECTOR);
constexpr uint32_t IMP_TYPES_NUMERIC_VEC    = IMP_TYPES_FLOAT_OR_DBL | IMP_TYPES_NODE_VEC;
constexpr uint32_t IMP_TYPES_FRONTIER       = IMP_TYPE_BIT(TYPE_BITSET_HANDLE) | IMP_TYPE_BIT(TYPE_LANE_FRONTIER);
constexpr uint32_t IMP_TYPES_SOURCE_SET     = IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_BITSET_HANDLE) | IMP_TYPES_NODE_VEC;

/**
 * @brief Branchless 2-cycle validation of register type against an allowed bitmask.
 */
#define IMPULSE_CHECK_REG_TYPE(vm_state, reg, allowed_mask) \
    do { \
        if (IMPULSE_UNLIKELY(((1u << static_cast<uint32_t>((vm_state)->register_types[(reg)])) & (allowed_mask)) == 0u)) { \
            return IMPULSE_VM_ERR_INVALID_REGISTER; \
        } \
    } while (0)

// ============================================================================
// 2. Operand Register Field Layouts
// ============================================================================
enum class RegFieldLayout : uint8_t {
    NONE = 0,
    DST_REG,        // inst.dst_reg (16-bit)
    PAYLOAD_LOW8,   // inst.payload & 0xFF
    PAYLOAD_LOW16,  // inst.payload & 0xFFFF
    PAYLOAD_BYTE1,  // (inst.payload >> 8) & 0xFF
    PAYLOAD_BYTE2,  // (inst.payload >> 16) & 0xFF
    PAYLOAD_BYTE3,  // (inst.payload >> 24) & 0xFF
    PAYLOAD_HIGH16  // (inst.payload >> 16) & 0xFFFF
};

inline uint16_t extract_reg_from_layout(const impulse_instruction_t& inst, RegFieldLayout layout) {
    switch (layout) {
        case RegFieldLayout::DST_REG:        return inst.dst_reg;
        case RegFieldLayout::PAYLOAD_LOW8:   return static_cast<uint16_t>(inst.payload & 0xFF);
        case RegFieldLayout::PAYLOAD_LOW16:  return static_cast<uint16_t>(inst.payload & 0xFFFF);
        case RegFieldLayout::PAYLOAD_BYTE1:  return static_cast<uint16_t>((inst.payload >> 8) & 0xFF);
        case RegFieldLayout::PAYLOAD_BYTE2:  return static_cast<uint16_t>((inst.payload >> 16) & 0xFF);
        case RegFieldLayout::PAYLOAD_BYTE3:  return static_cast<uint16_t>((inst.payload >> 24) & 0xFF);
        case RegFieldLayout::PAYLOAD_HIGH16: return static_cast<uint16_t>((inst.payload >> 16) & 0xFFFF);
        case RegFieldLayout::NONE: default:  return 0;
    }
}

inline void inject_reg_into_layout(impulse_instruction_t& inst, RegFieldLayout layout, uint16_t reg) {
    switch (layout) {
        case RegFieldLayout::DST_REG:
            inst.dst_reg = reg;
            break;
        case RegFieldLayout::PAYLOAD_LOW8:
            inst.payload = (inst.payload & ~0xFFu) | (static_cast<uint32_t>(reg) & 0xFFu);
            break;
        case RegFieldLayout::PAYLOAD_LOW16:
            inst.payload = (inst.payload & ~0xFFFFu) | (static_cast<uint32_t>(reg) & 0xFFFFu);
            break;
        case RegFieldLayout::PAYLOAD_BYTE1:
            inst.payload = (inst.payload & ~(0xFFu << 8)) | ((static_cast<uint32_t>(reg) & 0xFFu) << 8);
            break;
        case RegFieldLayout::PAYLOAD_BYTE2:
            inst.payload = (inst.payload & ~(0xFFu << 16)) | ((static_cast<uint32_t>(reg) & 0xFFu) << 16);
            break;
        case RegFieldLayout::PAYLOAD_BYTE3:
            inst.payload = (inst.payload & ~(0xFFu << 24)) | ((static_cast<uint32_t>(reg) & 0xFFu) << 24);
            break;
        case RegFieldLayout::PAYLOAD_HIGH16:
            inst.payload = (inst.payload & ~0xFFFF0000u) | ((static_cast<uint32_t>(reg) & 0xFFFFu) << 16);
            break;
        case RegFieldLayout::NONE:
        default:
            break;
    }
}

// ============================================================================
// 3. Opcode Variant Contract Definition
// ============================================================================
struct ImpulseOpcodeVariantContract {
    uint8_t        opcode;
    const char*    mnemonic;
    const char*    variant_name;
    uint8_t        flags_mask;           // Relevant bits in inst.flags
    uint8_t        flags_match;          // Expected matching bits for this variant
    uint8_t        defined_flags_mask;   // All valid flags bits for this opcode
    uint32_t       fixed_payload_bits;   // Non-register payload bits (e.g. sub-opcodes, rel_id, etc.)
    RegFieldLayout dst_layout;
    RegFieldLayout src_layout;
    RegFieldLayout aux1_layout;
    RegFieldLayout aux2_layout;
    uint32_t       allowed_src_types;
    uint32_t       allowed_aux1_types;
    uint32_t       allowed_aux2_types;
    uint32_t       output_dst_types;
    bool           allow_zero_skip_aux1; // True if aux1_reg == 0 means "omitted/none"
    bool           allow_zero_skip_aux2; // True if aux2_reg == 0 means "omitted/none"
    uint8_t        sets_vm_flags_mask;   // e.g. IMPULSE_VM_FLAG_ZF
    bool           requires_context;
    bool           modifies_dst_state;   // True if dst register is written with a result
};

} // namespace impulse::vm

// Include the 5 modular contract chunks
#include "contracts/contracts_chunk1.hpp"
#include "contracts/contracts_chunk2.hpp"
#include "contracts/contracts_chunk3.hpp"
#include "contracts/contracts_chunk4.hpp"
#include "contracts/contracts_chunk5.hpp"

namespace impulse::vm {

// ============================================================================
// 4. Canonical Contract Registry
// ============================================================================
inline std::vector<ImpulseOpcodeVariantContract> get_all_opcode_contracts() {
    std::vector<ImpulseOpcodeVariantContract> list;
    append_chunk1_contracts(list);
    append_chunk2_contracts(list);
    append_chunk3_contracts(list);
    append_chunk4_contracts(list);
    append_chunk5_contracts(list);
    return list;
}

} // namespace impulse::vm

#endif // IMPULSE_OPCODE_CONTRACTS_HPP
