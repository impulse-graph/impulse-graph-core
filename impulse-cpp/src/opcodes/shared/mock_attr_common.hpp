/**
 * @file mock_attr_common.hpp
 * @brief Shared force-inline helper for OP_INIT_MOCK_NODE_ATTR (0x0B) and OP_INIT_MOCK_EDGE_ATTR (0x0C).
 */
#ifndef IMPULSE_VM_MOCK_ATTR_COMMON_HPP
#define IMPULSE_VM_MOCK_ATTR_COMMON_HPP

#include "../../impulse_vm_internal.hpp"

template <bool IsNodeAttr>
IMPULSE_VM_FORCE_INLINE impulse_vm_status_t execute_init_mock_attr(
    const impulse_instruction_t* bytecode,
    impulse_vm_state_t* vm_state
) {
    const auto& inst = bytecode[vm_state->pc];
    uint16_t attr_id = inst.dst_reg;
    uint16_t src_reg = inst.payload & 0xFF;
    uint16_t mask_reg = (inst.payload >> 8) & 0xFF;
    VALIDATE_REG(src_reg);
    VALIDATE_REG(mask_reg);

    if (!vm_state->query_context) return IMPULSE_VM_ERR_NULL_SNAPSHOT;

    IMPULSE_CHECK_REG_TYPE(vm_state, src_reg, IMP_TYPES_NUMERIC_VEC);
    if (mask_reg != 0) {
        IMPULSE_CHECK_REG_TYPE(vm_state, mask_reg, IMP_TYPE_BIT(TYPE_BITSET_HANDLE));
    }

    {
        auto* ma_ptr = IsNodeAttr
            ? upsert_mock_node_attr(vm_state->query_context, attr_id)
            : upsert_mock_edge_attr(vm_state->query_context, attr_id);
        auto& mock_attr = *ma_ptr;
        mock_attr.float_data.clear();
        mock_attr.int_data.clear();
        mock_attr.has_mask = false;

        if (vm_state->register_types[src_reg] == TYPE_FLOAT_VECTOR) {
            int h = static_cast<int>(vm_state->registers[src_reg]);
            if (h >= 0 && static_cast<size_t>(h) < VM_MAX_VECTOR_HANDLES && vm_state->query_context->float_vectors_allocated[h]) {
                const auto& vec = vm_state->query_context->float_vectors[h];
                size_t len = vm_state->query_context->float_vector_lengths[h];
                if (len == 0 || len > vec.size()) len = vec.size();
                if (len == vm_state->query_context->max_nodes && !vm_state->query_context->snapshot) {
                    size_t slot_bound = 0;
                    for (const auto& s : vm_state->query_context->slots) {
                        slot_bound = std::max<size_t>(slot_bound, IsNodeAttr
                            ? std::max(s.node_count, s.csc_node_count)
                            : s.edge_count);
                    }
                    len = (slot_bound > 0) ? std::min(len, slot_bound) : std::min<size_t>(len, VM_MOCK_INIT_CAP);
                }
                len = std::min<size_t>(len, VM_MOCK_INIT_CAP);
                mock_attr.float_data.resize(len, 0.0f);
                mock_attr.int_data.resize(len, 0ULL);
                if (len > 0) {
                    std::memcpy(mock_attr.float_data.data(), vec.data(), len * sizeof(float));
                }
                for (size_t i = 0; i < len; ++i) {
                    uint32_t bits = 0;
                    std::memcpy(&bits, &vec[i], sizeof(uint32_t));
                    mock_attr.int_data[i] = bits;
                }
            }
        } else if (vm_state->register_types[src_reg] == TYPE_NODE_VECTOR || vm_state->register_types[src_reg] == TYPE_UINT64_VECTOR) {
            int h = static_cast<int>(vm_state->registers[src_reg]);
            if (h >= 0 && static_cast<size_t>(h) < VM_MAX_VECTOR_HANDLES && vm_state->query_context->node_vectors_allocated[h]) {
                const auto& vec = vm_state->query_context->node_vectors[h];
                size_t len = vm_state->query_context->node_vector_lengths[h];
                if (len == 0 || len > vec.size()) len = vec.size();
                len = std::min<size_t>(len, VM_MOCK_INIT_CAP);
                mock_attr.int_data.resize(len, 0ULL);
                if (len > 0) {
                    std::memcpy(mock_attr.int_data.data(), vec.data(), len * sizeof(uint64_t));
                }
                if (vm_state->query_context->float_vectors_allocated[h]) {
                    const auto& fvec = vm_state->query_context->float_vectors[h];
                    size_t flen = std::min<size_t>(len, fvec.size());
                    mock_attr.float_data.resize(flen, 0.0f);
                    if (flen > 0) {
                        std::memcpy(mock_attr.float_data.data(), fvec.data(), flen * sizeof(float));
                    }
                }
            }
        }
        if (mask_reg < 64 && vm_state->register_types[mask_reg] == TYPE_BITSET_HANDLE) {
            int h_mask = static_cast<int>(vm_state->registers[mask_reg]);
            if (h_mask >= 0 && static_cast<size_t>(h_mask) < VM_MAX_BITSET_HANDLES && vm_state->query_context->bitset_allocated[h_mask]) {
                mock_attr.has_mask = true;
                mock_attr.mask = vm_state->query_context->bitsets[h_mask];
            }
        }
    }

    return IMPULSE_VM_OK;
}

#endif // IMPULSE_VM_MOCK_ATTR_COMMON_HPP
