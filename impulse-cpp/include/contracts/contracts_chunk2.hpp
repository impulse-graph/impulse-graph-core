/**
 * @file contracts_chunk2.hpp
 * @brief Formal Opcode Contracts for Sets, GraphBLAS & Control Flow (0x30..0x5C).
 */

#ifndef IMPULSE_CONTRACTS_CHUNK2_HPP
#define IMPULSE_CONTRACTS_CHUNK2_HPP

#include <vector>

namespace impulse::vm {

inline void append_chunk2_contracts(std::vector<ImpulseOpcodeVariantContract>& list) {
    // --- Set Operations (0x30..0x33) ---

    // OP_SET_UNION (0x30)
    list.push_back({
        OP_SET_UNION, "OP_SET_UNION", "Set Union (BitSet / Node IDs / Node Vectors / Lane Frontier)",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW16, RegFieldLayout::PAYLOAD_HIGH16, RegFieldLayout::NONE,
        IMP_TYPES_SOURCE_SET | IMP_TYPE_BIT(TYPE_LANE_FRONTIER) | IMP_TYPE_BIT(TYPE_NULL) | IMP_TYPES_FLOAT_OR_DBL | IMP_TYPE_BIT(TYPE_FLOAT) | IMP_TYPE_BIT(TYPE_DOUBLE),
        IMP_TYPES_SOURCE_SET | IMP_TYPE_BIT(TYPE_LANE_FRONTIER) | IMP_TYPE_BIT(TYPE_NULL) | IMP_TYPES_FLOAT_OR_DBL | IMP_TYPE_BIT(TYPE_FLOAT) | IMP_TYPE_BIT(TYPE_DOUBLE),
        IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_BITSET_HANDLE) | IMP_TYPE_BIT(TYPE_LANE_FRONTIER),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });

