/**
 * @file lane_walk_common.hpp
 * @brief Shared force-inline bit-sliced traversal kernel for OP_LANE_CSR_WALK (0xC3)
 *        and OP_LANE_CSC_WALK (0xC4).
 */
#ifndef IMPULSE_VM_LANE_WALK_COMMON_HPP
#define IMPULSE_VM_LANE_WALK_COMMON_HPP

#include "../../impulse_vm_internal.hpp"

template <bool IsCsc>
IMPULSE_VM_FORCE_INLINE impulse_vm_status_t execute_lane_walk_common(
    const impulse_instruction_t* bytecode,
    impulse_vm_state_t* vm_state
) {
    {
        const auto& inst = bytecode[vm_state->pc];
        uint16_t dst = inst.dst_reg;
        uint16_t src_reg = inst.payload & 0xFFFF;
        uint16_t rel_id = (inst.payload >> 16) & 0xFFFF;
        uint8_t flags = inst.flags;
        VALIDATE_REG(dst);
        VALIDATE_REG(src_reg);
        if (!vm_state->query_context) return IMPULSE_VM_ERR_NULL_SNAPSHOT;

        if (vm_state->register_types[src_reg] != TYPE_LANE_FRONTIER) {
            return IMPULSE_VM_ERR_INVALID_REGISTER;
        }
        size_t h_src = static_cast<size_t>(vm_state->registers[src_reg]);
        if (h_src >= VM_MAX_LANE_FRONTIER_HANDLES || h_src >= vm_state->query_context->lane_frontiers_count) {
            return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        }
        if (rel_id >= vm_state->query_context->slots.size()) {
            return IMPULSE_VM_ERR_OUT_OF_BOUNDS;
        }

        const auto& lf_src = vm_state->query_context->lane_frontiers[h_src];
        size_t n_nodes = resolve_multilane_node_count(vm_state->query_context, rel_id, lf_src.lanes.size());
        int h_dst = acquire_lane_frontier(vm_state->query_context, n_nodes, lf_src.num_lanes);
        if (h_dst < 0) return IMPULSE_VM_ERR_OUT_OF_BOUNDS;

        auto& lf_dst = vm_state->query_context->lane_frontiers[h_dst];
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

        bool accumulate = (flags & (0x02 | IMPULSE_VM_OP_FLAG_ACCUMULATE)) != 0;
        bool is_fixpoint = (flags & 0x04) != 0;
        constexpr bool is_csc = IsCsc;

        if (accumulate || is_fixpoint) {
            size_t copy_words = std::min(lf_dst.active_words.size(), lf_src.active_words.size());
            for (size_t w_idx = 0; w_idx < copy_words; ++w_idx) {
                uint64_t word = lf_src.active_words[w_idx];
                if (word == 0ULL) continue;
                uint64_t dst_word = 0ULL;
                while (word != 0ULL) {
                    uint32_t bit = static_cast<uint32_t>(std::countr_zero(word));
                    size_t u = (w_idx << 6) + bit;
                    word &= (word - 1ULL);
                    if (u < n_nodes) {
                        uint64_t val = lf_src.lanes[u] & lf_dst.active_mask;
                        if (val != 0ULL) {
                            lf_dst.lanes[u] = val;
                            dst_word |= (1ULL << bit);
                        }
                    }
                }
                lf_dst.active_words[w_idx] = dst_word;
            }
        }

        const auto& slot = vm_state->query_context->slots[rel_id];
        const std::vector<uint64_t>* trip_ptr = (rel_id < VM_MAX_REL_SLOTS && (lf_dst.edge_trip_active_mask & (1u << rel_id)) != 0)
            ? &lf_dst.edge_trip_masks[rel_id]
            : nullptr;

        const bool csr_fast_u32 = (slot.offsets_ptr != nullptr && slot.targets_ptr != nullptr &&
                                   slot.edge_index_width != 8 && slot.edge_index_width != 64 &&
                                   slot.node_id_width != 2 && slot.node_id_width != 16 &&
                                   slot.node_id_width != 8 && slot.node_id_width != 64);
        const uint32_t* csr_offsets_u32 = csr_fast_u32 ? static_cast<const uint32_t*>(slot.offsets_ptr) : nullptr;
        const uint32_t* csr_targets_u32 = csr_fast_u32 ? static_cast<const uint32_t*>(slot.targets_ptr) : nullptr;

        const bool csc_fast_u32 = (!slot.dynamic_csc_offsets.empty() && !slot.dynamic_csc_targets.empty()) ||
                                  (slot.dynamic_csc_offsets.empty() &&
                                   slot.csc_offsets_ptr != nullptr && slot.csc_targets_ptr != nullptr &&
                                   slot.csc_edge_index_width != 8 && slot.csc_edge_index_width != 64 &&
                                   slot.csc_node_id_width != 2 && slot.csc_node_id_width != 16 &&
                                   slot.csc_node_id_width != 8 && slot.csc_node_id_width != 64);
        const uint32_t* csc_offsets_u32 = !slot.dynamic_csc_offsets.empty()
            ? slot.dynamic_csc_offsets.data()
            : (csc_fast_u32 ? static_cast<const uint32_t*>(slot.csc_offsets_ptr) : nullptr);
        const uint32_t* csc_targets_u32 = !slot.dynamic_csc_targets.empty()
            ? slot.dynamic_csc_targets.data()
            : (csc_fast_u32 ? static_cast<const uint32_t*>(slot.csc_targets_ptr) : nullptr);

        auto execute_walk_tiered = [&](auto width_tag) {
            using WordT = decltype(width_tag);
            const WordT active_mask_w = static_cast<WordT>(lf_dst.active_mask);

            auto step_once = [&](const std::vector<uint64_t>& cur_lanes,
                                 const std::vector<uint64_t>& cur_active_words,
                                 std::vector<uint64_t>& next_lanes,
                                 std::vector<uint64_t>& next_active_words) {
                if (!is_csc) {
                    if (slot.offsets_ptr && slot.targets_ptr) {
                        size_t csr_n = std::min<size_t>(slot.node_count > 0 ? slot.node_count : n_nodes, cur_lanes.size());
                        size_t max_words = std::min<size_t>(cur_active_words.size(), (csr_n + 63) / 64);
                        if (csr_fast_u32 && !trip_ptr) {
                            for (size_t w_idx = 0; w_idx < max_words; ++w_idx) {
                                uint64_t word = cur_active_words[w_idx];
                                while (word != 0ULL) {
                                    uint32_t bit = static_cast<uint32_t>(std::countr_zero(word));
                                    size_t u = (w_idx << 6) + bit;
                                    word &= (word - 1ULL);
                                    if (u >= csr_n) break;
                                    WordT u_mask = static_cast<WordT>(cur_lanes[u]) & active_mask_w;
                                    if (u_mask == 0) continue;
                                    uint32_t start = csr_offsets_u32[u];
                                    uint32_t end = csr_offsets_u32[u + 1];
                                    for (uint32_t e = start; e < end; ++e) {
                                        uint32_t v = csr_targets_u32[e];
                                        if (v < next_lanes.size()) {
                                            WordT prev = static_cast<WordT>(next_lanes[v]);
                                            WordT nw = static_cast<WordT>(prev | u_mask);
                                            if (nw != prev) {
                                                next_lanes[v] = static_cast<uint64_t>(nw);
                                                next_active_words[v >> 6] |= (1ULL << (v & 63));
                                            }
                                        }
                                    }
                                }
                            }
                        } else {
                            for (size_t w_idx = 0; w_idx < max_words; ++w_idx) {
                                uint64_t word = cur_active_words[w_idx];
                                while (word != 0ULL) {
                                    uint32_t bit = static_cast<uint32_t>(std::countr_zero(word));
                                    size_t u = (w_idx << 6) + bit;
                                    word &= (word - 1ULL);
                                    if (u >= csr_n) break;
                                    WordT u_mask = static_cast<WordT>(cur_lanes[u]) & active_mask_w;
                                    if (u_mask == 0) continue;
                                    uint64_t start = slot.get_csr_offset(u);
                                    uint64_t end = slot.get_csr_offset(u + 1);
                                    for (uint64_t e = start; e < end; ++e) {
                                        uint64_t v = slot.get_csr_target(e);
                                        if (v < next_lanes.size()) {
                                            WordT trip = (trip_ptr && e < trip_ptr->size()) ? static_cast<WordT>((*trip_ptr)[e]) : 0;
                                            WordT prop = static_cast<WordT>(u_mask & ~trip);
                                            WordT prev = static_cast<WordT>(next_lanes[v]);
                                            WordT nw = static_cast<WordT>(prev | prop);
                                            if (nw != prev) {
                                                next_lanes[v] = static_cast<uint64_t>(nw);
                                                next_active_words[v >> 6] |= (1ULL << (v & 63));
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                } else {
                    if (slot.csc_offsets_ptr && slot.csc_targets_ptr && !trip_ptr) {
                        size_t csc_n = std::min<size_t>(
                            slot.csc_node_count > 0 ? slot.csc_node_count : (slot.node_count > 0 ? slot.node_count : n_nodes),
                            cur_lanes.size()
                        );
                        size_t max_words = std::min<size_t>(cur_active_words.size(), (csc_n + 63) / 64);
                        if (csc_fast_u32) {
                            size_t dyn_off_lim = slot.dynamic_csc_offsets.size();
                            size_t dyn_tgt_lim = slot.dynamic_csc_targets.size();
                            bool use_dyn = !slot.dynamic_csc_offsets.empty();
                            for (size_t w_idx = 0; w_idx < max_words; ++w_idx) {
                                uint64_t word = cur_active_words[w_idx];
                                while (word != 0ULL) {
                                    uint32_t bit = static_cast<uint32_t>(std::countr_zero(word));
                                    size_t v = (w_idx << 6) + bit;
                                    word &= (word - 1ULL);
                                    if (v >= csc_n) break;
                                    WordT v_mask = static_cast<WordT>(cur_lanes[v]) & active_mask_w;
                                    if (v_mask == 0) continue;
                                    uint32_t start = (!use_dyn || v < dyn_off_lim) ? csc_offsets_u32[v] : csc_offsets_u32[dyn_off_lim - 1];
                                    uint32_t end = (!use_dyn || (v + 1) < dyn_off_lim) ? csc_offsets_u32[v + 1] : csc_offsets_u32[dyn_off_lim - 1];
                                    for (uint32_t e = start; e < end; ++e) {
                                        uint32_t u = (!use_dyn || e < dyn_tgt_lim) ? csc_targets_u32[e] : 0u;
                                        if (u < next_lanes.size()) {
                                            WordT prev = static_cast<WordT>(next_lanes[u]);
                                            WordT nw = static_cast<WordT>(prev | v_mask);
                                            if (nw != prev) {
                                                next_lanes[u] = static_cast<uint64_t>(nw);
                                                next_active_words[u >> 6] |= (1ULL << (u & 63));
                                            }
                                        }
                                    }
                                }
                            }
                        } else {
                            for (size_t w_idx = 0; w_idx < max_words; ++w_idx) {
                                uint64_t word = cur_active_words[w_idx];
                                while (word != 0ULL) {
                                    uint32_t bit = static_cast<uint32_t>(std::countr_zero(word));
                                    size_t v = (w_idx << 6) + bit;
                                    word &= (word - 1ULL);
                                    if (v >= csc_n) break;
                                    WordT v_mask = static_cast<WordT>(cur_lanes[v]) & active_mask_w;
                                    if (v_mask == 0) continue;
                                    uint64_t start = slot.get_csc_offset(v);
                                    uint64_t end = slot.get_csc_offset(v + 1);
                                    for (uint64_t e = start; e < end; ++e) {
                                        uint64_t u = slot.get_csc_target(e);
                                        if (u < next_lanes.size()) {
                                            WordT prev = static_cast<WordT>(next_lanes[u]);
                                            WordT nw = static_cast<WordT>(prev | v_mask);
                                            if (nw != prev) {
                                                next_lanes[u] = static_cast<uint64_t>(nw);
                                                next_active_words[u >> 6] |= (1ULL << (u & 63));
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    } else if (slot.offsets_ptr && slot.targets_ptr) {
                        size_t csr_n = std::min<size_t>(slot.node_count > 0 ? slot.node_count : n_nodes, next_lanes.size());
                        for (size_t u = 0; u < csr_n; ++u) {
                            uint64_t start = slot.get_csr_offset(u);
                            uint64_t end = slot.get_csr_offset(u + 1);
                            for (uint64_t e = start; e < end; ++e) {
                                uint64_t v = slot.get_csr_target(e);
                                if (v < cur_lanes.size()) {
                                    WordT v_mask = static_cast<WordT>(cur_lanes[v]) & active_mask_w;
                                    if (v_mask != 0) {
                                        WordT trip = (trip_ptr && e < trip_ptr->size()) ? static_cast<WordT>((*trip_ptr)[e]) : 0;
                                        WordT prop = static_cast<WordT>(v_mask & ~trip);
                                        WordT prev = static_cast<WordT>(next_lanes[u]);
                                        WordT nw = static_cast<WordT>(prev | prop);
                                        if (nw != prev) {
                                            next_lanes[u] = static_cast<uint64_t>(nw);
                                            next_active_words[u >> 6] |= (1ULL << (u & 63));
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            };

            if (is_fixpoint) {
                auto& delta = vm_state->query_context->scratch_lanes;
                auto& next_delta = vm_state->query_context->scratch_lanes_b;
                auto& delta_active_words = vm_state->query_context->scratch_active_words;
                auto& next_delta_active_words = vm_state->query_context->scratch_active_words_b;
                size_t num_words = (n_nodes + 63) / 64;
                delta.assign(n_nodes, 0ULL);
                next_delta.assign(n_nodes, 0ULL);
                delta_active_words.assign(num_words, 0ULL);
                next_delta_active_words.assign(num_words, 0ULL);

                bool has_delta = false;
                for (size_t w_idx = 0; w_idx < lf_dst.active_words.size() && w_idx < num_words; ++w_idx) {
                    uint64_t word = lf_dst.active_words[w_idx];
                    if (word == 0ULL) continue;
                    delta_active_words[w_idx] = word;
                    has_delta = true;
                    while (word != 0ULL) {
                        uint32_t bit = static_cast<uint32_t>(std::countr_zero(word));
                        size_t u = (w_idx << 6) + bit;
                        word &= (word - 1ULL);
                        if (u < n_nodes) {
                            delta[u] = lf_dst.lanes[u];
                        }
                    }
                }

                int iter = 0;
                while (has_delta && iter < 1024) {
                    has_delta = false;
                    if (!is_csc) {
                        if (slot.offsets_ptr && slot.targets_ptr) {
                            size_t csr_n = std::min<size_t>(slot.node_count > 0 ? slot.node_count : n_nodes, delta.size());
                            size_t max_words = std::min<size_t>(delta_active_words.size(), (csr_n + 63) / 64);
                            if (csr_fast_u32 && !trip_ptr) {
                                for (size_t w_idx = 0; w_idx < max_words; ++w_idx) {
                                    uint64_t word = delta_active_words[w_idx];
                                    while (word != 0ULL) {
                                        uint32_t bit = static_cast<uint32_t>(std::countr_zero(word));
                                        size_t u = (w_idx << 6) + bit;
                                        word &= (word - 1ULL);
                                        if (u >= csr_n) break;
                                        WordT u_mask = static_cast<WordT>(delta[u]) & active_mask_w;
                                        if (u_mask == 0) continue;
                                        uint32_t start = csr_offsets_u32[u];
                                        uint32_t end = csr_offsets_u32[u + 1];
                                        for (uint32_t e = start; e < end; ++e) {
                                            uint32_t v = csr_targets_u32[e];
                                            if (v < n_nodes) {
                                                WordT vis = static_cast<WordT>(lf_dst.lanes[v]);
                                                WordT newly_reached = static_cast<WordT>(u_mask & ~vis);
                                                if (newly_reached != 0) {
                                                    lf_dst.lanes[v] = static_cast<uint64_t>(vis | newly_reached);
                                                    lf_dst.active_words[v >> 6] |= (1ULL << (v & 63));
                                                    next_delta[v] |= static_cast<uint64_t>(newly_reached);
                                                    next_delta_active_words[v >> 6] |= (1ULL << (v & 63));
                                                    has_delta = true;
                                                }
                                            }
                                        }
                                    }
                                }
                            } else {
                                for (size_t w_idx = 0; w_idx < max_words; ++w_idx) {
                                    uint64_t word = delta_active_words[w_idx];
                                    while (word != 0ULL) {
                                        uint32_t bit = static_cast<uint32_t>(std::countr_zero(word));
                                        size_t u = (w_idx << 6) + bit;
                                        word &= (word - 1ULL);
                                        if (u >= csr_n) break;
                                        WordT u_mask = static_cast<WordT>(delta[u]) & active_mask_w;
                                        if (u_mask == 0) continue;
                                        uint64_t start = slot.get_csr_offset(u);
                                        uint64_t end = slot.get_csr_offset(u + 1);
                                        for (uint64_t e = start; e < end; ++e) {
                                            uint64_t v = slot.get_csr_target(e);
                                            if (v < n_nodes) {
                                                WordT trip = (trip_ptr && e < trip_ptr->size()) ? static_cast<WordT>((*trip_ptr)[e]) : 0;
                                                WordT prop = static_cast<WordT>(u_mask & ~trip);
                                                WordT vis = static_cast<WordT>(lf_dst.lanes[v]);
                                                WordT newly_reached = static_cast<WordT>(prop & ~vis);
                                                if (newly_reached != 0) {
                                                    lf_dst.lanes[v] = static_cast<uint64_t>(vis | newly_reached);
                                                    lf_dst.active_words[v >> 6] |= (1ULL << (v & 63));
                                                    next_delta[v] |= static_cast<uint64_t>(newly_reached);
                                                    next_delta_active_words[v >> 6] |= (1ULL << (v & 63));
                                                    has_delta = true;
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    } else {
                        if (slot.csc_offsets_ptr && slot.csc_targets_ptr && !trip_ptr) {
                            size_t csc_n = std::min<size_t>(
                                slot.csc_node_count > 0 ? slot.csc_node_count : (slot.node_count > 0 ? slot.node_count : n_nodes),
                                delta.size()
                            );
                            size_t max_words = std::min<size_t>(delta_active_words.size(), (csc_n + 63) / 64);
                            if (csc_fast_u32) {
                                size_t dyn_off_lim = slot.dynamic_csc_offsets.size();
                                size_t dyn_tgt_lim = slot.dynamic_csc_targets.size();
                                bool use_dyn = !slot.dynamic_csc_offsets.empty();
                                for (size_t w_idx = 0; w_idx < max_words; ++w_idx) {
                                    uint64_t word = delta_active_words[w_idx];
                                    while (word != 0ULL) {
                                        uint32_t bit = static_cast<uint32_t>(std::countr_zero(word));
                                        size_t v = (w_idx << 6) + bit;
                                        word &= (word - 1ULL);
                                        if (v >= csc_n) break;
                                        WordT v_mask = static_cast<WordT>(delta[v]) & active_mask_w;
                                        if (v_mask == 0) continue;
                                        uint32_t start = (!use_dyn || v < dyn_off_lim) ? csc_offsets_u32[v] : csc_offsets_u32[dyn_off_lim - 1];
                                        uint32_t end = (!use_dyn || (v + 1) < dyn_off_lim) ? csc_offsets_u32[v + 1] : csc_offsets_u32[dyn_off_lim - 1];
                                        for (uint32_t e = start; e < end; ++e) {
                                            uint32_t u = (!use_dyn || e < dyn_tgt_lim) ? csc_targets_u32[e] : 0u;
                                            if (u < n_nodes) {
                                                WordT vis = static_cast<WordT>(lf_dst.lanes[u]);
                                                WordT newly_reached = static_cast<WordT>(v_mask & ~vis);
                                                if (newly_reached != 0) {
                                                    lf_dst.lanes[u] = static_cast<uint64_t>(vis | newly_reached);
                                                    lf_dst.active_words[u >> 6] |= (1ULL << (u & 63));
                                                    next_delta[u] |= static_cast<uint64_t>(newly_reached);
                                                    next_delta_active_words[u >> 6] |= (1ULL << (u & 63));
                                                    has_delta = true;
                                                }
                                            }
                                        }
                                    }
                                }
                            } else {
                                for (size_t w_idx = 0; w_idx < max_words; ++w_idx) {
                                    uint64_t word = delta_active_words[w_idx];
                                    while (word != 0ULL) {
                                        uint32_t bit = static_cast<uint32_t>(std::countr_zero(word));
                                        size_t v = (w_idx << 6) + bit;
                                        word &= (word - 1ULL);
                                        if (v >= csc_n) break;
                                        WordT v_mask = static_cast<WordT>(delta[v]) & active_mask_w;
                                        if (v_mask == 0) continue;
                                        uint64_t start = slot.get_csc_offset(v);
                                        uint64_t end = slot.get_csc_offset(v + 1);
                                        for (uint64_t e = start; e < end; ++e) {
                                            uint64_t u = slot.get_csc_target(e);
                                            if (u < n_nodes) {
                                                WordT vis = static_cast<WordT>(lf_dst.lanes[u]);
                                                WordT newly_reached = static_cast<WordT>(v_mask & ~vis);
                                                if (newly_reached != 0) {
                                                    lf_dst.lanes[u] = static_cast<uint64_t>(vis | newly_reached);
                                                    lf_dst.active_words[u >> 6] |= (1ULL << (u & 63));
                                                    next_delta[u] |= static_cast<uint64_t>(newly_reached);
                                                    next_delta_active_words[u >> 6] |= (1ULL << (u & 63));
                                                    has_delta = true;
                                                }
                                            }
                                        }
                                    }
                                }
                            }
                        } else if (slot.offsets_ptr && slot.targets_ptr) {
                            size_t csr_n = std::min<size_t>(slot.node_count > 0 ? slot.node_count : n_nodes, n_nodes);
                            for (size_t u = 0; u < csr_n; ++u) {
                                uint64_t start = slot.get_csr_offset(u);
                                uint64_t end = slot.get_csr_offset(u + 1);
                                for (uint64_t e = start; e < end; ++e) {
                                    uint64_t v = slot.get_csr_target(e);
                                    if (v < delta.size()) {
                                        WordT v_mask = static_cast<WordT>(delta[v]) & active_mask_w;
                                        if (v_mask != 0) {
                                            WordT trip = (trip_ptr && e < trip_ptr->size()) ? static_cast<WordT>((*trip_ptr)[e]) : 0;
                                            WordT prop = static_cast<WordT>(v_mask & ~trip);
                                            WordT vis = static_cast<WordT>(lf_dst.lanes[u]);
                                            WordT newly_reached = static_cast<WordT>(prop & ~vis);
                                            if (newly_reached != 0) {
                                                lf_dst.lanes[u] = static_cast<uint64_t>(vis | newly_reached);
                                                lf_dst.active_words[u >> 6] |= (1ULL << (u & 63));
                                                next_delta[u] |= static_cast<uint64_t>(newly_reached);
                                                next_delta_active_words[u >> 6] |= (1ULL << (u & 63));
                                                has_delta = true;
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }

                    for (size_t w_idx = 0; w_idx < num_words; ++w_idx) {
                        uint64_t word = delta_active_words[w_idx];
                        if (word == 0ULL) continue;
                        delta_active_words[w_idx] = 0ULL;
                        while (word != 0ULL) {
                            uint32_t bit = static_cast<uint32_t>(std::countr_zero(word));
                            size_t u = (w_idx << 6) + bit;
                            word &= (word - 1ULL);
                            if (u < n_nodes) {
                                delta[u] = 0ULL;
                            }
                        }
                    }
                    delta.swap(next_delta);
                    delta_active_words.swap(next_delta_active_words);
                    iter++;
                }
            } else {
                step_once(lf_src.lanes, lf_src.active_words, lf_dst.lanes, lf_dst.active_words);
            }
        };

        switch (lf_dst.width) {
            case IMPULSE_LANE_WIDTH_8:
                IMPULSE_TESTCASE(lf_dst.width == IMPULSE_LANE_WIDTH_8);
                execute_walk_tiered(uint8_t{0});
                break;
            case IMPULSE_LANE_WIDTH_16:
                IMPULSE_TESTCASE(lf_dst.width == IMPULSE_LANE_WIDTH_16);
                execute_walk_tiered(uint16_t{0});
                break;
            case IMPULSE_LANE_WIDTH_32:
                IMPULSE_TESTCASE(lf_dst.width == IMPULSE_LANE_WIDTH_32);
                execute_walk_tiered(uint32_t{0});
                break;
            case IMPULSE_LANE_WIDTH_64:
            default:
                IMPULSE_TESTCASE(lf_dst.width == IMPULSE_LANE_WIDTH_64);
                execute_walk_tiered(uint64_t{0});
                break;
        }

        bool any_set = false;
        bool any_changed = false;
        for (size_t w_idx = 0; w_idx < lf_dst.active_words.size(); ++w_idx) {
            uint64_t word = lf_dst.active_words[w_idx];
            if (word == 0ULL) continue;
            uint64_t cleaned_word = 0ULL;
            while (word != 0ULL) {
                uint32_t bit = static_cast<uint32_t>(std::countr_zero(word));
                size_t u = (w_idx << 6) + bit;
                word &= (word - 1ULL);
                if (u >= n_nodes) break;
                uint64_t w = lf_dst.lanes[u] & lf_dst.active_mask;
                lf_dst.lanes[u] = w;
                if (w != 0ULL) {
                    cleaned_word |= (1ULL << bit);
                    any_set = true;
                    uint64_t orig = (u < lf_src.lanes.size()) ? (lf_src.lanes[u] & lf_dst.active_mask) : 0ULL;
                    if ((w & ~orig) != 0ULL) any_changed = true;
                }
            }
            lf_dst.active_words[w_idx] = cleaned_word;
        }

        impulse_vm_branch_coverage_record(static_cast<uint32_t>(vm_state->pc), inst.opcode, 1u,
                                          IMPULSE_COV_KIND_TWO_WAY, IMPULSE_COV_BOTH,
                                          any_set ? 0u : 1u);

        vm_state->registers[dst] = static_cast<uint64_t>(h_dst);
        vm_state->register_types[dst] = TYPE_LANE_FRONTIER;

        if (is_fixpoint) {
            if (!any_changed) {
                vm_state->flags |= IMPULSE_VM_FLAG_ZF;
                vm_state->flags |= IMPULSE_VM_FLAG_ST;
            } else {
                vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;
                vm_state->flags &= ~IMPULSE_VM_FLAG_ST;
            }
        } else {
            if (!any_set) vm_state->flags |= IMPULSE_VM_FLAG_ZF;
            else vm_state->flags &= ~IMPULSE_VM_FLAG_ZF;
            if (!any_changed) vm_state->flags |= IMPULSE_VM_FLAG_ST;
            else vm_state->flags &= ~IMPULSE_VM_FLAG_ST;
        }
    }

    return IMPULSE_VM_OK;
}

#endif // IMPULSE_VM_LANE_WALK_COMMON_HPP
