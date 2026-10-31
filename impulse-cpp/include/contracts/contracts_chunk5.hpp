/**
 * @file contracts_chunk5.hpp
 * @brief Formal Opcode Contracts for Multi-Lane Bit-Sliced & Hybrid Vector ISA (0xC0..0xCB).
 */

#ifndef IMPULSE_CONTRACTS_CHUNK5_HPP
#define IMPULSE_CONTRACTS_CHUNK5_HPP

#include <vector>

namespace impulse::vm {

inline void append_chunk5_contracts(std::vector<ImpulseOpcodeVariantContract>& list) {
    // --- Multi-Lane Bit-Sliced Execution & Hybrid Vector (0xC0..0xCB) ---

    // OP_LANE_INIT_SEEDS (0xC0)
    list.push_back({
        OP_LANE_INIT_SEEDS, "OP_LANE_INIT_SEEDS", "Init Seeds from Node Vector/BitSet/ID (flags=0)",
        0x01, 0x00, 0x01, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW16, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPE_BIT(TYPE_NULL) | IMP_TYPE_BIT(TYPE_NODE_ID) | IMP_TYPE_BIT(TYPE_INT64) |
        IMP_TYPE_BIT(TYPE_BITSET_HANDLE) | IMP_TYPE_BIT(TYPE_NODE_VECTOR) | IMP_TYPE_BIT(TYPE_UINT64_VECTOR),
        IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_LANE_FRONTIER),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });
    list.push_back({
        OP_LANE_INIT_SEEDS, "OP_LANE_INIT_SEEDS", "Init Range Seeds (flags=1)",
        0x01, 0x01, 0x01, 0x00040000u, // k = 4
        RegFieldLayout::DST_REG, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_LANE_FRONTIER),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });

    // OP_LANE_INIT_GROUPS (0xC1)
    list.push_back({
        OP_LANE_INIT_GROUPS, "OP_LANE_INIT_GROUPS", "Init Node Vector Groups (flags=0)",
        0x03, 0x00, 0x03, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW16, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPE_BIT(TYPE_NODE_VECTOR) | IMP_TYPE_BIT(TYPE_UINT64_VECTOR),
        IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_LANE_FRONTIER),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });
    list.push_back({
        OP_LANE_INIT_GROUPS, "OP_LANE_INIT_GROUPS", "Init Node Vector Groups Inverted (flags=1)",
        0x03, 0x01, 0x03, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW16, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPE_BIT(TYPE_NODE_VECTOR) | IMP_TYPE_BIT(TYPE_UINT64_VECTOR),
        IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_LANE_FRONTIER),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });

    // OP_LANE_SET_EDGE_MASK (0xC2)
    list.push_back({
        OP_LANE_SET_EDGE_MASK, "OP_LANE_SET_EDGE_MASK", "Clear Edge Mask (flags=0)",
        0x01, 0x00, 0x01, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW8, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPE_BIT(TYPE_LANE_FRONTIER), IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_LANE_FRONTIER),
        false, false, 0, true, true
    });
    list.push_back({
        OP_LANE_SET_EDGE_MASK, "OP_LANE_SET_EDGE_MASK", "Set Edge Mask from Register (flags=1)",
        0x01, 0x01, 0x01, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW8, RegFieldLayout::PAYLOAD_BYTE1, RegFieldLayout::NONE,
        IMP_TYPE_BIT(TYPE_LANE_FRONTIER),
        IMP_TYPE_BIT(TYPE_INT64) | IMP_TYPE_BIT(TYPE_NODE_ID) | IMP_TYPES_NODE_VEC, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_LANE_FRONTIER),
        false, false, 0, true, true
    });

    // OP_LANE_CSR_WALK (0xC3)
    list.push_back({
        OP_LANE_CSR_WALK, "OP_LANE_CSR_WALK", "Multi-Lane CSR Walk (flags=0)",
        0x03, 0x00, 0x03, 0x00000000u, // rel=0
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW8, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPE_BIT(TYPE_LANE_FRONTIER),
        IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_LANE_FRONTIER),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });
    list.push_back({
        OP_LANE_CSR_WALK, "OP_LANE_CSR_WALK", "Multi-Lane CSR Walk with Accumulate (flags=1)",
        0x03, 0x01, 0x03, 0x00000000u, // rel=0
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW8, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPE_BIT(TYPE_LANE_FRONTIER),
        IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_LANE_FRONTIER),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });

    // OP_LANE_CSC_WALK (0xC4)
    list.push_back({
        OP_LANE_CSC_WALK, "OP_LANE_CSC_WALK", "Multi-Lane CSC Walk (flags=0)",
        0x03, 0x00, 0x03, 0x00000000u, // rel=0
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW8, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPE_BIT(TYPE_LANE_FRONTIER),
        IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_LANE_FRONTIER),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });
    list.push_back({
        OP_LANE_CSC_WALK, "OP_LANE_CSC_WALK", "Multi-Lane CSC Walk with Accumulate (flags=1)",
        0x03, 0x01, 0x03, 0x00000000u, // rel=0
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW8, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPE_BIT(TYPE_LANE_FRONTIER),
        IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_LANE_FRONTIER),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });

    // OP_LANE_PRUNE_MASK (0xC5)
    list.push_back({
        OP_LANE_PRUNE_MASK, "OP_LANE_PRUNE_MASK", "Prune Mask (Mode 0: Mask AND)",
        0x03, 0x00, 0x03, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW8, RegFieldLayout::PAYLOAD_BYTE1, RegFieldLayout::NONE,
        IMP_TYPE_BIT(TYPE_LANE_FRONTIER),
        IMP_TYPE_BIT(TYPE_BITSET_HANDLE) | IMP_TYPES_NODE_VEC | IMP_TYPES_INT_OR_NODE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_LANE_FRONTIER),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });
    list.push_back({
        OP_LANE_PRUNE_MASK, "OP_LANE_PRUNE_MASK", "Prune Mask (Mode 1: Mask AND-NOT, half immediate)",
        0x03, 0x01, 0x03, 0x00000100u, // half=1
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW8, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPE_BIT(TYPE_LANE_FRONTIER),
        IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_LANE_FRONTIER),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });
    list.push_back({
        OP_LANE_PRUNE_MASK, "OP_LANE_PRUNE_MASK", "Prune Mask (Mode 3: Invert Lanes)",
        0x03, 0x03, 0x03, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW8, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPE_BIT(TYPE_LANE_FRONTIER), IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_LANE_FRONTIER),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });
    list.push_back({
        OP_LANE_PRUNE_MASK, "OP_LANE_PRUNE_MASK", "Prune Mask (Mode 2: Degree Hub Pruning)",
        0x03, 0x02, 0x03, 0x00020400u, // max_deg=4, min_k=2
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW8, RegFieldLayout::PAYLOAD_BYTE1, RegFieldLayout::NONE,
        IMP_TYPE_BIT(TYPE_LANE_FRONTIER),
        IMP_TYPES_INT_OR_NODE | IMP_TYPES_NODE_VEC, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_LANE_FRONTIER),
        true, false, IMPULSE_VM_FLAG_ZF, true, true
    });

    // OP_LANE_FILTER_QUORUM (0xC6)
    list.push_back({
        OP_LANE_FILTER_QUORUM, "OP_LANE_FILTER_QUORUM", "Basic Quorum Filter (flags=0)",
        0x01, 0x00, 0x01, 0x00020000u, // min_k = 2
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW16, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPE_BIT(TYPE_LANE_FRONTIER), IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_BITSET_HANDLE),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });
    list.push_back({
        OP_LANE_FILTER_QUORUM, "OP_LANE_FILTER_QUORUM", "Extended Quorum with Req/Forbid Masks (flags=1)",
        0x01, 0x01, 0x01, 0x00000200u, // min_k = 2
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW8, RegFieldLayout::PAYLOAD_BYTE2, RegFieldLayout::PAYLOAD_BYTE3,
        IMP_TYPE_BIT(TYPE_LANE_FRONTIER),
        IMP_TYPES_INT_OR_NODE, IMP_TYPES_INT_OR_NODE,
        IMP_TYPE_BIT(TYPE_BITSET_HANDLE),
        true, true, IMPULSE_VM_FLAG_ZF, true, true
    });

    // OP_LANE_REDUCE_AND (0xC7)
    list.push_back({
        OP_LANE_REDUCE_AND, "OP_LANE_REDUCE_AND", "Reduce Bitwise AND (all reached)",
        0x03, 0x00, 0x03, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW16, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPE_BIT(TYPE_LANE_FRONTIER), IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_INT64),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });
    list.push_back({
        OP_LANE_REDUCE_AND, "OP_LANE_REDUCE_AND", "Reduce Bitwise AND (islanded mask)",
        0x03, 0x01, 0x03, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW16, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPE_BIT(TYPE_LANE_FRONTIER), IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_INT64),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });
    list.push_back({
        OP_LANE_REDUCE_AND, "OP_LANE_REDUCE_AND", "Reduce Bitwise AND (universal node count)",
        0x03, 0x02, 0x03, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW16, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPE_BIT(TYPE_LANE_FRONTIER), IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_INT64),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });

    // OP_LANE_REDUCE_ATTR (0xC8)
    list.push_back({
        OP_LANE_REDUCE_ATTR, "OP_LANE_REDUCE_ATTR", "Snapshot Attribute Scalar Reduction (flags=0, SUM)",
        0x01, 0x00, 0x01, 0x00000000u | (static_cast<uint32_t>(BINARY_OP_ADD) << 16),
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW8, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPE_BIT(TYPE_LANE_FRONTIER), IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_FLOAT_VECTOR),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });
    list.push_back({
        OP_LANE_REDUCE_ATTR, "OP_LANE_REDUCE_ATTR", "Register Vector Scalar Reduction (flags=1, SUM)",
        0x01, 0x01, 0x01, (static_cast<uint32_t>(BINARY_OP_ADD) << 16),
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW8, RegFieldLayout::PAYLOAD_BYTE1, RegFieldLayout::NONE,
        IMP_TYPE_BIT(TYPE_LANE_FRONTIER),
        IMP_TYPES_FLOAT_OR_DBL | IMP_TYPES_NODE_VEC, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_FLOAT_VECTOR),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });
    list.push_back({
        OP_LANE_REDUCE_ATTR, "OP_LANE_REDUCE_ATTR", "Register Vector ARGMIN Reduction (flags=1, ARGMIN)",
        0x01, 0x01, 0x01, (static_cast<uint32_t>(BINARY_OP_ARGMIN) << 16),
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW8, RegFieldLayout::PAYLOAD_BYTE1, RegFieldLayout::NONE,
        IMP_TYPE_BIT(TYPE_LANE_FRONTIER),
        IMP_TYPES_FLOAT_OR_DBL | IMP_TYPES_NODE_VEC, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_UINT64_VECTOR),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });

    // OP_LANE_SET_COVER (0xC9)
    list.push_back({
        OP_LANE_SET_COVER, "OP_LANE_SET_COVER", "Greedy Set Cover (flags=0)",
        0x01, 0x00, 0x01, 0x00080000u, // max_pick=8
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW8, RegFieldLayout::PAYLOAD_BYTE1, RegFieldLayout::NONE,
        IMP_TYPE_BIT(TYPE_LANE_FRONTIER),
        IMP_TYPES_INT_OR_NODE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_NODE_VECTOR),
        true, false, IMPULSE_VM_FLAG_ZF, true, true
    });
    list.push_back({
        OP_LANE_SET_COVER, "OP_LANE_SET_COVER", "Exact 2-Node Pair Cover (flags=1)",
        0x01, 0x01, 0x01, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW8, RegFieldLayout::PAYLOAD_BYTE1, RegFieldLayout::NONE,
        IMP_TYPE_BIT(TYPE_LANE_FRONTIER),
        IMP_TYPES_INT_OR_NODE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_NODE_VECTOR),
        true, false, IMPULSE_VM_FLAG_ZF, true, true
    });

    // OP_VECTOR_ANN_SEARCH (0xCA)
    list.push_back({
        OP_VECTOR_ANN_SEARCH, "OP_VECTOR_ANN_SEARCH", "Vamana ANN Search (flags=0 -> BitSet)",
        0x01, 0x00, 0x01, 0x00000400u, // top_k=4
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW8, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPE_BIT(TYPE_FLOAT_VECTOR), IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_BITSET_HANDLE),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });
    list.push_back({
        OP_VECTOR_ANN_SEARCH, "OP_VECTOR_ANN_SEARCH", "Vamana ANN Search (flags=1 -> Fused Multi-Lane)",
        0x01, 0x01, 0x01, 0x00000400u, // top_k=4
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW8, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPE_BIT(TYPE_FLOAT_VECTOR), IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_LANE_FRONTIER),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });

    // OP_VECTOR_RERANK_DOT (0xCB)
    list.push_back({
        OP_VECTOR_RERANK_DOT, "OP_VECTOR_RERANK_DOT", "Vector Rerank Dot/Cosine/L2",
        0x00, 0x00, 0x03, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW8, RegFieldLayout::PAYLOAD_BYTE1, RegFieldLayout::NONE,
        IMP_TYPE_BIT(TYPE_BITSET_HANDLE) | IMP_TYPE_BIT(TYPE_LANE_FRONTIER),
        IMP_TYPE_BIT(TYPE_FLOAT_VECTOR), IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_FLOAT_VECTOR),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });
}

} // namespace impulse::vm

#endif // IMPULSE_CONTRACTS_CHUNK5_HPP