    // OP_SET_INTERSECT (0x31)
    list.push_back({
        OP_SET_INTERSECT, "OP_SET_INTERSECT", "Set Intersect (BitSet / Node IDs / Node Vectors / Lane Frontier)",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW16, RegFieldLayout::PAYLOAD_HIGH16, RegFieldLayout::NONE,
        IMP_TYPES_SOURCE_SET | IMP_TYPE_BIT(TYPE_LANE_FRONTIER) | IMP_TYPE_BIT(TYPE_NULL) | IMP_TYPES_FLOAT_OR_DBL | IMP_TYPE_BIT(TYPE_FLOAT) | IMP_TYPE_BIT(TYPE_DOUBLE),
        IMP_TYPES_SOURCE_SET | IMP_TYPE_BIT(TYPE_LANE_FRONTIER) | IMP_TYPE_BIT(TYPE_NULL) | IMP_TYPES_FLOAT_OR_DBL | IMP_TYPE_BIT(TYPE_FLOAT) | IMP_TYPE_BIT(TYPE_DOUBLE),
        IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_BITSET_HANDLE) | IMP_TYPE_BIT(TYPE_LANE_FRONTIER),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });

    // OP_SET_DIFFERENCE (0x32)
    list.push_back({
        OP_SET_DIFFERENCE, "OP_SET_DIFFERENCE", "Set Difference (BitSet / Node IDs / Node Vectors / Lane Frontier)",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW16, RegFieldLayout::PAYLOAD_HIGH16, RegFieldLayout::NONE,
        IMP_TYPES_SOURCE_SET | IMP_TYPE_BIT(TYPE_LANE_FRONTIER) | IMP_TYPE_BIT(TYPE_NULL) | IMP_TYPES_FLOAT_OR_DBL | IMP_TYPE_BIT(TYPE_FLOAT) | IMP_TYPE_BIT(TYPE_DOUBLE),
        IMP_TYPES_SOURCE_SET | IMP_TYPE_BIT(TYPE_LANE_FRONTIER) | IMP_TYPE_BIT(TYPE_NULL) | IMP_TYPES_FLOAT_OR_DBL | IMP_TYPE_BIT(TYPE_FLOAT) | IMP_TYPE_BIT(TYPE_DOUBLE),
        IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_BITSET_HANDLE) | IMP_TYPE_BIT(TYPE_LANE_FRONTIER),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });

    // OP_SET_CARDINALITY (0x33)
    list.push_back({
        OP_SET_CARDINALITY, "OP_SET_CARDINALITY", "Set Cardinality",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW16, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_SOURCE_SET | IMP_TYPE_BIT(TYPE_LANE_FRONTIER) | IMP_TYPE_BIT(TYPE_NULL),
        IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_INT64),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });

    // --- Vector Math, Reductions, Projections & Temporal (0x34..0x3D) ---

    // OP_VECTOR_MUL_ATTR (0x34)
    list.push_back({
        OP_VECTOR_MUL_ATTR, "OP_VECTOR_MUL_ATTR", "Scale vector by scalar float immediate (flags=0)",
        0x01, 0x00, 0x01, 0x3F800000u, // 1.0f
        RegFieldLayout::DST_REG, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPES_FLOAT_OR_DBL,
        false, false, 0, true, true
    });
    list.push_back({
        OP_VECTOR_MUL_ATTR, "OP_VECTOR_MUL_ATTR", "Multiply vector by attribute slot (flags=1)",
        0x01, 0x01, 0x01, 0x00000000u, // rel=0, attr=0
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW8, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_FLOAT_OR_DBL | IMP_TYPES_INT_OR_NODE,
        IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPES_FLOAT_OR_DBL,
        false, false, 0, true, true
    });

    // OP_VECTOR_REDUCE_SUM (0x35)
    list.push_back({
        OP_VECTOR_REDUCE_SUM, "OP_VECTOR_REDUCE_SUM", "Sum reduce numeric vector or scalar",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW16, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_FLOAT_OR_DBL | IMP_TYPE_BIT(TYPE_FLOAT) | IMP_TYPE_BIT(TYPE_DOUBLE),
        IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_FLOAT) | IMP_TYPE_BIT(TYPE_DOUBLE),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });

    // OP_VECTOR_DIV (0x36)
    list.push_back({
        OP_VECTOR_DIV, "OP_VECTOR_DIV", "Element-wise vector division",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW16, RegFieldLayout::PAYLOAD_HIGH16, RegFieldLayout::NONE,
        IMP_TYPES_FLOAT_OR_DBL | IMP_TYPE_BIT(TYPE_FLOAT) | IMP_TYPE_BIT(TYPE_DOUBLE) | IMP_TYPES_INT_OR_NODE,
        IMP_TYPES_FLOAT_OR_DBL | IMP_TYPE_BIT(TYPE_FLOAT) | IMP_TYPE_BIT(TYPE_DOUBLE) | IMP_TYPES_INT_OR_NODE,
        IMP_TYPES_NONE,
        IMP_TYPES_FLOAT_OR_DBL,
        false, false, 0, true, true
    });

    // OP_VECTOR_STR_CONCAT (0x37)
    list.push_back({
        OP_VECTOR_STR_CONCAT, "OP_VECTOR_STR_CONCAT", "String vector concatenation",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW16, RegFieldLayout::PAYLOAD_HIGH16, RegFieldLayout::NONE,
        IMP_TYPE_BIT(TYPE_STRING_VECTOR),
        IMP_TYPE_BIT(TYPE_STRING_VECTOR),
        IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_STRING_VECTOR),
        false, false, 0, true, true
    });

    // OP_FLOAT_VECTOR_SCALE (0x38)
    list.push_back({
        OP_FLOAT_VECTOR_SCALE, "OP_FLOAT_VECTOR_SCALE", "In-place float vector scale by immediate (flags=0)",
        0x01, 0x00, 0x01, 0x3F800000u, // 1.0f
        RegFieldLayout::DST_REG, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPES_NONE,
        false, false, 0, true, false
    });
    list.push_back({
        OP_FLOAT_VECTOR_SCALE, "OP_FLOAT_VECTOR_SCALE", "In-place float vector scale by register (flags=1)",
        0x01, 0x01, 0x01, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW16, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPE_BIT(TYPE_FLOAT) | IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_DOUBLE),
        IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPES_NONE,
        false, false, 0, true, false
    });

    // OP_L1_NORM_DIFF (0x39)
    list.push_back({
        OP_L1_NORM_DIFF, "OP_L1_NORM_DIFF", "L1 norm difference between vectors or scalars",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW16, RegFieldLayout::PAYLOAD_HIGH16, RegFieldLayout::NONE,
        IMP_TYPE_BIT(TYPE_FLOAT_VECTOR) | IMP_TYPE_BIT(TYPE_FLOAT) | IMP_TYPE_BIT(TYPE_INT64),
        IMP_TYPE_BIT(TYPE_FLOAT_VECTOR) | IMP_TYPE_BIT(TYPE_FLOAT) | IMP_TYPE_BIT(TYPE_INT64),
        IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_FLOAT),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });

    // OP_PROJECT_STATE (0x3A)
    list.push_back({
        OP_PROJECT_STATE, "OP_PROJECT_STATE", "Project Frontier State Register",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW16, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPE_BIT(TYPE_FRONTIER_STATE),
        IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_FRONTIER_STATE),
        false, false, 0, false, true
    });

    // OP_COALESCE (0x3B)
    list.push_back({
        OP_COALESCE, "OP_COALESCE", "Coalesce two values or vectors",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW16, RegFieldLayout::PAYLOAD_HIGH16, RegFieldLayout::NONE,
        IMP_TYPES_ANY,
        IMP_TYPES_ANY,
        IMP_TYPES_NONE,
        IMP_TYPES_ANY,
        false, false, 0, true, true
    });

    // OP_EXTRACT_VALIDITY (0x3C)
    list.push_back({
        OP_EXTRACT_VALIDITY, "OP_EXTRACT_VALIDITY", "Extract validity bitset from numeric vector",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW16, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NUMERIC_VEC,
        IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_BITSET_HANDLE),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });

    // OP_VECTOR_TIME_VALID_AT (0x3D)
    list.push_back({
        OP_VECTOR_TIME_VALID_AT, "OP_VECTOR_TIME_VALID_AT", "Filter valid temporal intervals at timestamp",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW8, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_FLOAT_VECTOR) | IMP_TYPE_BIT(TYPE_NODE_VECTOR),
        IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_BITSET_HANDLE),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });

    // --- GraphBLAS & Analytics (0x40..0x4B) ---

    // OP_CC_AFFOREST (0x40)
    list.push_back({
        OP_CC_AFFOREST, "OP_CC_AFFOREST", "Connected Components Afforest Algorithm",
        0x00, 0x00, 0x00, 0x00000000u, // rel=0
        RegFieldLayout::DST_REG, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_NODE_VECTOR) | IMP_TYPE_BIT(TYPE_UINT64_VECTOR),
        false, false, 0, true, true
    });

    // OP_MXV (0x41)
    list.push_back({
        OP_MXV, "OP_MXV", "Matrix-Vector Multiply (unmasked, rel=0, plus-times)",
        0xFF, 0x00, 0xFF, 0x00000000u, // rel=0, semiring=0
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW8, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_FLOAT_OR_DBL,
        IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPES_FLOAT_OR_DBL,
        false, false, 0, true, true
    });

    // OP_VXM (0x42)
    list.push_back({
        OP_VXM, "OP_VXM", "Vector-Matrix Multiply (unmasked, rel=0, plus-times)",
        0xFF, 0x00, 0xFF, 0x00000000u, // rel=0, semiring=0
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW8, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_FLOAT_OR_DBL,
        IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPES_FLOAT_OR_DBL,
        false, false, 0, true, true
    });

    // OP_EWISE_ADD (0x43)
    list.push_back({
        OP_EWISE_ADD, "OP_EWISE_ADD", "Element-wise vector addition",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW8, RegFieldLayout::PAYLOAD_BYTE1, RegFieldLayout::NONE,
        IMP_TYPES_FLOAT_OR_DBL,
        IMP_TYPES_FLOAT_OR_DBL,
        IMP_TYPES_NONE,
        IMP_TYPES_FLOAT_OR_DBL,
        false, false, 0, true, true
    });

    // OP_EWISE_MULT (0x44)
    list.push_back({
        OP_EWISE_MULT, "OP_EWISE_MULT", "Element-wise vector multiplication",
        0x00, 0x00, 0x00, (static_cast<uint32_t>(BINARY_OP_MUL) << 16),
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW8, RegFieldLayout::PAYLOAD_BYTE1, RegFieldLayout::NONE,
        IMP_TYPES_FLOAT_OR_DBL,
        IMP_TYPES_FLOAT_OR_DBL,
        IMP_TYPES_NONE,
        IMP_TYPES_FLOAT_OR_DBL,
        false, false, 0, true, true
    });

    // OP_REDUCE (0x45)
    list.push_back({
        OP_REDUCE, "OP_REDUCE", "Vector Scalar Reduction (SUM)",
        0x00, 0x00, 0x00, 0x00000000u, // BINARY_OP_ADD
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW8, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_FLOAT_OR_DBL | IMP_TYPES_NODE_VEC,
        IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_FLOAT) | IMP_TYPE_BIT(TYPE_DOUBLE) | IMP_TYPE_BIT(TYPE_INT64),
        false, false, 0, true, true
    });
    list.push_back({
        OP_REDUCE, "OP_REDUCE", "Vector Argmin Reduction (ARGMIN)",
        0x00, 0x00, 0x00, (static_cast<uint32_t>(BINARY_OP_ARGMIN) << 16),
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW8, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_FLOAT_OR_DBL | IMP_TYPES_NODE_VEC,
        IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_NODE_ID),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });

    // OP_CC_HOOK_COMPRESS (0x46)
    list.push_back({
        OP_CC_HOOK_COMPRESS, "OP_CC_HOOK_COMPRESS", "Connected Components Hook Compress",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPES_NONE,
        false, false, 0, false, false
    });

    // OP_TC_SWEEP_BATCH (0x47)
    list.push_back({
        OP_TC_SWEEP_BATCH, "OP_TC_SWEEP_BATCH", "Triangle Counting Sweep Batch",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPES_NONE,
        false, false, 0, false, false
    });

    // OP_BRANDES_FORWARD (0x48)
    list.push_back({
        OP_BRANDES_FORWARD, "OP_BRANDES_FORWARD", "Brandes Betweenness Centrality Forward",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPES_NONE,
        false, false, 0, false, false
    });

    // OP_BRANDES_BACKWARD (0x49)
    list.push_back({
        OP_BRANDES_BACKWARD, "OP_BRANDES_BACKWARD", "Brandes Betweenness Centrality Backward",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPES_NONE,
        false, false, 0, false, false
    });

    // OP_DELTA_STEP_RELAX (0x4A)
    list.push_back({
        OP_DELTA_STEP_RELAX, "OP_DELTA_STEP_RELAX", "Delta Stepping SSSP Relaxation",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPES_NONE,
        false, false, 0, false, false
    });

    // OP_READ_EDGE_WEIGHT (0x4B)
    list.push_back({
        OP_READ_EDGE_WEIGHT, "OP_READ_EDGE_WEIGHT", "Read edge weight attribute (rel=0)",
        0x80, 0x00, 0x80, 0x00000000u, // rel=0
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW8, RegFieldLayout::PAYLOAD_BYTE1, RegFieldLayout::NONE,
        IMP_TYPES_INT_OR_NODE,
        IMP_TYPES_INT_OR_NODE,
        IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_FLOAT),
        false, false, 0, true, true
    });

    // --- Control Flow & Traps (0x50..0x5C) ---

    // OP_JMP (0x50)
    list.push_back({
        OP_JMP, "OP_JMP", "Unconditional Jump",
        0x00, 0x00, 0x00, 0x00000001u, // offset = +1
        RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPES_NONE,
        false, false, 0, false, false
    });

    // OP_JZ (0x51)
    list.push_back({
        OP_JZ, "OP_JZ", "Jump if Zero Flag Set",
        0x00, 0x00, 0x00, 0x00000001u, // offset = +1
        RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPES_NONE,
        false, false, 0, false, false
    });

    // OP_JNZ (0x52)
    list.push_back({
        OP_JNZ, "OP_JNZ", "Jump if Zero Flag Clear",
        0x00, 0x00, 0x00, 0x00000001u, // offset = +1
        RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPES_NONE,
        false, false, 0, false, false
    });

    // OP_LOOP_DECR (0x53)
    list.push_back({
        OP_LOOP_DECR, "OP_LOOP_DECR", "Loop Counter Decrement and Branch",
        0x00, 0x00, 0x00, 0x00000001u, // offset = +1
        RegFieldLayout::DST_REG, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_INT64),
        false, false, IMPULSE_VM_FLAG_ZF, false, true
    });

    // OP_STABLE_CHECK (0x54)
    list.push_back({
        OP_STABLE_CHECK, "OP_STABLE_CHECK", "Fixed-point stability subset check",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW16, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPE_BIT(TYPE_BITSET_HANDLE) | IMP_TYPES_INT_OR_NODE,
        IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPES_NONE,
        false, false, IMPULSE_VM_FLAG_ST | IMPULSE_VM_FLAG_ZF, true, false
    });

    // OP_CALL (0x55)
    list.push_back({
        OP_CALL, "OP_CALL", "Function Call",
        0x00, 0x00, 0x00, 0x00000001u, // callee = PC+1
        RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPES_NONE,
        false, false, 0, false, false
    });

    // OP_RET (0x56)
    list.push_back({
        OP_RET, "OP_RET", "Function Return",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPES_NONE,
        false, false, 0, false, false
    });

    // OP_ENTER_FRAME (0x57)
    list.push_back({
        OP_ENTER_FRAME, "OP_ENTER_FRAME", "Enter Stack Frame",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPES_NONE,
        false, false, 0, false, false
    });

    // OP_LEAVE_FRAME (0x58)
    list.push_back({
        OP_LEAVE_FRAME, "OP_LEAVE_FRAME", "Leave Stack Frame",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPES_NONE,
        false, false, 0, false, false
    });

    // OP_THROW (0x5A)
    list.push_back({
        OP_THROW, "OP_THROW", "User Exception Throw",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPES_NONE,
        false, false, 0, false, false
    });

    // OP_ASSERT (0x5B)
    list.push_back({
        OP_ASSERT, "OP_ASSERT", "Register Value Assertion",
        0x01, 0x00, 0x01, 2u, // expected_val = 2 (matches populate_register for TYPE_INT64)
        RegFieldLayout::NONE, RegFieldLayout::DST_REG, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_FLOAT) | IMP_TYPE_BIT(TYPE_BOOLEAN),
        IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPES_NONE,
        false, false, 0, false, false
    });

    // OP_TRAP (0x5C)
    list.push_back({
        OP_TRAP, "OP_TRAP", "Debugger Trap",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPES_NONE,
        false, false, 0, false, false
    });
}

} // namespace impulse::vm

#endif // IMPULSE_CONTRACTS_CHUNK2_HPP
