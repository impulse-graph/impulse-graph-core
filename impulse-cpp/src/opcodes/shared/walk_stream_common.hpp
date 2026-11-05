/**
 * @file walk_stream_common.hpp
 * @brief Shared force-inline Edge Stream Shader execution engine for
 *        OP_COO_WALK_STREAM (0xA0), OP_CSR_WALK_STREAM (0xA9), and OP_CSC_WALK_STREAM (0xAA).
 */
#ifndef IMPULSE_VM_WALK_STREAM_COMMON_HPP
#define IMPULSE_VM_WALK_STREAM_COMMON_HPP

#include "../../impulse_vm_internal.hpp"

template <uint8_t WalkOp>
IMPULSE_VM_FORCE_INLINE impulse_vm_status_t execute_walk_stream_common(
    const impulse_instruction_t* bytecode,
    size_t instruction_count,
    impulse_vm_state_t* vm_state
) {
    const auto& inst = bytecode[vm_state->pc];
    uint8_t walk_op = inst.opcode;
    uint16_t dst = inst.dst_reg;
    uint16_t src = inst.payload & 0xFFFF;
    uint16_t rel = (inst.payload >> 16) & 0xFFFF;
    int shaderPcStart = inst.flags & 0xFF;
    VALIDATE_REG(dst);
    VALIDATE_REG(src);
    if (!vm_state->query_context) return IMPULSE_VM_ERR_NULL_SNAPSHOT;
    IMPULSE_CHECK_REG_TYPE(vm_state, src, IMP_TYPES_SOURCE_SET | IMP_TYPE_BIT(TYPE_LANE_FRONTIER));

    if (rel >= vm_state->query_context->slots.size()) {
        return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
    }
    const auto& slot = vm_state->query_context->slots[rel];

    bool src_is_lane = (vm_state->register_types[src] == TYPE_LANE_FRONTIER);

    auto& active_sources = vm_state->query_context->scratch_u32_a;
    active_sources.clear();
    if (src_is_lane) {
        size_t h_src = static_cast<size_t>(vm_state->registers[src]);
        if (h_src >= VM_MAX_LANE_FRONTIER_HANDLES || h_src >= vm_state->query_context->lane_frontiers_count) {
            return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        }
        const auto& lf_src = vm_state->query_context->lane_frontiers[h_src];
        for (size_t w = 0; w < lf_src.active_words.size(); ++w) {
            uint64_t word = lf_src.active_words[w];
            while (word) {
                int bit = std::countr_zero(word);
                uint64_t u = (w << 6) + bit;
                word &= word - 1;
                active_sources.push_back(static_cast<uint32_t>(u));
            }
        }
    } else if (vm_state->register_types[src] == TYPE_NODE_ID || vm_state->register_types[src] == TYPE_INT64) {
        active_sources.push_back(static_cast<uint32_t>(vm_state->registers[src]));
    } else if (vm_state->register_types[src] == TYPE_NODE_VECTOR || vm_state->register_types[src] == TYPE_UINT64_VECTOR) {
        const uint64_t* ptr = nullptr;
        size_t len = 0;
        if (get_node_vector_slice(vm_state->query_context, vm_state->registers[src], &ptr, &len) && ptr) {
            for (size_t i = 0; i < len; ++i) {
                active_sources.push_back(static_cast<uint32_t>(ptr[i]));
            }
        }
    } else if (vm_state->register_types[src] == TYPE_BITSET_HANDLE) {
        int h_src_bs = static_cast<int>(vm_state->registers[src]);
        const auto& bs_src = vm_state->query_context->bitsets[h_src_bs];
        for (size_t w = 0; w < vm_state->query_context->words_per_bitset; ++w) {
            uint64_t word = bs_src.words[w];
            while (word) {
                int bit = std::countr_zero(word);
                uint64_t u = w * 64 + bit;
                word &= word - 1;
                active_sources.push_back(static_cast<uint32_t>(u));
            }
        }
    } else {
        return IMPULSE_VM_ERR_INVALID_REGISTER;
    }

    int h_dst_bs = -1;
    int h_dst_lf = -1;
    if (src_is_lane) {
        size_t h_src = static_cast<size_t>(vm_state->registers[src]);
        const auto& lf_src = vm_state->query_context->lane_frontiers[h_src];
        size_t n_nodes = resolve_multilane_node_count(vm_state->query_context, rel, lf_src.lanes.size());
        h_dst_lf = acquire_lane_frontier(vm_state->query_context, n_nodes, lf_src.num_lanes);
        if (h_dst_lf < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        auto& lf_dst = vm_state->query_context->lane_frontiers[h_dst_lf];
        lf_dst.edge_trip_active_mask = lf_src.edge_trip_active_mask;
        if (lf_src.edge_trip_active_mask != 0) {
            for (size_t r = 0; r < VM_MAX_REL_SLOTS; ++r) {
                if (lf_src.edge_trip_active_mask & (1u << r)) {
                    const auto& s_trip = lf_src.edge_trip_masks[r];
                    auto& d_trip = lf_dst.edge_trip_masks[r];
                    d_trip.clear();
                    d_trip.resize(s_trip.size(), 0ULL);
                    if (!s_trip.empty()) {
                        std::memcpy(d_trip.data(), s_trip.data(), s_trip.size() * sizeof(uint64_t));
                    }
                }
            }
        }
        vm_state->registers[dst] = static_cast<uint64_t>(h_dst_lf);
        vm_state->register_types[dst] = TYPE_LANE_FRONTIER;
    } else {
        if (vm_state->register_types[dst] == TYPE_BITSET_HANDLE) {
            h_dst_bs = static_cast<int>(vm_state->registers[dst]);
            vm_state->query_context->bitsets[h_dst_bs].clear();
        } else {
            h_dst_bs = acquire_bitset(vm_state->query_context);
            if (h_dst_bs < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
            vm_state->registers[dst] = static_cast<uint64_t>(h_dst_bs);
            vm_state->register_types[dst] = TYPE_BITSET_HANDLE;
        }
    }

    size_t h_src_lf = src_is_lane ? static_cast<size_t>(vm_state->registers[src]) : 0;
    const auto* lf_src_ptr = src_is_lane ? &vm_state->query_context->lane_frontiers[h_src_lf] : nullptr;
    auto* lf_dst_ptr = src_is_lane ? &vm_state->query_context->lane_frontiers[h_dst_lf] : nullptr;
    auto* bs_dst_ptr = !src_is_lane ? &vm_state->query_context->bitsets[h_dst_bs] : nullptr;
    const std::vector<uint64_t>* trip_ptr = (src_is_lane && lf_dst_ptr && rel < VM_MAX_REL_SLOTS && (lf_dst_ptr->edge_trip_active_mask & (1u << rel)) != 0)
        ? &lf_dst_ptr->edge_trip_masks[rel] : nullptr;

    bool isCsc = (walk_op == OP_CSC_WALK_STREAM);
    uint64_t walk_node_bound = isCsc
        ? ((slot.csc_node_count > 0) ? slot.csc_node_count : slot.node_count)
        : slot.node_count;
    uint64_t target_node_bound = std::max(slot.node_count, slot.csc_node_count);
    auto& reduced_reg_masks = vm_state->query_context->scratch_u64_a;
    reduced_reg_masks.clear();
    size_t mask_cap = std::min<size_t>(static_cast<size_t>(target_node_bound) + 1024, reduced_reg_masks.capacity());
    reduced_reg_masks.resize(mask_cap, 0ULL);
    for (uint32_t u : active_sources) {
        if (u >= walk_node_bound) continue;
        uint64_t u_lane_mask = src_is_lane ? (lf_src_ptr->lanes[u] & lf_src_ptr->active_mask) : 0ULL;
        if (src_is_lane && u_lane_mask == 0ULL) continue;

        uint64_t start = isCsc ? slot.get_csc_offset(u) : slot.get_csr_offset(u);
        uint64_t end = isCsc ? slot.get_csc_offset(u + 1) : slot.get_csr_offset(u + 1);

        for (uint64_t eIdx = start; eIdx < end; ++eIdx) {
            uint32_t neighbor = isCsc ? static_cast<uint32_t>(slot.get_csc_target(eIdx)) : static_cast<uint32_t>(slot.get_csr_target(eIdx));
            uint32_t actualSource = isCsc ? neighbor : u;
            uint32_t actualTarget = isCsc ? u : neighbor;

            uint64_t trip = (trip_ptr && eIdx < trip_ptr->size()) ? (*trip_ptr)[eIdx] : 0ULL;
            uint64_t prop_mask = u_lane_mask & ~trip;
            if (src_is_lane && prop_mask == 0ULL) continue;

            float s_regs[16] = {0.0f};
            bool abort = false;
            size_t mutPc = shaderPcStart;

            while (mutPc < instruction_count) {
                const auto& sInst = bytecode[mutPc];
                uint8_t op = sInst.opcode;
                if (op == OP_STREAM_FUNC_END) break;

                uint16_t sDst = sInst.dst_reg;
                if (sDst >= 16) return IMPULSE_VM_ERR_INVALID_REGISTER;
                uint16_t sPayloadLow = sInst.payload & 0xFFFF;
                uint16_t sPayloadHigh = (sInst.payload >> 16) & 0xFFFF;

                if ((op >= OP_STREAM_MATH_ADD && op <= OP_STREAM_MATH_DIV) ||
                    (op >= OP_STREAM_MATH_SUB && op <= OP_STREAM_MATH_MOD) ||
                    (op >= OP_STREAM_CMP_EQ && op <= OP_STREAM_LOGIC_OR) ||
                    op == OP_STREAM_SELECT) {
                    if (sPayloadLow >= 16 || sPayloadHigh >= 16) return IMPULSE_VM_ERR_INVALID_REGISTER;
                } else if (op == OP_STREAM_MATH_UNARY || op == OP_STREAM_LOGIC_NOT) {
                    if (sPayloadLow >= 16) return IMPULSE_VM_ERR_INVALID_REGISTER;
                }

                switch (op) {

#include "../op_a1_stream_func_begin.inc"
#include "../op_a2_stream_func_end.inc"
#include "../op_a3_stream_load_src.inc"
#include "../op_a4_stream_load_edge.inc"
#include "../op_a5_stream_math_add.inc"
#include "../op_a6_stream_math_div.inc"
#include "../op_a7_stream_filter.inc"
#include "../op_a8_stream_reduce.inc"
#include "../op_ab_stream_load_tgt.inc"
#include "../op_ac_stream_math_sub.inc"
#include "../op_ad_stream_math_mul.inc"
#include "../op_ae_stream_math_mod.inc"
#include "../op_af_stream_math_unary.inc"
#include "../op_b0_stream_cmp_eq.inc"
#include "../op_b1_stream_cmp_neq.inc"
#include "../op_b2_stream_cmp_gt.inc"
#include "../op_b3_stream_cmp_lt.inc"
#include "../op_b4_stream_logic_and.inc"
#include "../op_b5_stream_logic_or.inc"
#include "../op_b6_stream_logic_not.inc"
#include "../op_b7_stream_select.inc"
#include "../op_b8_stream_reduce_argmin.inc"
#include "../op_b9_stream_reduce_argmax.inc"
#include "../op_ba_stream_load_src_id.inc"
#include "../op_bb_stream_load_tgt_id.inc"
#include "../op_bc_stream_load_edge_id.inc"
#include "../op_bd_stream_load_const.inc"
#include "../op_be_stream_yield.inc"
#include "../op_bf_stream_scatter_reduce.inc"
                }
                if (abort) break;
                mutPc++;
            }
            if (!abort) {
                if (src_is_lane) {
                    if (actualTarget < lf_dst_ptr->lanes.size()) {
                        lf_dst_ptr->lanes[actualTarget] |= prop_mask;
                        lf_dst_ptr->active_words[actualTarget >> 6] |= (1ULL << (actualTarget & 63));
                    }
                } else {
                    bitset_add(*bs_dst_ptr, actualTarget, vm_state->query_context->max_nodes);
                }
            }
        }
    }

    bool is_empty = true;
    if (src_is_lane) {
        for (size_t i = 0; i < lf_dst_ptr->active_words.size(); ++i) {
            if (lf_dst_ptr->active_words[i] != 0ULL) { is_empty = false; break; }
        }
    } else {
        for (size_t i = 0; i < vm_state->query_context->words_per_bitset; ++i) {
            if (bs_dst_ptr->words[i] != 0) { is_empty = false; break; }
        }
    }
    if (is_empty) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
    else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;

    size_t next_pc = vm_state->pc + 1;
    if (next_pc < instruction_count && bytecode[next_pc].opcode == OP_STREAM_FUNC_BEGIN) {
        while (next_pc < instruction_count && bytecode[next_pc].opcode != OP_STREAM_FUNC_END) {
            next_pc++;
        }
        if (next_pc < instruction_count) {
            next_pc++;
        }
    }
    vm_state->pc = next_pc;

    return IMPULSE_VM_OK;
}

#endif // IMPULSE_VM_WALK_STREAM_COMMON_HPP
