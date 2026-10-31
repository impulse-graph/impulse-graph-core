/**
 * @file contracts_chunk3.hpp
 * @brief Formal Opcode Contracts for Graph Analytics, Regs, Columnar Gathers & Walks (0x60..0x8F).
 */

#ifndef IMPULSE_CONTRACTS_CHUNK3_HPP
#define IMPULSE_CONTRACTS_CHUNK3_HPP

#include <vector>

namespace impulse::vm {

inline void append_chunk3_contracts(std::vector<ImpulseOpcodeVariantContract>& list) {
    // --- Extended Domain Opcodes (0x60 .. 0x6C) ---

    // OP_SAMPLE_NEIGHBORS (0x60)
    list.push_back({
        OP_SAMPLE_NEIGHBORS, "OP_SAMPLE_NEIGHBORS", "Sample Neighbors (GNN Target)",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW16, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_SOURCE_SET, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_NODE_VECTOR) | IMP_TYPE_BIT(TYPE_BITSET_HANDLE),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });

    // OP_RANDOM_WALK (0x61)
    list.push_back({
        OP_RANDOM_WALK, "OP_RANDOM_WALK", "Node Random Walk Traversal",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW16, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_SOURCE_SET, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_NODE_VECTOR),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });

    // OP_SCATTER_GATHER (0x62)
    list.push_back({
        OP_SCATTER_GATHER, "OP_SCATTER_GATHER", "Scatter Gather Node Feature Vectors",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW16, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_SOURCE_SET | IMP_TYPES_NUMERIC_VEC, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPES_NUMERIC_VEC,
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });

    // OP_REBAC_CHECK (0x63)
    list.push_back({
        OP_REBAC_CHECK, "OP_REBAC_CHECK", "ReBAC Zanzibar Check Path Evaluation",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW16, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_BITSET_HANDLE), IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_BOOLEAN) | IMP_TYPE_BIT(TYPE_INT64),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });

    // OP_ROARING_BITMAP_AND (0x64)
    list.push_back({
        OP_ROARING_BITMAP_AND, "OP_ROARING_BITMAP_AND", "Bitwise Logical AND on Bitsets",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW16, RegFieldLayout::PAYLOAD_HIGH16, RegFieldLayout::NONE,
        IMP_TYPE_BIT(TYPE_BITSET_HANDLE) | IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_NULL),
        IMP_TYPE_BIT(TYPE_BITSET_HANDLE) | IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_NULL),
        IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_BITSET_HANDLE),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });

    // OP_ISLAND_DETECT (0x65)
    list.push_back({
        OP_ISLAND_DETECT, "OP_ISLAND_DETECT", "Power Grid Component Island Traversal",
        0x80, 0x00, 0x80, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW8, RegFieldLayout::PAYLOAD_BYTE1, RegFieldLayout::NONE,
        IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_BITSET_HANDLE),
        IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_BITSET_HANDLE),
        IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_INT64),
        true, false, 0, true, true
    });

    // OP_SPARSE_MATVEC (0x66)
    list.push_back({
        OP_SPARSE_MATVEC, "OP_SPARSE_MATVEC", "Sparse Matrix-Vector Product",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW16, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_FLOAT_OR_DBL | IMP_TYPES_NODE_VEC, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPES_FLOAT_OR_DBL,
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });

    // OP_LOUVAIN_MODULARITY (0x67)
    list.push_back({
        OP_LOUVAIN_MODULARITY, "OP_LOUVAIN_MODULARITY", "Louvain Community Modularity Optimization",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW16, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_SOURCE_SET, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_UINT64_VECTOR) | IMP_TYPE_BIT(TYPE_NODE_VECTOR),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });

    // OP_KCORE_DECOMPOSITION (0x68)
    list.push_back({
        OP_KCORE_DECOMPOSITION, "OP_KCORE_DECOMPOSITION", "Node K-Core Graph Shell Decomposition",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW8, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_RELATION_ID), IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_FLOAT_VECTOR),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });

    // OP_MOTIF_MATCH_3 (0x69)
    list.push_back({
        OP_MOTIF_MATCH_3, "OP_MOTIF_MATCH_3", "Search 3-Node Motifs/Subgraphs",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW16, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_SOURCE_SET, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_INT64),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });

    // OP_GRAPH_ISOMORPHISM (0x6A)
    list.push_back({
        OP_GRAPH_ISOMORPHISM, "OP_GRAPH_ISOMORPHISM", "Compare Graph Topology Isomorphism",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW16, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_SOURCE_SET, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_BOOLEAN) | IMP_TYPE_BIT(TYPE_INT64),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });

    // OP_ROARING_BITMAP_OR (0x6B)
    list.push_back({
        OP_ROARING_BITMAP_OR, "OP_ROARING_BITMAP_OR", "Bitwise Logical OR on Bitsets",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW16, RegFieldLayout::PAYLOAD_HIGH16, RegFieldLayout::NONE,
        IMP_TYPE_BIT(TYPE_BITSET_HANDLE) | IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_NULL),
        IMP_TYPE_BIT(TYPE_BITSET_HANDLE) | IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_NULL),
        IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_BITSET_HANDLE),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });

    // OP_ROARING_BITMAP_AND_NOT (0x6C)
    list.push_back({
        OP_ROARING_BITMAP_AND_NOT, "OP_ROARING_BITMAP_AND_NOT", "Bitwise Logical AND-NOT Subtraction on Bitsets",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW16, RegFieldLayout::PAYLOAD_HIGH16, RegFieldLayout::NONE,
        IMP_TYPE_BIT(TYPE_BITSET_HANDLE) | IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_NULL),
        IMP_TYPE_BIT(TYPE_BITSET_HANDLE) | IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_NULL),
        IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_BITSET_HANDLE),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });

    // --- Register Control & Scratch Memory Instructions (0x70 .. 0x75) ---

    // OP_MOV (0x70)
    list.push_back({
        OP_MOV, "OP_MOV", "Move Register Value and Type",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW16, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_ANY, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPES_ANY,
        false, false, 0, false, true
    });

    // OP_CLEAR_REG (0x71)
    list.push_back({
        OP_CLEAR_REG, "OP_CLEAR_REG", "Clear Register to TYPE_NULL",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_NULL),
        false, false, 0, false, true
    });

    // OP_LOAD_INDIRECT (0x72) - Flag 0: Register Dereference
    list.push_back({
        OP_LOAD_INDIRECT, "OP_LOAD_INDIRECT", "Load Indirect Register Dereference (flags=0)",
        0x01, 0x00, 0x01, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW16, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_INT_OR_NODE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPES_ANY,
        false, false, 0, false, true
    });

    // OP_LOAD_INDIRECT (0x72) - Flag 1: Vector Element Dereference
    list.push_back({
        OP_LOAD_INDIRECT, "OP_LOAD_INDIRECT", "Load Indirect Vector Element (flags=1)",
        0x01, 0x01, 0x01, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW16, RegFieldLayout::PAYLOAD_HIGH16, RegFieldLayout::NONE,
        (IMP_TYPE_BIT(TYPE_FLOAT_VECTOR) | IMP_TYPES_INT_OR_NODE), IMP_TYPES_INT_OR_NODE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_FLOAT),
        false, false, 0, true, true
    });

    // OP_ALLOC_SCRATCH (0x73)
    list.push_back({
        OP_ALLOC_SCRATCH, "OP_ALLOC_SCRATCH", "Allocate Local Scratch Memory Buffer",
        0x00, 0x00, 0x00, 0x00000400u, // 1024 bytes
        RegFieldLayout::DST_REG, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_INT64),
        false, false, IMPULSE_VM_FLAG_ZF, false, true
    });

    // OP_ASSERT_SCRATCH_BYTES (0x74)
    list.push_back({
        OP_ASSERT_SCRATCH_BYTES, "OP_ASSERT_SCRATCH_BYTES", "Assert Scratch Memory Bounds Capacity",
        0x00, 0x00, 0x00, 0x00000400u, // 1024 bytes
        RegFieldLayout::DST_REG, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_INT64),
        false, false, IMPULSE_VM_FLAG_ZF, false, true
    });

    // OP_SET_MAX_DOP (0x75)
    list.push_back({
        OP_SET_MAX_DOP, "OP_SET_MAX_DOP", "Set Concurrency Degree of Parallelism",
        0x00, 0x00, 0x00, 0x00000004u, // 4 threads
        RegFieldLayout::DST_REG, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_INT64),
        false, false, IMPULSE_VM_FLAG_ZF, false, true
    });

    // --- Columnar Gathers & Multi-Layout Sweeps (0x80 .. 0x8F) ---

    // OP_LOAD_COLUMN_VECTOR (0x80)
    list.push_back({
        OP_LOAD_COLUMN_VECTOR, "OP_LOAD_COLUMN_VECTOR", "Load Column Attribute Vector into Register",
        0x00, 0x00, 0x00, 0x00000000u, // domain 0, attr 0
        RegFieldLayout::DST_REG, RegFieldLayout::NONE, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_NONE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_FLOAT_VECTOR),
        false, false, 0, true, true
    });

    // OP_GATHER_NODE_ATTR (0x81) - Standard 8-bit
    list.push_back({
        OP_GATHER_NODE_ATTR, "OP_GATHER_NODE_ATTR", "Gather Node Attribute Vector (flags=0, standard)",
        0x80, 0x00, 0x80, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW8, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPE_BIT(TYPE_BITSET_HANDLE) | IMP_TYPES_INT_OR_NODE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_FLOAT_VECTOR),
        false, false, 0, true, true
    });

    // OP_GATHER_NODE_ATTR (0x81) - Wide 16-bit
    list.push_back({
        OP_GATHER_NODE_ATTR, "OP_GATHER_NODE_ATTR", "Gather Node Attribute Vector (flags=0x80, wide)",
        0x80, 0x80, 0x80, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW16, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPE_BIT(TYPE_BITSET_HANDLE) | IMP_TYPES_INT_OR_NODE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_FLOAT_VECTOR),
        false, false, 0, true, true
    });

    // OP_GATHER_EDGE_ATTR (0x82)
    list.push_back({
        OP_GATHER_EDGE_ATTR, "OP_GATHER_EDGE_ATTR", "Gather Edge Attribute Vector",
        0x80, 0x00, 0x80, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW8, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPE_BIT(TYPE_BITSET_HANDLE) | IMP_TYPES_INT_OR_NODE, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_FLOAT_VECTOR),
        false, false, 0, true, true
    });

    // OP_BRIN_ZONE_SKIP (0x83)
    list.push_back({
        OP_BRIN_ZONE_SKIP, "OP_BRIN_ZONE_SKIP", "BRIN Zone Page Skipping Filter",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW8, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_BITSET_HANDLE), IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_INT64),
        false, false, IMPULSE_VM_FLAG_ZF, false, true
    });

    // OP_CSR_WALK_DIRECT_STORE (0x84)
    list.push_back({
        OP_CSR_WALK_DIRECT_STORE, "OP_CSR_WALK_DIRECT_STORE", "CSR 1-Hop Walk Direct Store into Bitset",
        0x80, 0x00, 0x80, 0x00000000u, // rel 0
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW8, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_BITSET_HANDLE), IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_BITSET_HANDLE),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });

    // OP_CSR_WALK_DENSE_STREAM (0x85)
    list.push_back({
        OP_CSR_WALK_DENSE_STREAM, "OP_CSR_WALK_DENSE_STREAM", "CSR Walk Streaming to Dense Frontier",
        0x00, 0x00, 0x00, 0x00000000u, // rel 0
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW8, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_BITSET_HANDLE), IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_BITSET_HANDLE),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });

    // OP_COO_WALK (0x86)
    list.push_back({
        OP_COO_WALK, "OP_COO_WALK", "COO Adjacency Matrix Traversal Walk",
        0x00, 0x00, 0x00, 0x00000000u, // rel 0
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW8, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_BITSET_HANDLE), IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_BITSET_HANDLE),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });

    // OP_CSC_WALK_DIRECT_STORE (0x87)
    list.push_back({
        OP_CSC_WALK_DIRECT_STORE, "OP_CSC_WALK_DIRECT_STORE", "CSC Reverse Traversal Direct Store",
        0x00, 0x00, 0x00, 0x00000000u, // rel 0
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW8, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_BITSET_HANDLE), IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_BITSET_HANDLE),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });

    // OP_FIXPOINT_KLEENE_STAR (0x88)
    list.push_back({
        OP_FIXPOINT_KLEENE_STAR, "OP_FIXPOINT_KLEENE_STAR", "Transitive Reachability Kleene Star Fixed-Point",
        0x00, 0x00, 0x00, 0x00000000u, // rel 0
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW8, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_BITSET_HANDLE), IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_BITSET_HANDLE),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });

    // OP_SWAP_REG (0x89)
    list.push_back({
        OP_SWAP_REG, "OP_SWAP_REG", "Swap Register Values and Types",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW16, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_ANY, IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPES_ANY,
        false, false, 0, false, true
    });

    // OP_FRONTIER_DIFF (0x8A)
    list.push_back({
        OP_FRONTIER_DIFF, "OP_FRONTIER_DIFF", "Frontier Bitset Difference (curr AND NOT prev)",
        0x00, 0x00, 0x00, 0x00000000u,
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW8, RegFieldLayout::PAYLOAD_BYTE1, RegFieldLayout::NONE,
        IMP_TYPE_BIT(TYPE_BITSET_HANDLE), IMP_TYPE_BIT(TYPE_BITSET_HANDLE), IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_BITSET_HANDLE),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });

    // OP_COO_WALK_FILTERED (0x8B)
    list.push_back({
        OP_COO_WALK_FILTERED, "OP_COO_WALK_FILTERED", "COO Filtered Traversal Walk",
        0x00, 0x00, 0x00, 0x00000000u, // rel 0
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW8, RegFieldLayout::PAYLOAD_BYTE1, RegFieldLayout::NONE,
        IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_BITSET_HANDLE),
        IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_BITSET_HANDLE),
        IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_BITSET_HANDLE),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });

    // OP_COO_WALK_REDUCE (0x8C)
    list.push_back({
        OP_COO_WALK_REDUCE, "OP_COO_WALK_REDUCE", "COO Traversal with Degree Reduction",
        0x00, 0x00, 0x00, 0x00000000u, // rel 0
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW8, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_BITSET_HANDLE), IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_FLOAT_VECTOR),
        false, false, 0, true, true
    });

    // OP_COO_WALK_DIRECT_STORE (0x8D)
    list.push_back({
        OP_COO_WALK_DIRECT_STORE, "OP_COO_WALK_DIRECT_STORE", "COO Traversal Direct Store into Bitset",
        0x00, 0x00, 0x00, 0x00000000u, // rel 0
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW8, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_BITSET_HANDLE), IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_BITSET_HANDLE),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });

    // OP_DENSE_WALK (0x8E)
    list.push_back({
        OP_DENSE_WALK, "OP_DENSE_WALK", "Dense Frontier Graph Traversal",
        0x00, 0x00, 0x00, 0x00000000u, // rel 0
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW8, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_BITSET_HANDLE), IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_BITSET_HANDLE),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });

    // OP_DENSE_WALK_BITMATRIX (0x8F)
    list.push_back({
        OP_DENSE_WALK_BITMATRIX, "OP_DENSE_WALK_BITMATRIX", "Dense BitMatrix Graph Traversal",
        0x00, 0x00, 0x00, 0x00000000u, // rel 0
        RegFieldLayout::DST_REG, RegFieldLayout::PAYLOAD_LOW8, RegFieldLayout::NONE, RegFieldLayout::NONE,
        IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_BITSET_HANDLE), IMP_TYPES_NONE, IMP_TYPES_NONE,
        IMP_TYPE_BIT(TYPE_BITSET_HANDLE),
        false, false, IMPULSE_VM_FLAG_ZF, true, true
    });
}

} // namespace impulse::vm

#endif // IMPULSE_CONTRACTS_CHUNK3_HPP
