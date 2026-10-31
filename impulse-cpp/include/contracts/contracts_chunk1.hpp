/**
 * @file contracts_chunk1.hpp
 * @brief Formal Opcode Contracts for Lifecycle, Constants, CSR/CSC Walks & Vector Math (0x00..0x2F).
 */

#ifndef IMPULSE_CONTRACTS_CHUNK1_HPP
#define IMPULSE_CONTRACTS_CHUNK1_HPP

#include <vector>
#include "impulse_math_ops.h"

namespace impulse::vm {

inline void append_chunk1_contracts(std::vector<ImpulseOpcodeVariantContract>& list) {
    // ============================================================================
    // 1. VM Lifecycle & Input Initialization (0x00..0x03)
    // ============================================================================

    // OP_HALT (0x00)
    list.push_back({
        OP_HALT, "OP_HALT", "Halt VM Execution",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPES_NONE,
        false, false, 0, false, false
    });

    // OP_NOP (0x01)
    list.push_back({
        OP_NOP, "OP_NOP", "No Operation",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPES_NONE,
        false, false, 0, false, false
    });

    // OP_INIT_INPUT_NODE (0x02)
    list.push_back({
        OP_INIT_INPUT_NODE, "OP_INIT_INPUT_NODE", "Initialize Input Node ID",
        0x00, 0x00, 0x00, 0x00000001u,
        RegFieldLayout::DST_REG, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_NODE_ID),
        false, false, IMPULSE_VM_FLAG_ZF, false, true
    });

