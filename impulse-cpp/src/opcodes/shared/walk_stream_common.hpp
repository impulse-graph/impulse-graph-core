/**
 * @file walk_stream_common.hpp
 * @brief Shared force-inline Edge Stream Shader execution engine for
 *        OP_COO_WALK_STREAM (0xA0), OP_CSR_WALK_STREAM (0xA9), and OP_CSC_WALK_STREAM (0xAA).
 */
#ifndef IMPULSE_VM_WALK_STREAM_COMMON_HPP
#define IMPULSE_VM_WALK_STREAM_COMMON_HPP

#include "../../impulse_vm_internal.hpp"
#include "impulse_opcode_macros.h"
#include "impulse_math_ops.h"

#define IMP_STREAM_REDUCED_MASK_SLACK   1024u

template <uint8_t WalkOp>
IMPULSE_VM_FORCE_INLINE impulse_vm_status_t execute_walk_stream_common(
    const impulse_instruction_t* bytecode,
    size_t instruction_count,
    impulse_vm_state_t* vm_state
) {
    const auto& inst = bytecode[vm_state->pc];
    uint8_t walk_op = IMP_INST_OPCODE(inst);
    uint16_t dst = IMP_INST_DST(inst);
    uint16_t src = IMP_INST_LOW16(inst);
    uint16_t rel = IMP_INST_HIGH16(inst);
    size_t shaderPcStart = static_cast<size_t>(IMP_INST_FLAGS(inst));
    IMP_VALIDATE_GPR(dst);
    IMP_VALIDATE_GPR(src);
    IMP_REQUIRE_CONTEXT(vm_state);
    IMPULSE_ASSERT(vm_state->query_context != nullptr);
    IMPULSE_CHECK_REG_TYPE(vm_state, src, IMP_TYPES_SOURCE_SET | IMP_TYPE_BIT(TYPE_LANE_FRONTIER));

    IMP_REQUIRE_REL_SLOT(vm_state, rel);
    const auto& slot = vm_state->query_context->slots[rel];

    bool src_is_lane = (vm_state->register_types[src] == TYPE_LANE_FRONTIER);
    IMPULSE_TESTCASE(src_is_lane);

    auto& active_sources = vm_state->query_context->scratch_u32_a;
    active_sources.clear();
    if (src_is_lane) {
        size_t h_src = static_cast<size_t>(vm_state->registers[src]);
        IMPULSE_TESTCASE(h_src >= VM_MAX_LANE_FRONTIER_HANDLES || h_src >= vm_state->query_context->lane_frontiers_count);
        if (h_src >= VM_MAX_LANE_FRONTIER_HANDLES || h_src >= vm_state->query_context->lane_frontiers_count) {
            return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        }
        const auto& lf_src = vm_state->query_context->lane_frontiers[h_src];
        for (size_t w = 0; w < lf_src.active_words.size(); ++w) {
            uint64_t word = lf_src.active_words[w];
            while (word != 0ULL) {
                uint32_t bit = static_cast<uint32_t>(std::countr_zero(word));
                uint64_t u = IMP_WORD_BIT_TO_NODE(w, bit);
                word = IMP_CLEAR_LOWEST_BIT(word);
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
        IMPULSE_ASSERT(h_src_bs >= 0 && static_cast<size_t>(h_src_bs) < VM_MAX_BITSET_HANDLES);
        const auto& bs_src = vm_state->query_context->bitsets[h_src_bs];
        for (size_t w = 0; w < vm_state->query_context->words_per_bitset; ++w) {
            uint64_t word = bs_src.words[w];
            while (word != 0ULL) {
                uint32_t bit = static_cast<uint32_t>(std::countr_zero(word));
                uint64_t u = IMP_WORD_BIT_TO_NODE(w, bit);
                word = IMP_CLEAR_LOWEST_BIT(word);
                active_sources.push_back(static_cast<uint32_t>(u));
            }
        }
    } else {
        return IMPULSE_VM_ERR_INVALID_REGISTER;
    }
    IMPULSE_TESTCASE(active_sources.empty());

    int h_dst_bs = -1;
    int h_dst_lf = -1;
    if (src_is_lane) {
        size_t h_src = static_cast<size_t>(vm_state->registers[src]);
        const auto& lf_src = vm_state->query_context->lane_frontiers[h_src];
        size_t n_nodes = resolve_multilane_node_count(vm_state->query_context, rel, lf_src.lanes.size());
        h_dst_lf = acquire_lane_frontier(vm_state->query_context, n_nodes, lf_src.num_lanes);
        if (h_dst_lf < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        IMPULSE_ASSERT(static_cast<size_t>(h_dst_lf) < VM_MAX_LANE_FRONTIER_HANDLES);
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
            IMPULSE_ASSERT(h_dst_bs >= 0 && static_cast<size_t>(h_dst_bs) < VM_MAX_BITSET_HANDLES);
            vm_state->query_context->bitsets[h_dst_bs].clear();
        } else {
            h_dst_bs = acquire_bitset(vm_state->query_context);
            if (h_dst_bs < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
            IMPULSE_ASSERT(static_cast<size_t>(h_dst_bs) < VM_MAX_BITSET_HANDLES);
            vm_state->registers[dst] = static_cast<uint64_t>(h_dst_bs);
            vm_state->register_types[dst] = TYPE_BITSET_HANDLE;
        }
    }

    size_t h_src_lf = src_is_lane ? static_cast<size_t>(vm_state->registers[src]) : 0;
    const auto* lf_src_ptr = src_is_lane ? &vm_state->query_context->lane_frontiers[h_src_lf] : nullptr;
    auto* lf_dst_ptr = src_is_lane ? &vm_state->query_context->lane_frontiers[h_dst_lf] : nullptr;
    auto* bs_dst_ptr = !src_is_lane ? &vm_state->query_context->bitsets[h_dst_bs] : nullptr;
    IMPULSE_ASSERT(src_is_lane ? (lf_src_ptr != nullptr && lf_dst_ptr != nullptr) : (bs_dst_ptr != nullptr));
    const std::vector<uint64_t>* trip_ptr = (src_is_lane && lf_dst_ptr && rel < VM_MAX_REL_SLOTS && (lf_dst_ptr->edge_trip_active_mask & (1u << rel)) != 0)
        ? &lf_dst_ptr->edge_trip_masks[rel] : nullptr;
    IMPULSE_TESTCASE(trip_ptr != nullptr);

    bool isCsc = (walk_op == OP_CSC_WALK_STREAM);
    IMPULSE_TESTCASE(isCsc);
    uint64_t walk_node_bound = isCsc
        ? ((slot.csc_node_count > 0) ? slot.csc_node_count : slot.node_count)
        : slot.node_count;
    uint64_t target_node_bound = std::max(slot.node_count, slot.csc_node_count);
    auto& reduced_reg_masks = vm_state->query_context->scratch_u64_a;
    reduced_reg_masks.clear();
    size_t mask_cap = std::min<size_t>(static_cast<size_t>(target_node_bound) + IMP_STREAM_REDUCED_MASK_SLACK, reduced_reg_masks.capacity());
    reduced_reg_masks.resize(mask_cap, 0ULL);
    for (uint32_t u : active_sources) {
        if (u >= walk_node_bound) continue;
        uint64_t u_lane_mask = src_is_lane ? (lf_src_ptr->lanes[u] & lf_src_ptr->active_mask) : 0ULL;
        if (src_is_lane && u_lane_mask == 0ULL) continue;

        uint64_t start = isCsc ? slot.get_csc_offset(u) : slot.get_csr_offset(u);
        uint64_t end = isCsc ? slot.get_csc_offset(u + 1) : slot.get_csr_offset(u + 1);
        IMPULSE_TESTCASE(start == end);

        for (uint64_t eIdx = start; eIdx < end; ++eIdx) {
            uint32_t neighbor = isCsc ? static_cast<uint32_t>(slot.get_csc_target(eIdx)) : static_cast<uint32_t>(slot.get_csr_target(eIdx));
            uint32_t actualSource = isCsc ? neighbor : u;
            uint32_t actualTarget = isCsc ? u : neighbor;

            uint64_t trip = (trip_ptr && eIdx < trip_ptr->size()) ? (*trip_ptr)[eIdx] : 0ULL;
            uint64_t prop_mask = u_lane_mask & ~trip;
            if (src_is_lane && prop_mask == 0ULL) continue;

            float s_regs[IMP_VM_STREAM_REG_COUNT] = {0.0f};
            bool abort = false;
            size_t mutPc = shaderPcStart;

            while (mutPc < instruction_count) {
                const auto& sInst = bytecode[mutPc];
                uint8_t op = IMP_INST_OPCODE(sInst);
                if (op == OP_STREAM_FUNC_END) break;

                uint16_t sDst = IMP_INST_DST(sInst);
                IMP_VALIDATE_STREAM_REG(sDst);
                uint16_t sPayloadLow = IMP_INST_LOW16(sInst);
                uint16_t sPayloadHigh = IMP_INST_HIGH16(sInst);

                if ((op >= OP_STREAM_MATH_ADD && op <= OP_STREAM_MATH_DIV) ||
                    (op >= OP_STREAM_MATH_SUB && op <= OP_STREAM_MATH_MOD) ||
                    (op >= OP_STREAM_CMP_EQ && op <= OP_STREAM_LOGIC_OR) ||
                    op == OP_STREAM_SELECT) {
                    IMP_VALIDATE_STREAM_REG(sPayloadLow);
                    IMP_VALIDATE_STREAM_REG(sPayloadHigh);
                } else if (op == OP_STREAM_MATH_UNARY || op == OP_STREAM_LOGIC_NOT) {
                    IMP_VALIDATE_STREAM_REG(sPayloadLow);
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
            IMPULSE_TESTCASE(abort);
            if (!abort) {
                if (src_is_lane) {
                    if (actualTarget < lf_dst_ptr->lanes.size()) {
                        lf_dst_ptr->lanes[actualTarget] |= prop_mask;
                        lf_dst_ptr->active_words[IMP_WORD_INDEX(actualTarget)] |= IMP_BIT_MASK64(actualTarget);
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
            if (bs_dst_ptr->words[i] != 0ULL) { is_empty = false; break; }
        }
    }
    IMP_VM_SET_ZF(vm_state, is_empty);

    size_t next_pc = vm_state->pc + 1;
    const bool has_inline_shader_block = (next_pc < instruction_count &&
                                          IMP_INST_OPCODE(bytecode[next_pc]) == OP_STREAM_FUNC_BEGIN);
    IMPULSE_TESTCASE(has_inline_shader_block);
    if (has_inline_shader_block) {
        while (next_pc < instruction_count && IMP_INST_OPCODE(bytecode[next_pc]) != OP_STREAM_FUNC_END) {
            next_pc++;
        }
        if (next_pc < instruction_count) {
            next_pc++;
        }
    }
    vm_state->pc = next_pc;
    IMPULSE_ASSERT(vm_state->pc <= instruction_count);

    return IMPULSE_VM_OK;
}

#endif // IMPULSE_VM_WALK_STREAM_COMMON_HPP
