/**
 * @file dense_walk_common.hpp
 * @brief Shared force-inline helper for OP_DENSE_WALK (0x8E), OP_DENSE_WALK_BITMATRIX (0x8F),
 *        and OP_DENSE_WALK_DIRECT_STORE (0x95).
 */
#ifndef IMPULSE_VM_DENSE_WALK_COMMON_HPP
#define IMPULSE_VM_DENSE_WALK_COMMON_HPP

#include "../../impulse_vm_internal.hpp"
#include "impulse_opcode_macros.h"

IMPULSE_VM_FORCE_INLINE impulse_vm_status_t execute_dense_walk_common(
    const impulse_instruction_t* bytecode,
    impulse_vm_state_t* vm_state,
    uint32_t allowed_src_types = (IMP_TYPES_INT_OR_NODE | IMP_TYPE_BIT(TYPE_BITSET_HANDLE))
) {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t dst = IMP_INST_DST(inst);
    uint16_t src = IMP_INST_U8_0(inst);
    uint16_t rel = IMP_INST_HIGH16(inst);
    IMP_VALIDATE_GPR(dst);
    IMP_VALIDATE_GPR(src);
    IMP_REQUIRE_CONTEXT(vm_state);
    IMPULSE_ASSERT(vm_state->query_context != nullptr);
    IMPULSE_CHECK_REG_TYPE(vm_state, src, allowed_src_types);

    uint8_t src_type = vm_state->register_types[src];
    uint64_t src_val = vm_state->registers[src];
    bool src_is_bitset = (src_type == TYPE_BITSET_HANDLE);
    IMPULSE_TESTCASE(dst == src);
    IMPULSE_TESTCASE(src_is_bitset);
    IMPULSE_TESTCASE(src_type == TYPE_NODE_VECTOR || src_type == TYPE_UINT64_VECTOR);

    int h_dst = -1;
    if (vm_state->register_types[dst] == TYPE_BITSET_HANDLE && !(dst == src && src_is_bitset)) {
        h_dst = static_cast<int>(vm_state->registers[dst]);
    } else {
        h_dst = acquire_bitset(vm_state->query_context);
        if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
    IMPULSE_ASSERT(h_dst >= 0 && static_cast<size_t>(h_dst) < VM_MAX_BITSET_HANDLES);
    auto& bs_dst = vm_state->query_context->bitsets[h_dst];
    bs_dst.clear();

    IMPULSE_TESTCASE(rel >= vm_state->query_context->slots.size());
    if (rel < vm_state->query_context->slots.size()) {
        const auto& slot = vm_state->query_context->slots[rel];
        IMPULSE_WELL_FORMED_ASSERT(!slot.offsets_ptr || slot.targets_ptr != nullptr || slot.edge_count == 0);
        if (slot.offsets_ptr && slot.targets_ptr) {
            if (src_type == TYPE_NODE_ID || src_type == TYPE_INT64) {
                uint64_t u = src_val;
                IMPULSE_TESTCASE(slot.node_count == 0);
                if (u < slot.node_count || slot.node_count == 0) {
                    uint64_t start = slot.get_csr_offset(u);
                    uint64_t end = slot.get_csr_offset(u + 1);
                    IMPULSE_TESTCASE(start == end);
                    for (uint64_t e = start; e < end; ++e) {
                        bs_dst.set(slot.get_csr_target(e));
                    }
                }
            } else if (src_is_bitset) {
                int h_src = static_cast<int>(src_val);
                IMPULSE_ASSERT(h_src >= 0 && static_cast<size_t>(h_src) < VM_MAX_BITSET_HANDLES);
                const auto& bs_src = vm_state->query_context->bitsets[h_src];
                size_t N = (slot.node_count > 0) ? slot.node_count : vm_state->query_context->max_nodes;
                IMPULSE_TESTCASE(N == 0);
                #pragma omp parallel for schedule(static)
                for (size_t u = 0; u < N; ++u) {
                    if (bs_src.test(u)) {
                        uint64_t start = slot.get_csr_offset(u);
                        uint64_t end = slot.get_csr_offset(u + 1);
                        for (uint64_t e = start; e < end; ++e) {
                            uint64_t v = slot.get_csr_target(e);
                            if (v < vm_state->query_context->max_nodes) {
                                #pragma omp atomic
                                bs_dst.words[IMP_WORD_INDEX(v)] |= IMP_BIT_MASK64(v);
                            }
                        }
                    }
                }
                if (dst == src) release_bitset(vm_state->query_context, h_src);
            } else if (src_type == TYPE_NODE_VECTOR || src_type == TYPE_UINT64_VECTOR) {
                const uint64_t* vec_ptr = nullptr;
                size_t vec_len = 0;
                if (get_node_vector_slice(vm_state->query_context, src_val, &vec_ptr, &vec_len) && vec_ptr) {
                    IMPULSE_TESTCASE(vec_len == 0);
                    for (size_t i = 0; i < vec_len; ++i) {
                        uint64_t u = vec_ptr[i];
                        if (u < slot.node_count || slot.node_count == 0) {
                            uint64_t start = slot.get_csr_offset(u);
                            uint64_t end = slot.get_csr_offset(u + 1);
                            for (uint64_t e = start; e < end; ++e) {
                                bs_dst.set(slot.get_csr_target(e));
                            }
                        }
                    }
                }
            }
        }
    }
    vm_state->registers[dst] = static_cast<uint64_t>(h_dst);
    vm_state->register_types[dst] = TYPE_BITSET_HANDLE;

    bool is_empty = true;
    for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
        if (bs_dst.words[i] != 0ULL) { is_empty = false; break; }
    }
    IMP_VM_SET_ZF(vm_state, is_empty);
    IMPULSE_ASSERT(vm_state->register_types[dst] == TYPE_BITSET_HANDLE);

    return IMPULSE_VM_OK;
}

#endif // IMPULSE_VM_DENSE_WALK_COMMON_HPP
