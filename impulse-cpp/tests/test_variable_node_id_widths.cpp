#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <iostream>

#include "impulse_graph.h"
#include "impulse_vm.h"

// Helper to construct mock CSR / CSC buffers of specified width
// Graph topology for test:
// Source domain: 4 nodes (0, 1, 2, 3)
// Target domain: 4 nodes (0, 1, 2, 3)
// Edges:
// 0 -> [1, 2]
// 1 -> [2, 3]
// 2 -> [0, 3]
// 3 -> []
//
// In reverse (CSC):
// 0 <- [2]
// 1 <- [0]
// 2 <- [0, 1]
// 3 <- [1, 2]

void run_matrix_test(uint8_t src_width, uint8_t dst_width, uint8_t edge_idx_width) {
    std::cout << "Testing combination: Src NodeID Width = " << (int)src_width * 8
              << " bit, Dst NodeID Width = " << (int)dst_width * 8
              << " bit, Edge Index Width = " << (int)edge_idx_width * 8 << " bit..." << std::endl;

    impulse_vm_context_t* ctx = impulse_vm_context_create(nullptr);
    assert(ctx != nullptr);

    // Setup buffers dynamically based on width configuration
    std::vector<uint8_t> csr_off_bytes;
    std::vector<uint8_t> csr_tgt_bytes;
    std::vector<uint8_t> csc_off_bytes;
    std::vector<uint8_t> csc_tgt_bytes;

    uint64_t raw_csr_offsets[5] = {0, 2, 4, 6, 6};
    uint64_t raw_csr_targets[6] = {1, 2, 2, 3, 0, 3};
    uint64_t raw_csc_offsets[5] = {0, 1, 2, 4, 6};
    uint64_t raw_csc_targets[6] = {2, 0, 0, 1, 1, 2};

    // Serialize CSR offsets
    csr_off_bytes.resize(5 * edge_idx_width);
    for (int i = 0; i < 5; ++i) {
        if (edge_idx_width == 4) {
            uint32_t val = static_cast<uint32_t>(raw_csr_offsets[i]);
            std::memcpy(&csr_off_bytes[i * 4], &val, 4);
        } else {
            uint64_t val = raw_csr_offsets[i];
            std::memcpy(&csr_off_bytes[i * 8], &val, 8);
        }
    }

    // Serialize CSR targets
    csr_tgt_bytes.resize(6 * dst_width);
    for (int i = 0; i < 6; ++i) {
        if (dst_width == 2) {
            uint16_t val = static_cast<uint16_t>(raw_csr_targets[i]);
            std::memcpy(&csr_tgt_bytes[i * 2], &val, 2);
        } else if (dst_width == 4) {
            uint32_t val = static_cast<uint32_t>(raw_csr_targets[i]);
            std::memcpy(&csr_tgt_bytes[i * 4], &val, 4);
        } else {
            uint64_t val = raw_csr_targets[i];
            std::memcpy(&csr_tgt_bytes[i * 8], &val, 8);
        }
    }

    // Serialize CSC offsets
    csc_off_bytes.resize(5 * edge_idx_width);
    for (int i = 0; i < 5; ++i) {
        if (edge_idx_width == 4) {
            uint32_t val = static_cast<uint32_t>(raw_csc_offsets[i]);
            std::memcpy(&csc_off_bytes[i * 4], &val, 4);
        } else {
            uint64_t val = raw_csc_offsets[i];
            std::memcpy(&csc_off_bytes[i * 8], &val, 8);
        }
    }

    // Serialize CSC targets (points to source node IDs)
    csc_tgt_bytes.resize(6 * src_width);
    for (int i = 0; i < 6; ++i) {
        if (src_width == 2) {
            uint16_t val = static_cast<uint16_t>(raw_csc_targets[i]);
            std::memcpy(&csc_tgt_bytes[i * 2], &val, 2);
        } else if (src_width == 4) {
            uint32_t val = static_cast<uint32_t>(raw_csc_targets[i]);
            std::memcpy(&csc_tgt_bytes[i * 4], &val, 4);
        } else {
            uint64_t val = raw_csc_targets[i];
            std::memcpy(&csc_tgt_bytes[i * 8], &val, 8);
        }
    }

    impulse_vm_context_mock_csr_typed(
        ctx,
        0,
        csr_off_bytes.data(),
        csr_tgt_bytes.data(),
        4,
        6,
        dst_width,
        edge_idx_width
    );

    impulse_vm_context_mock_csc_typed(
        ctx,
        0,
        csc_off_bytes.data(),
        csc_tgt_bytes.data(),
        src_width,
        edge_idx_width
    );

    // Test 1: OP_CSR_DEGREE for node 0 (expected: 2) and node 3 (expected: 0)
    {
        std::vector<impulse_instruction_t> code = {
            { OP_LOAD_CONST_INT, 0, 1, 0 },         // R1 = 0
            { OP_CSR_DEGREE, 0, 0, (0 << 16) | 1 }, // R0 = DEGREE(R1, rel=0)
            { OP_LOAD_CONST_INT, 0, 2, 3 },         // R2 = 3
            { OP_CSR_DEGREE, 0, 3, (0 << 16) | 2 }, // R3 = DEGREE(R2, rel=0)
            { OP_HALT, 0, 0, 0 }
        };

        impulse_vm_state_t vm_state{};
        vm_state.query_context = ctx;

        impulse_vm_status_t status = impulse_vm_execute(code.data(), code.size(), &vm_state, 0);
        assert(status == IMPULSE_VM_OK);
        assert(vm_state.registers[0] == 2);
        assert(vm_state.registers[3] == 0);
    }

    // Test 2: OP_CSR_WALK from scalar node 0 -> expected bitset {1, 2}
    {
        std::vector<impulse_instruction_t> code = {
            { OP_LOAD_CONST_INT, 0, 1, 0 },       // R1 = 0
            { OP_CSR_WALK, 0, 0, (0 << 16) | 1 }, // R0 = CSR_WALK(R1, rel=0)
            { OP_HALT, 0, 0, 0 }
        };

        impulse_vm_state_t vm_state{};
        vm_state.query_context = ctx;

        impulse_vm_status_t status = impulse_vm_execute(code.data(), code.size(), &vm_state, 0);
        assert(status == IMPULSE_VM_OK);

        size_t h_dst = vm_state.registers[0];
        assert(impulse_vm_context_bitset_test(ctx, h_dst, 1) == true);
        assert(impulse_vm_context_bitset_test(ctx, h_dst, 2) == true);
        assert(impulse_vm_context_bitset_test(ctx, h_dst, 0) == false);
        assert(impulse_vm_context_bitset_test(ctx, h_dst, 3) == false);
    }

    // Test 3: OP_CSC_WALK from scalar target 2 -> expected bitset {0, 1}
    {
        std::vector<impulse_instruction_t> code = {
            { OP_LOAD_CONST_INT, 0, 1, 2 },       // R1 = 2
            { OP_CSC_WALK, 0, 0, (0 << 16) | 1 }, // R0 = CSC_WALK(R1, rel=0)
            { OP_HALT, 0, 0, 0 }
        };

        impulse_vm_state_t vm_state{};
        vm_state.query_context = ctx;

        impulse_vm_status_t status = impulse_vm_execute(code.data(), code.size(), &vm_state, 0);
        assert(status == IMPULSE_VM_OK);

        size_t h_dst = vm_state.registers[0];
        assert(impulse_vm_context_bitset_test(ctx, h_dst, 0) == true);
        assert(impulse_vm_context_bitset_test(ctx, h_dst, 1) == true);
        assert(impulse_vm_context_bitset_test(ctx, h_dst, 2) == false);
        assert(impulse_vm_context_bitset_test(ctx, h_dst, 3) == false);
    }

    impulse_vm_context_destroy(ctx);
    std::cout << "  -> PASSED!" << std::endl;
}

int main() {
    std::cout << "=== ImpulseVM 3x3 Variable Node ID & Edge Index Matrix Tests ===" << std::endl;

    uint8_t widths[] = {2, 4, 8}; // 16-bit, 32-bit, 64-bit

    for (uint8_t src_w : widths) {
        for (uint8_t dst_w : widths) {
            // Test with 32-bit edge offsets
            run_matrix_test(src_w, dst_w, 4);
            // Test with 64-bit edge offsets
            run_matrix_test(src_w, dst_w, 8);
        }
    }

    std::cout << "=== ALL 18 CONFIGURATIONS (9 matrix x 2 edge index widths) PASSED ===" << std::endl;
    return 0;
}
