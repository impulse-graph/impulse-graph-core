/**
 * @file contracts_chunk4.hpp
 * @brief Formal Opcode Contracts for Terminal Collectors & Edge Stream Shaders (0x90..0xBF).
 */

#ifndef IMPULSE_CONTRACTS_CHUNK4_HPP
#define IMPULSE_CONTRACTS_CHUNK4_HPP

#include <vector>

namespace impulse::vm {

inline void append_chunk4_contracts(std::vector<ImpulseOpcodeVariantContract>& list) {
    // --- Terminal Collectors & Multiplicity Sweep (0x90..0x95) ---

    // OP_COLLECT_BITSET (0x90)
    list.push_back({
        OP_COLLECT_BITSET, "OP_COLLECT_BITSET", "Collect Active Bitset",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW16, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPE_BIT(TYPE_NULL) | IMP_TYPE_BIT(TYPE_BITSET_HANDLE) | IMP_TYPE_BIT(TYPE_LANE_FRONTIER) | IMP_TYPES_NODE_VEC | IMP_TYPES_INT_OR_NODE,
        IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_NULL) | IMP_TYPE_BIT(TYPE_BITSET_HANDLE) | IMP_TYPE_BIT(TYPE_LANE_FRONTIER) | IMP_TYPES_NODE_VEC | IMP_TYPES_INT_OR_NODE,
        false, false, 0, false, true
    });