    // OP_INIT_INPUT_SET (0x03)
    list.push_back({
        OP_INIT_INPUT_SET, "OP_INIT_INPUT_SET", "Initialize Input BitSet Frontier",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_BITSET_HANDLE),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });

    // ============================================================================
    // 2. Constants & Inline Data Loading (0x04..0x0A)
    // ============================================================================

    // OP_LOAD_CONST_INT (0x04)
    list.push_back({
        OP_LOAD_CONST_INT, "OP_LOAD_CONST_INT", "Load Immediate Int64",
        0x00, 0x00, 0x00, 0x0000002Au,
        RegFieldLayout::DST_REG, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_INT64),
        false, false, 0, false, true
    });

    // OP_MAP_KEYS_TO_DENSE (0x05)
    list.push_back({
        OP_MAP_KEYS_TO_DENSE, "OP_MAP_KEYS_TO_DENSE", "Map Domain Keys to Dense Bitset",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_BITSET_HANDLE),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });

    // OP_LOAD_CONST_FLOAT (0x06)
    list.push_back({
        OP_LOAD_CONST_FLOAT, "OP_LOAD_CONST_FLOAT", "Load Immediate Float32",
        0x00, 0x00, 0x00, 0x3F800000u,
        RegFieldLayout::DST_REG, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_FLOAT),
        false, false, 0, false, true
    });

    // OP_LOAD_CONST_STR_PREFIX (0x07)
    list.push_back({
        OP_LOAD_CONST_STR_PREFIX, "OP_LOAD_CONST_STR_PREFIX", "Load 4-Byte String Prefix Immediate",
        0x00, 0x00, 0x00, 0x74657374u,
        RegFieldLayout::DST_REG, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_INT64),
        false, false, 0, false, true
    });

    // OP_LOAD_INLINE_ARRAY (0x08)
    list.push_back({
        OP_LOAD_INLINE_ARRAY, "OP_LOAD_INLINE_ARRAY", "Load Inline Array to Float Vector (flags=0)",
        0x01, 0x00, 0x01, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_FLOAT_VECTOR),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });
    list.push_back({
        OP_LOAD_INLINE_ARRAY, "OP_LOAD_INLINE_ARRAY", "Load Inline Array to Node Vector (flags=1)",
        0x01, 0x01, 0x01, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_NODE_VECTOR),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });

    // OP_INIT_MOCK_GRAPH (0x09)
    list.push_back({
        OP_INIT_MOCK_GRAPH, "OP_INIT_MOCK_GRAPH", "Initialize In-Memory Mock Graph Slot",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPES_NONE,
        false, false, 0, true, false
    });

    // OP_LOAD_INLINE_SET (0x0A)
    list.push_back({
        OP_LOAD_INLINE_SET, "OP_LOAD_INLINE_SET", "Load Inline Data into BitSet",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_BITSET_HANDLE),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });

    // ============================================================================
    // 3. Mock Attributes (0x0B..0x0C)
    // ============================================================================

    // OP_INIT_MOCK_NODE_ATTR (0x0B)
    list.push_back({
        OP_INIT_MOCK_NODE_ATTR, "OP_INIT_MOCK_NODE_ATTR", "Initialize Mock Node Attribute",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::NONE, RegFieldLayout::PAYLOAD_LOW8, RegFieldLayout::PAYLOAD_BYTE1, RegFieldLayout::NONE,
        IMP_TYPES_NUMERIC_VEC,
        IMP_TYPE_BIT(TYPE_BITSET_HANDLE),
        IMP_TYPES_NONE,
        IMP_TYPES_NONE,
        true, false, 0, true, false
    });

    // OP_INIT_MOCK_EDGE_ATTR (0x0C)
    list.push_back({
        OP_INIT_MOCK_EDGE_ATTR, "OP_INIT_MOCK_EDGE_ATTR", "Initialize Mock Edge Attribute",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::NONE, RegFieldLayout::PAYLOAD_LOW8, RegFieldLayout::PAYLOAD_BYTE1, RegFieldLayout::NONE,
        IMP_TYPES_NUMERIC_VEC,
        IMP_TYPE_BIT(TYPE_BITSET_HANDLE),
        IMP_TYPES_NONE,
        IMP_TYPES_NONE,
        true, false, 0, true, false
    });

    // ============================================================================
    // 4. CSR / CSC Graph Traversals & Topology (0x0E..0x1F)
    // ============================================================================

    // OP_CSR_WALK_2HOP (0x0E)
    list.push_back({
        OP_CSR_WALK_2HOP, "OP_CSR_WALK_2HOP", "2-Hop CSR Graph Walk (flags=0)",
        0x80, 0x00, 0x83, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_BITSET_HANDLE),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });

    // OP_CSR_WALK_STATE (0x0F)
    list.push_back({
        OP_CSR_WALK_STATE, "OP_CSR_WALK_STATE", "Initialize CSR Walk Frontier State",
        0x80, 0x00, 0x80, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_FRONTIER_STATE),
        false, false, 0, false, true
    });

    // OP_CSR_WALK (0x10)
    list.push_back({
        OP_CSR_WALK, "OP_CSR_WALK", "CSR Walk (flags=0)",
        0x07, 0x00, 0x87, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW16, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPE_BIT(TYPE_NULL) | IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_BITSET_HANDLE) | IMP_TYPES_NODE_VEC,
        IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_BITSET_HANDLE),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });
    list.push_back({
        OP_CSR_WALK, "OP_CSR_WALK", "CSR Walk with Accumulate (flags=4)",
        0x07, 0x04, 0x87, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW16, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPE_BIT(TYPE_NULL) | IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_BITSET_HANDLE) | IMP_TYPES_NODE_VEC,
        IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_BITSET_HANDLE),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });

    // OP_CSR_WALK_FILTERED (0x11)
    list.push_back({
        OP_CSR_WALK_FILTERED, "OP_CSR_WALK_FILTERED", "Filtered CSR Walk",
        0x80, 0x00, 0x87, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW8, RegFieldLayout::PAYLOAD_BYTE1, RegFieldLayout::NONE,
        IMP_TYPES_SOURCE_SET,
        IMP_TYPE_BIT(TYPE_BITSET_HANDLE),
        IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_BITSET_HANDLE),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });

    // OP_CSR_DEGREE (0x12)
    list.push_back({
        OP_CSR_DEGREE, "OP_CSR_DEGREE", "CSR Node / Frontier Degree",
        0x80, 0x00, 0x80, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW16, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_BITSET_HANDLE) | IMP_TYPE_BIT(TYPE_NULL),
        IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_INT64),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });

    // OP_CSR_WALK_PREDICATE (0x13)
    list.push_back({
        OP_CSR_WALK_PREDICATE, "OP_CSR_WALK_PREDICATE", "CSR Walk with Node Predicate",
        0x80, 0x00, 0x80, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW8, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_SOURCE_SET,
        IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_BITSET_HANDLE),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });

    // OP_NODE_FILTER (0x14)
    list.push_back({
        OP_NODE_FILTER, "OP_NODE_FILTER", "Filter Nodes by Attribute Value",
        0x00, 0x00, 0x08, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW8, RegFieldLayout::PAYLOAD_BYTE1, RegFieldLayout::NONE,
        IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_BITSET_HANDLE),
        IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_FLOAT),
        IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_BITSET_HANDLE),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });

    // OP_NODE_FILTER_STR_PREFIX (0x15)
    list.push_back({
        OP_NODE_FILTER_STR_PREFIX, "OP_NODE_FILTER_STR_PREFIX", "Filter Nodes by String Prefix",
        0x00, 0x00, 0x08, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW8, RegFieldLayout::PAYLOAD_BYTE1, RegFieldLayout::NONE,
        IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_BITSET_HANDLE),
        IMP_TYPES_INT_OR_NODE,
        IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_BITSET_HANDLE),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });

    // OP_CSR_WALK_REDUCE_SUM (0x16)
    list.push_back({
        OP_CSR_WALK_REDUCE_SUM, "OP_CSR_WALK_REDUCE_SUM", "CSR Walk Reduce Sum",
        0x80, 0x00, 0x80, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW8, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_FLOAT_OR_DBL | IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_BITSET_HANDLE),
        IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_INT64) | IMP_TYPE_BIT(TYPE_FLOAT_VECTOR) | IMP_TYPE_BIT(TYPE_DOUBLE_VECTOR),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });

    // OP_CSR_WALK_REDUCE (0x17)
    list.push_back({
        OP_CSR_WALK_REDUCE, "OP_CSR_WALK_REDUCE", "CSR Walk Reduce Binary Op",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW8, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_FLOAT_OR_DBL | IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_BITSET_HANDLE),
        IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPES_FLOAT_OR_DBL,
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });

    // OP_CSC_WALK (0x18)
    list.push_back({
        OP_CSC_WALK, "OP_CSC_WALK", "CSC Walk (flags=0)",
        0x07, 0x00, 0x87, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW16, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPE_BIT(TYPE_NULL) | IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_BITSET_HANDLE) | IMP_TYPES_NODE_VEC,
        IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_BITSET_HANDLE),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });

    // OP_HAS_CSR (0x19)
    list.push_back({
        OP_HAS_CSR, "OP_HAS_CSR", "Test CSR Relation Availability",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_INT64),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });

    // OP_HAS_CSC (0x1A)
    list.push_back({
        OP_HAS_CSC, "OP_HAS_CSC", "Test CSC Relation Availability",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_INT64),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });

    // OP_HAS_COO (0x1B)
    list.push_back({
        OP_HAS_COO, "OP_HAS_COO", "Test COO Relation Availability",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_INT64),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });

    // OP_HAS_KEY_CATALOG (0x1C)
    list.push_back({
        OP_HAS_KEY_CATALOG, "OP_HAS_KEY_CATALOG", "Test Domain Key Catalog Availability",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_INT64),
        false, false, IMPULSE_VM_FLAG_ZF, false, true
    });

    // OP_ADAPTIVE_WALK (0x1D)
    list.push_back({
        OP_ADAPTIVE_WALK, "OP_ADAPTIVE_WALK", "Direction-Optimizing Adaptive Walk",
        0x80, 0x00, 0x80, 0x00000000u,
        RegFieldLayout::NONE, RegFieldLayout::PAYLOAD_LOW16, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPE_BIT(TYPE_NULL) | IMP_TYPE_BIT(TYPE_BITSET_HANDLE) | IMP_TYPES_INT_OR_NODE | IMP_TYPES_NODE_VEC,
        IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPES_NONE,
        false, false, 0, true, false
    });

    // OP_CREATE_SCRATCH_INDEX (0x1E)
    list.push_back({
        OP_CREATE_SCRATCH_INDEX, "OP_CREATE_SCRATCH_INDEX", "Create Transient Scratch Index",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPES_NONE,
        false, false, 0, false, false
    });

    // OP_DROP_SCRATCH_INDEX (0x1F)
    list.push_back({
        OP_DROP_SCRATCH_INDEX, "OP_DROP_SCRATCH_INDEX", "Drop Transient Scratch Index",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPES_NONE,
        false, false, 0, false, false
    });

    // ============================================================================
    // 5. Vector Comparisons, Bitwise Masks & Vector Math (0x20..0x2F)
    // ============================================================================

    // OP_VEC_CMP_EQ (0x20)
    list.push_back({
        OP_VEC_CMP_EQ, "OP_VEC_CMP_EQ", "Vector Elementwise Compare Equal",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW8, RegFieldLayout::PAYLOAD_BYTE1, RegFieldLayout::NONE,
        IMP_TYPES_NUMERIC_VEC | IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_FLOAT),
        IMP_TYPES_NUMERIC_VEC | IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_FLOAT),
        IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_BITSET_HANDLE),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });

    // OP_VEC_CMP_GT (0x21)
    list.push_back({
        OP_VEC_CMP_GT, "OP_VEC_CMP_GT", "Vector Elementwise Compare Greater-Than",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW8, RegFieldLayout::PAYLOAD_BYTE1, RegFieldLayout::NONE,
        IMP_TYPES_NUMERIC_VEC | IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_FLOAT),
        IMP_TYPES_NUMERIC_VEC | IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_FLOAT),
        IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_BITSET_HANDLE),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });

    // OP_VEC_CMP_LT (0x22)
    list.push_back({
        OP_VEC_CMP_LT, "OP_VEC_CMP_LT", "Vector Elementwise Compare Less-Than",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW8, RegFieldLayout::PAYLOAD_BYTE1, RegFieldLayout::NONE,
        IMP_TYPES_NUMERIC_VEC | IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_FLOAT),
        IMP_TYPES_NUMERIC_VEC | IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_FLOAT),
        IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_BITSET_HANDLE),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });

    // OP_VEC_CMP_BETWEEN (0x23)
    list.push_back({
        OP_VEC_CMP_BETWEEN, "OP_VEC_CMP_BETWEEN", "Vector Elementwise Compare Between Range",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW8, RegFieldLayout::PAYLOAD_BYTE1, RegFieldLayout::PAYLOAD_BYTE2,
        IMP_TYPES_NUMERIC_VEC | IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_FLOAT) | IMP_TYPE_BIT(TYPE_NULL),
        IMP_TYPES_NUMERIC_VEC | IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_FLOAT) | IMP_TYPE_BIT(TYPE_NULL),
        IMP_TYPES_NUMERIC_VEC | IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_FLOAT) | IMP_TYPE_BIT(TYPE_NULL),
        IMP_TYPE_BIT(TYPE_BITSET_HANDLE),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });

    // OP_MASK_AND (0x24)
    list.push_back({
        OP_MASK_AND, "OP_MASK_AND", "BitSet Bitwise Intersection (AND)",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW8, RegFieldLayout::PAYLOAD_BYTE1, RegFieldLayout::NONE,
        IMP_TYPE_BIT(TYPE_BITSET_HANDLE) | IMP_TYPE_BIT(TYPE_NULL) | IMP_TYPES_INT_OR_NODE,
        IMP_TYPE_BIT(TYPE_BITSET_HANDLE) | IMP_TYPE_BIT(TYPE_NULL) | IMP_TYPES_INT_OR_NODE,
        IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_BITSET_HANDLE),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });

    // OP_MASK_OR (0x25)
    list.push_back({
        OP_MASK_OR, "OP_MASK_OR", "BitSet Bitwise Union (OR)",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW8, RegFieldLayout::PAYLOAD_BYTE1, RegFieldLayout::NONE,
        IMP_TYPE_BIT(TYPE_BITSET_HANDLE) | IMP_TYPE_BIT(TYPE_NULL) | IMP_TYPES_INT_OR_NODE,
        IMP_TYPE_BIT(TYPE_BITSET_HANDLE) | IMP_TYPE_BIT(TYPE_NULL) | IMP_TYPES_INT_OR_NODE,
        IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_BITSET_HANDLE),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });

    // OP_MASK_NOT (0x26)
    list.push_back({
        OP_MASK_NOT, "OP_MASK_NOT", "BitSet Bitwise Complement (NOT)",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW8, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPE_BIT(TYPE_BITSET_HANDLE) | IMP_TYPE_BIT(TYPE_NULL) | IMP_TYPES_INT_OR_NODE,
        IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_BITSET_HANDLE),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });

    // OP_VEC_BLEND (0x27)
    list.push_back({
        OP_VEC_BLEND, "OP_VEC_BLEND", "Vector Mask Conditional Blend",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW8, RegFieldLayout::PAYLOAD_BYTE1, RegFieldLayout::PAYLOAD_BYTE2,
        IMP_TYPE_BIT(TYPE_BITSET_HANDLE),
        IMP_TYPES_NUMERIC_VEC,
        IMP_TYPES_NUMERIC_VEC,
        IMP_TYPES_NUMERIC_VEC,
        false, false, 0, true, true
    });

    // OP_ASSERT_FINITE (0x2A)
    list.push_back({
        OP_ASSERT_FINITE, "OP_ASSERT_FINITE", "Assert Numeric Finite (No NaN or Inf)",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPES_NONE,
        false, false, 0, false, false
    });

    // OP_VEC_MATH_UNARY (0x2D)
    list.push_back({
        OP_VEC_MATH_UNARY, "OP_VEC_MATH_UNARY", "Vector Math Unary Function (ABS)",
        0x00, 0x00, 0x00, (static_cast<uint32_t>(MATH_FUNC_ABS) << 8),
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW8, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_FLOAT_OR_DBL | IMP_TYPE_BIT(TYPE_FLOAT),
        IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPES_FLOAT_OR_DBL | IMP_TYPE_BIT(TYPE_FLOAT),
        false, false, 0, true, true
    });

    // OP_VEC_MATH_BINARY (0x2E)
    list.push_back({
        OP_VEC_MATH_BINARY, "OP_VEC_MATH_BINARY", "Vector Math Binary Function (POW)",
        0x00, 0x00, 0x00, (static_cast<uint32_t>(MATH_FUNC_POW) << 16),
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW8, RegFieldLayout::PAYLOAD_BYTE1, RegFieldLayout::NONE,
        IMP_TYPES_FLOAT_OR_DBL | IMP_TYPE_BIT(TYPE_FLOAT),
        IMP_TYPES_FLOAT_OR_DBL | IMP_TYPE_BIT(TYPE_FLOAT),
        IMP_TYPES_NONE,
        IMP_TYPES_FLOAT_OR_DBL | IMP_TYPE_BIT(TYPE_FLOAT),
        false, false, 0, true, true
    });

    // OP_VEC_MATH_TERNARY (0x2F)
    list.push_back({
        OP_VEC_MATH_TERNARY, "OP_VEC_MATH_TERNARY", "Vector Math Ternary Function (LERP)",
        0x00, 0x00, 0x00, (static_cast<uint32_t>(MATH_FUNC_LERP) << 24),
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW8, RegFieldLayout::PAYLOAD_BYTE1, RegFieldLayout::PAYLOAD_BYTE2,
        IMP_TYPES_FLOAT_OR_DBL,
        IMP_TYPES_FLOAT_OR_DBL,
        IMP_TYPES_FLOAT_OR_DBL,
        IMP_TYPES_FLOAT_OR_DBL,
        false, false, 0, true, true
    });
}

} // namespace impulse::vm

#endif // IMPULSE_CONTRACTS_CHUNK1_HPP