    // OP_COLLECT_ARRAY (0x91)
    list.push_back({
        OP_COLLECT_ARRAY, "OP_COLLECT_ARRAY", "Collect Frontier into Array",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW16, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPE_BIT(TYPE_NULL) | IMP_TYPE_BIT(TYPE_BITSET_HANDLE) | IMP_TYPE_BIT(TYPE_LANE_FRONTIER) | IMP_TYPES_NODE_VEC | IMP_TYPES_INT_OR_NODE,
        IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_NODE_VECTOR),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });

    // OP_MAP_DENSE_TO_KEYS (0x92)
    list.push_back({
        OP_MAP_DENSE_TO_KEYS, "OP_MAP_DENSE_TO_KEYS", "Map Dense IDs to String Keys",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW8, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_SOURCE_SET | IMP_TYPE_BIT(TYPE_LANE_FRONTIER),
        IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_STRING_VECTOR),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });

    // OP_COLLECT_VALUE_MAP (0x93)
    list.push_back({
        OP_COLLECT_VALUE_MAP, "OP_COLLECT_VALUE_MAP", "Collect Value Map from Nodes and Values",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW8, RegFieldLayout::PAYLOAD_BYTE1, RegFieldLayout::NONE,
        IMP_TYPES_SOURCE_SET | IMP_TYPE_BIT(TYPE_LANE_FRONTIER),
        IMP_TYPES_FLOAT_OR_DBL, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_VALUE_MAP),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });

    // OP_DENSE_WALK_REDUCE (0x94)
    list.push_back({
        OP_DENSE_WALK_REDUCE, "OP_DENSE_WALK_REDUCE", "Dense Walk Reduce Initialize Float Vector",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_FLOAT_VECTOR),
        false, false, 0, true, true
    });

    // OP_DENSE_WALK_DIRECT_STORE (0x95)
    list.push_back({
        OP_DENSE_WALK_DIRECT_STORE, "OP_DENSE_WALK_DIRECT_STORE", "Dense Walk Direct Store BitSet",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW8, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_SOURCE_SET,
        IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_BITSET_HANDLE),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });

    // --- Edge Stream Shader Walks (0xA0, 0xA9, 0xAA) ---

    // OP_COO_WALK_STREAM (0xA0)
    list.push_back({
        OP_COO_WALK_STREAM, "OP_COO_WALK_STREAM", "COO Walk Edge Stream",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW16, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_SOURCE_SET,
        IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_BITSET_HANDLE),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });

    // OP_CSR_WALK_STREAM (0xA9)
    list.push_back({
        OP_CSR_WALK_STREAM, "OP_CSR_WALK_STREAM", "CSR Walk Edge Stream",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW16, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_SOURCE_SET,
        IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_BITSET_HANDLE),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });

    // OP_CSC_WALK_STREAM (0xAA)
    list.push_back({
        OP_CSC_WALK_STREAM, "OP_CSC_WALK_STREAM", "CSC Walk Edge Stream",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW16, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_SOURCE_SET,
        IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_BITSET_HANDLE),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });

    // --- Edge Stream Shader Sub-Opcodes (0xA1..0xA8, 0xAB..0xBF) ---
    // Evaluated per-edge inside stream walk fused loop; act as OP_NOP in top-level VM dispatch.

    // OP_STREAM_FUNC_BEGIN (0xA1)
    list.push_back({
        OP_STREAM_FUNC_BEGIN, "OP_STREAM_FUNC_BEGIN", "Stream Shader Function Begin",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPES_NONE,
        false, false, 0, false, false
    });

    // OP_STREAM_FUNC_END (0xA2)
    list.push_back({
        OP_STREAM_FUNC_END, "OP_STREAM_FUNC_END", "Stream Shader Function End",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPES_NONE,
        false, false, 0, false, false
    });

    // OP_STREAM_LOAD_SRC (0xA3)
    list.push_back({
        OP_STREAM_LOAD_SRC, "OP_STREAM_LOAD_SRC", "Stream Load Source Attribute",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPES_NONE,
        false, false, 0, false, false
    });

    // OP_STREAM_LOAD_EDGE (0xA4)
    list.push_back({
        OP_STREAM_LOAD_EDGE, "OP_STREAM_LOAD_EDGE", "Stream Load Edge Attribute",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPES_NONE,
        false, false, 0, false, false
    });

    // OP_STREAM_MATH_ADD (0xA5)
    list.push_back({
        OP_STREAM_MATH_ADD, "OP_STREAM_MATH_ADD", "Stream Math Addition",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPES_NONE,
        false, false, 0, false, false
    });

    // OP_STREAM_MATH_DIV (0xA6)
    list.push_back({
        OP_STREAM_MATH_DIV, "OP_STREAM_MATH_DIV", "Stream Math Division",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPES_NONE,
        false, false, 0, false, false
    });

    // OP_STREAM_FILTER (0xA7)
    list.push_back({
        OP_STREAM_FILTER, "OP_STREAM_FILTER", "Stream Predicate Filter",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPES_NONE,
        false, false, 0, false, false
    });

    // OP_STREAM_REDUCE (0xA8)
    list.push_back({
        OP_STREAM_REDUCE, "OP_STREAM_REDUCE", "Stream Monoidic Reduction",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPES_NONE,
        false, false, 0, false, false
    });

    // OP_STREAM_LOAD_TGT (0xAB)
    list.push_back({
        OP_STREAM_LOAD_TGT, "OP_STREAM_LOAD_TGT", "Stream Load Target Attribute",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPES_NONE,
        false, false, 0, false, false
    });

    // OP_STREAM_MATH_SUB (0xAC)
    list.push_back({
        OP_STREAM_MATH_SUB, "OP_STREAM_MATH_SUB", "Stream Math Subtraction",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPES_NONE,
        false, false, 0, false, false
    });

    // OP_STREAM_MATH_MUL (0xAD)
    list.push_back({
        OP_STREAM_MATH_MUL, "OP_STREAM_MATH_MUL", "Stream Math Multiplication",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPES_NONE,
        false, false, 0, false, false
    });

    // OP_STREAM_MATH_MOD (0xAE)
    list.push_back({
        OP_STREAM_MATH_MOD, "OP_STREAM_MATH_MOD", "Stream Math Modulo",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPES_NONE,
        false, false, 0, false, false
    });

    // OP_STREAM_MATH_UNARY (0xAF)
    list.push_back({
        OP_STREAM_MATH_UNARY, "OP_STREAM_MATH_UNARY", "Stream Math Unary Activation",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPES_NONE,
        false, false, 0, false, false
    });

    // OP_STREAM_CMP_EQ (0xB0)
    list.push_back({
        OP_STREAM_CMP_EQ, "OP_STREAM_CMP_EQ", "Stream Comparison Equal",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPES_NONE,
        false, false, 0, false, false
    });

    // OP_STREAM_CMP_NEQ (0xB1)
    list.push_back({
        OP_STREAM_CMP_NEQ, "OP_STREAM_CMP_NEQ", "Stream Comparison Not Equal",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPES_NONE,
        false, false, 0, false, false
    });

    // OP_STREAM_CMP_GT (0xB2)
    list.push_back({
        OP_STREAM_CMP_GT, "OP_STREAM_CMP_GT", "Stream Comparison Greater Than",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPES_NONE,
        false, false, 0, false, false
    });

    // OP_STREAM_CMP_LT (0xB3)
    list.push_back({
        OP_STREAM_CMP_LT, "OP_STREAM_CMP_LT", "Stream Comparison Less Than",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPES_NONE,
        false, false, 0, false, false
    });

    // OP_STREAM_LOGIC_AND (0xB4)
    list.push_back({
        OP_STREAM_LOGIC_AND, "OP_STREAM_LOGIC_AND", "Stream Logic AND",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPES_NONE,
        false, false, 0, false, false
    });

    // OP_STREAM_LOGIC_OR (0xB5)
    list.push_back({
        OP_STREAM_LOGIC_OR, "OP_STREAM_LOGIC_OR", "Stream Logic OR",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPES_NONE,
        false, false, 0, false, false
    });

    // OP_STREAM_LOGIC_NOT (0xB6)
    list.push_back({
        OP_STREAM_LOGIC_NOT, "OP_STREAM_LOGIC_NOT", "Stream Logic NOT",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPES_NONE,
        false, false, 0, false, false
    });

    // OP_STREAM_SELECT (0xB7)
    list.push_back({
        OP_STREAM_SELECT, "OP_STREAM_SELECT", "Stream Ternary Select",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPES_NONE,
        false, false, 0, false, false
    });

    // OP_STREAM_REDUCE_ARGMIN (0xB8)
    list.push_back({
        OP_STREAM_REDUCE_ARGMIN, "OP_STREAM_REDUCE_ARGMIN", "Stream Reduce ArgMin",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPES_NONE,
        false, false, 0, false, false
    });

    // OP_STREAM_REDUCE_ARGMAX (0xB9)
    list.push_back({
        OP_STREAM_REDUCE_ARGMAX, "OP_STREAM_REDUCE_ARGMAX", "Stream Reduce ArgMax",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPES_NONE,
        false, false, 0, false, false
    });

    // OP_STREAM_LOAD_SRC_ID (0xBA)
    list.push_back({
        OP_STREAM_LOAD_SRC_ID, "OP_STREAM_LOAD_SRC_ID", "Stream Load Source Node ID",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPES_NONE,
        false, false, 0, false, false
    });

    // OP_STREAM_LOAD_TGT_ID (0xBB)
    list.push_back({
        OP_STREAM_LOAD_TGT_ID, "OP_STREAM_LOAD_TGT_ID", "Stream Load Target Node ID",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPES_NONE,
        false, false, 0, false, false
    });

    // OP_STREAM_LOAD_EDGE_ID (0xBC)
    list.push_back({
        OP_STREAM_LOAD_EDGE_ID, "OP_STREAM_LOAD_EDGE_ID", "Stream Load Edge ID",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPES_NONE,
        false, false, 0, false, false
    });

    // OP_STREAM_LOAD_CONST (0xBD)
    list.push_back({
        OP_STREAM_LOAD_CONST, "OP_STREAM_LOAD_CONST", "Stream Load Immediate Constant",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPES_NONE,
        false, false, 0, false, false
    });

    // OP_STREAM_YIELD (0xBE)
    list.push_back({
        OP_STREAM_YIELD, "OP_STREAM_YIELD", "Stream Yield Target Node",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPES_NONE,
        false, false, 0, false, false
    });

    // OP_STREAM_SCATTER_REDUCE (0xBF)
    list.push_back({
        OP_STREAM_SCATTER_REDUCE, "OP_STREAM_SCATTER_REDUCE", "Stream Scatter Reduce Attribute",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPES_NONE,
        false, false, 0, false, false
    });
}

} // namespace impulse::vm

#endif // IMPULSE_CONTRACTS_CHUNK4_HPP
