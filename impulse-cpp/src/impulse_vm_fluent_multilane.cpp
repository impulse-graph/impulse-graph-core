/**
 * @file impulse_vm_fluent_multilane.cpp
 * @brief Bit-sliced Multi-Lane Graph Traversal (MultiLaneResult, MultiLaneTraversal, MultiLaneBuilder).
 */

#include "impulse_vm_fluent_internal.hpp"

namespace impulse::vm {

MultiLaneResult::MultiLaneResult(const impulse_snapshot_t* snapshot,
                                 uint16_t domain_id,
                                 uint32_t domain_node_count,
                                 uint32_t lane_count,
                                 std::vector<std::string> lane_keys,
                                 std::vector<uint64_t> words_per_node)
    : snapshot_(snapshot),
      domain_id_(domain_id),
      domain_node_count_(domain_node_count),
      lane_count_(lane_count),
      words_per_lane_((lane_count + 63U) / 64U),
      lane_keys_(std::move(lane_keys)),
      words_per_node_(std::move(words_per_node)) {}

uint64_t MultiLaneResult::laneMaskFor(uint32_t node_id) const {
    if (node_id >= domain_node_count_ || words_per_lane_ == 0) return 0ULL;
    return words_per_node_[static_cast<size_t>(node_id) * words_per_lane_];
}

uint32_t MultiLaneResult::lanePopcountFor(uint32_t node_id) const {
    if (node_id >= domain_node_count_ || words_per_lane_ == 0) return 0U;
    uint32_t total = 0;
    const uint64_t* row = &words_per_node_[static_cast<size_t>(node_id) * words_per_lane_];
    for (uint32_t w = 0; w < words_per_lane_; ++w) {
        total += static_cast<uint32_t>(std::popcount(row[w]));
    }
    return total;
}

std::vector<uint32_t> MultiLaneResult::attributionFor(uint32_t node_id) const {
    std::vector<uint32_t> lanes;
    if (node_id >= domain_node_count_ || words_per_lane_ == 0) return lanes;
    const uint64_t* row = &words_per_node_[static_cast<size_t>(node_id) * words_per_lane_];
    for (uint32_t w = 0; w < words_per_lane_; ++w) {
        uint64_t m = row[w];
        while (m != 0ULL) {
            uint32_t b = static_cast<uint32_t>(std::countr_zero(m));
            uint32_t global_lane = w * 64U + b;
            if (global_lane < lane_count_) {
                lanes.push_back(global_lane);
            }
            m &= (m - 1ULL);
        }
    }
    return lanes;
}

std::vector<std::string> MultiLaneResult::keyAttributionFor(const std::string& node_key) const {
    uint32_t u = resolve_key_to_dense(snapshot_, domain_id_, node_key);
    auto lanes = attributionFor(u);
    std::vector<std::string> out;
    out.reserve(lanes.size());
    for (uint32_t lb : lanes) {
        if (lb < lane_keys_.size()) {
            out.push_back(lane_keys_[lb]);
        } else {
            out.push_back(std::to_string(lb));
        }
    }
    return out;
}

std::vector<uint32_t> MultiLaneResult::nodesWithQuorum(uint32_t min_lanes) const {
    std::vector<uint32_t> out;
    if (words_per_lane_ == 0 || lane_count_ == 0) return out;
    for (uint32_t u = 0; u < domain_node_count_; ++u) {
        if (lanePopcountFor(u) >= min_lanes) {
            out.push_back(u);
        }
    }
    return out;
}

std::vector<std::pair<std::string, uint32_t>> MultiLaneResult::keysWithQuorum(uint32_t min_lanes) const {
    std::vector<std::pair<std::string, uint32_t>> out;
    if (words_per_lane_ == 0 || lane_count_ == 0) return out;
    for (uint32_t u = 0; u < domain_node_count_; ++u) {
        uint32_t pc = lanePopcountFor(u);
        if (pc >= min_lanes) {
            out.emplace_back(resolve_dense_to_key(snapshot_, domain_id_, u), pc);
        }
    }
    return out;
}

std::vector<std::pair<uint32_t, uint32_t>> MultiLaneResult::findSetCoverPairs(uint64_t target_mask) const {
    std::vector<std::pair<uint32_t, uint32_t>> pairs;
    if (lane_count_ == 0 || words_per_lane_ == 0) return pairs;
    uint64_t eff_mask = target_mask;
    if (lane_count_ < 64) {
        eff_mask &= ((1ULL << lane_count_) - 1ULL);
    }
    if (eff_mask == 0ULL) return pairs;

    std::vector<uint32_t> active;
    for (uint32_t u = 0; u < domain_node_count_; ++u) {
        if ((laneMaskFor(u) & eff_mask) != 0ULL) {
            active.push_back(u);
        }
    }
    for (size_t i = 0; i < active.size(); ++i) {
        uint64_t mu = laneMaskFor(active[i]) & eff_mask;
        for (size_t j = i + 1; j < active.size(); ++j) {
            uint64_t mv = laneMaskFor(active[j]) & eff_mask;
            if ((mu | mv) == eff_mask) {
                pairs.emplace_back(active[i], active[j]);
            }
        }
    }
    return pairs;
}

std::vector<uint32_t> MultiLaneResult::findGreedySetCover(uint64_t target_mask, uint32_t max_nodes) const {
    std::vector<uint32_t> selected;
    if (lane_count_ == 0 || words_per_lane_ == 0 || max_nodes == 0) return selected;

    std::vector<uint64_t> uncovered(words_per_lane_, ~0ULL);
    uncovered[0] &= target_mask;
    if (lane_count_ % 64 != 0) {
        uncovered[words_per_lane_ - 1] &= ((1ULL << (lane_count_ % 64)) - 1ULL);
    }

    auto has_uncovered = [&]() {
        for (uint64_t w : uncovered) {
            if (w != 0ULL) return true;
        }
        return false;
    };

    for (uint32_t step = 0; step < max_nodes && has_uncovered(); ++step) {
        uint32_t best_u = UINT32_MAX;
        uint32_t best_gain = 0;
        for (uint32_t u = 0; u < domain_node_count_; ++u) {
            uint32_t gain = 0;
            const uint64_t* row = &words_per_node_[static_cast<size_t>(u) * words_per_lane_];
            for (uint32_t w = 0; w < words_per_lane_; ++w) {
                gain += static_cast<uint32_t>(std::popcount(row[w] & uncovered[w]));
            }
            if (gain > best_gain) {
                best_gain = gain;
                best_u = u;
            }
        }
        if (best_u == UINT32_MAX || best_gain == 0) break;
        selected.push_back(best_u);
        const uint64_t* best_row = &words_per_node_[static_cast<size_t>(best_u) * words_per_lane_];
        for (uint32_t w = 0; w < words_per_lane_; ++w) {
            uncovered[w] &= ~best_row[w];
        }
    }
    return selected;
}

std::vector<std::pair<std::string, std::string>> MultiLaneResult::findKeySetCoverPairs(
    const std::vector<std::string>& required_keys) const
{
    uint64_t target_mask = ~0ULL;
    if (!required_keys.empty()) {
        target_mask = 0ULL;
        for (const auto& k : required_keys) {
            for (size_t b = 0; b < lane_keys_.size() && b < 64; ++b) {
                if (lane_keys_[b] == k) {
                    target_mask |= (1ULL << b);
                }
            }
        }
    }
    auto dense_pairs = findSetCoverPairs(target_mask);
    std::vector<std::pair<std::string, std::string>> out;
    out.reserve(dense_pairs.size());
    for (const auto& p : dense_pairs) {
        out.emplace_back(
            resolve_dense_to_key(snapshot_, domain_id_, p.first),
            resolve_dense_to_key(snapshot_, domain_id_, p.second));
    }
    return out;
}

std::vector<std::string> MultiLaneResult::findGreedyKeySetCover(
    const std::vector<std::string>& required_keys, uint32_t max_nodes) const
{
    uint64_t target_mask = ~0ULL;
    if (!required_keys.empty()) {
        target_mask = 0ULL;
        for (const auto& k : required_keys) {
            for (size_t b = 0; b < lane_keys_.size() && b < 64; ++b) {
                if (lane_keys_[b] == k) {
                    target_mask |= (1ULL << b);
                }
            }
        }
    }
    auto dense_ids = findGreedySetCover(target_mask, max_nodes);
    std::vector<std::string> out;
    out.reserve(dense_ids.size());
    for (uint32_t u : dense_ids) {
        out.push_back(resolve_dense_to_key(snapshot_, domain_id_, u));
    }
    return out;
}

std::vector<std::pair<uint64_t, float>> MultiLaneResult::topKWithQuorum(
    uint32_t min_lanes, size_t k, const std::vector<float>& query_vector,
    const std::string& attr_name, uint8_t metric) const
{
    auto qnodes32 = nodesWithQuorum(min_lanes);
    std::vector<uint64_t> qnodes(qnodes32.begin(), qnodes32.end());
    return Traversal(snapshot_, domain_id_, std::move(qnodes), false)
        .rerank(query_vector, attr_name, metric)
        .topK(k);
}

std::vector<std::pair<std::string, float>> MultiLaneResult::topKKeysWithQuorum(
    uint32_t min_lanes, size_t k, const std::vector<float>& query_vector,
    const std::string& attr_name, uint8_t metric) const
{
    auto qnodes32 = nodesWithQuorum(min_lanes);
    std::vector<uint64_t> qnodes(qnodes32.begin(), qnodes32.end());
    return Traversal(snapshot_, domain_id_, std::move(qnodes), false)
        .rerank(query_vector, attr_name, metric)
        .topKKeys(k);
}


// --- Multi-Lane Traversal Implementation ---

MultiLaneTraversal::MultiLaneTraversal(const impulse_snapshot_t* snapshot,
                                       uint16_t domain_id,
                                       InitMode mode,
                                       std::vector<uint64_t> seeds,
                                       std::vector<std::string> lane_keys,
                                       std::vector<std::pair<uint32_t, uint32_t>> group_pairs,
                                       uint16_t outage_rel_id,
                                       std::vector<std::vector<uint32_t>> tripped_edges_per_lane)
    : snapshot_(snapshot),
      current_domain_id_(domain_id),
      mode_(mode),
      seeds_(std::move(seeds)),
      lane_keys_(std::move(lane_keys)),
      group_pairs_(std::move(group_pairs)),
      outage_rel_id_(outage_rel_id),
      tripped_edges_per_lane_(std::move(tripped_edges_per_lane)) {}

MultiLaneTraversal& MultiLaneTraversal::out(const std::string& relation) {
    LaneStep s{};
    s.kind = LaneStep::Kind::OUT;
    s.str_arg = relation;
    steps_.push_back(std::move(s));
    return *this;
}

MultiLaneTraversal& MultiLaneTraversal::out(const std::string& relation, const std::string& /*edge_cel_predicate*/) {
    return out(relation);
}

MultiLaneTraversal& MultiLaneTraversal::out(const std::vector<std::string>& relations) {
    if (relations.size() == 1) return out(relations[0]);
    LaneStep s{};
    s.kind = LaneStep::Kind::OUT_MULTI;
    s.rel_names = relations;
    steps_.push_back(std::move(s));
    return *this;
}

MultiLaneTraversal& MultiLaneTraversal::in(const std::string& relation) {
    LaneStep s{};
    s.kind = LaneStep::Kind::IN;
    s.str_arg = relation;
    steps_.push_back(std::move(s));
    return *this;
}

MultiLaneTraversal& MultiLaneTraversal::in(const std::string& relation, const std::string& /*edge_cel_predicate*/) {
    return in(relation);
}

MultiLaneTraversal& MultiLaneTraversal::in(const std::vector<std::string>& relations) {
    if (relations.size() == 1) return in(relations[0]);
    LaneStep s{};
    s.kind = LaneStep::Kind::IN_MULTI;
    s.rel_names = relations;
    steps_.push_back(std::move(s));
    return *this;
}

MultiLaneTraversal& MultiLaneTraversal::both(const std::string& relation) {
    LaneStep s{};
    s.kind = LaneStep::Kind::BOTH;
    s.str_arg = relation;
    steps_.push_back(std::move(s));
    return *this;
}

MultiLaneTraversal& MultiLaneTraversal::both(const std::vector<std::string>& relations) {
    if (relations.size() == 1) return both(relations[0]);
    LaneStep s{};
    s.kind = LaneStep::Kind::BOTH_MULTI;
    s.rel_names = relations;
    steps_.push_back(std::move(s));
    return *this;
}

MultiLaneTraversal& MultiLaneTraversal::repeat(int iterations, const std::string& relation) {
    LaneStep s{};
    s.kind = LaneStep::Kind::REPEAT;
    s.str_arg = relation;
    s.u16_arg = static_cast<uint16_t>(std::max(0, iterations));
    steps_.push_back(std::move(s));
    return *this;
}

MultiLaneTraversal& MultiLaneTraversal::repeatUntilStable(const std::string& relation) {
    LaneStep s{};
    s.kind = LaneStep::Kind::REPEAT_UNTIL_STABLE;
    s.str_arg = relation;
    steps_.push_back(std::move(s));
    return *this;
}

MultiLaneTraversal& MultiLaneTraversal::filter(const std::string& node_cel_predicate) {
    LaneStep s{};
    s.kind = LaneStep::Kind::FILTER_CEL;
    s.str_arg = node_cel_predicate;
    steps_.push_back(std::move(s));
    return *this;
}

MultiLaneTraversal& MultiLaneTraversal::project(const std::string& cel_expression) {
    LaneStep s{};
    s.kind = LaneStep::Kind::PROJECT_CEL;
    s.str_arg = cel_expression;
    steps_.push_back(std::move(s));
    return *this;
}

MultiLaneTraversal& MultiLaneTraversal::pruneWhere(const std::string& deny_cel_predicate) {
    LaneStep s{};
    s.kind = LaneStep::Kind::PRUNE_WHERE_CEL;
    s.str_arg = deny_cel_predicate;
    steps_.push_back(std::move(s));
    return *this;
}

MultiLaneTraversal& MultiLaneTraversal::pruneDeniedBy(const std::string& deny_relation) {
    LaneStep s{};
    s.kind = LaneStep::Kind::PRUNE_DENIED_BY;
    s.str_arg = deny_relation;
    steps_.push_back(std::move(s));
    return *this;
}

MultiLaneTraversal& MultiLaneTraversal::pruneHubs(uint16_t max_degree, uint8_t min_quorum) {
    LaneStep s{};
    s.kind = LaneStep::Kind::PRUNE_HUBS;
    s.u16_arg = max_degree;
    s.u8_arg = min_quorum;
    steps_.push_back(std::move(s));
    return *this;
}

MultiLaneTraversal& MultiLaneTraversal::whereQuorum(uint8_t min_lanes) {
    LaneStep s{};
    s.kind = LaneStep::Kind::WHERE_QUORUM;
    s.u8_arg = min_lanes;
    steps_.push_back(std::move(s));
    return *this;
}

MultiLaneTraversal& MultiLaneTraversal::whereMask(uint64_t required_any_mask, uint64_t forbidden_mask) {
    LaneStep s{};
    s.kind = LaneStep::Kind::WHERE_MASK;
    s.mask_req = required_any_mask;
    s.mask_forbid = forbidden_mask;
    steps_.push_back(std::move(s));
    return *this;
}

std::vector<uint32_t> MultiLaneTraversal::resolveKeyIndices(const std::vector<std::string>& keys) const {
    std::vector<uint32_t> indices;
    for (const auto& k : keys) {
        for (size_t i = 0; i < lane_keys_.size(); ++i) {
            if (lane_keys_[i] == k) {
                indices.push_back(static_cast<uint32_t>(i));
                break;
            }
        }
    }
    return indices;
}

MultiLaneTraversal& MultiLaneTraversal::mustHitAtLeast(uint8_t min_lanes, const std::vector<std::string>& subset_seed_keys) {
    LaneStep s{};
    s.kind = LaneStep::Kind::MUST_HIT_AT_LEAST;
    s.u8_arg = min_lanes;
    s.lane_indices = resolveKeyIndices(subset_seed_keys);
    steps_.push_back(std::move(s));
    return *this;
}

MultiLaneTraversal& MultiLaneTraversal::mustAvoidAll(const std::vector<std::string>& forbidden_seed_keys) {
    LaneStep s{};
    s.kind = LaneStep::Kind::MUST_AVOID_ALL;
    s.lane_indices = resolveKeyIndices(forbidden_seed_keys);
    steps_.push_back(std::move(s));
    return *this;
}

MultiLaneResult MultiLaneTraversal::collect() const {
    uint32_t N = get_domain_node_count(snapshot_, current_domain_id_);
    uint32_t K = static_cast<uint32_t>(seeds_.size());
    uint32_t words_per_lane = (K + 63U) / 64U;
    std::vector<uint64_t> words_per_node(static_cast<size_t>(N) * words_per_lane, 0ULL);

    if (!snapshot_ || N == 0 || K == 0) {
        return MultiLaneResult(snapshot_, current_domain_id_, N, K, lane_keys_, std::move(words_per_node));
    }

    // Execute in batches of <= 64 lanes through ImpulseVM (OP_LANE_*)
    impulse_vm_context_t* ctx = impulse_vm_context_create(snapshot_);
    if (!ctx) {
        return MultiLaneResult(snapshot_, current_domain_id_, N, K, lane_keys_, std::move(words_per_node));
    }

    for (uint32_t batch_idx = 0; batch_idx < words_per_lane; ++batch_idx) {
        uint32_t lane_start = batch_idx * 64U;
        uint32_t batch_k = std::min(64U, K - lane_start);

        impulse_vm_state_t state{};
        state.query_context = ctx;

        // Build inline data section with batch_k seed uint32_t values
        std::vector<uint32_t> inline_u32;
        std::vector<uint32_t> group_u32;
        if (mode_ == InitMode::SEEDS || mode_ == InitMode::OUTAGES) {
            for (uint32_t b = 0; b < batch_k; ++b) {
                inline_u32.push_back(static_cast<uint32_t>(seeds_[lane_start + b]));
            }
        } else if (mode_ == InitMode::GROUPS) {
            for (const auto& gp : group_pairs_) {
                if (gp.second >= lane_start && gp.second < lane_start + batch_k) {
                    inline_u32.push_back(gp.first);
                    group_u32.push_back(gp.second - lane_start);
                }
            }
        }

        size_t group_offset_bytes = inline_u32.size() * sizeof(uint32_t);
        size_t group_u32_count = group_u32.size();
        for (uint32_t g : group_u32) {
            inline_u32.push_back(g);
        }

        size_t outage_pairs_offset_bytes = inline_u32.size() * sizeof(uint32_t);
        size_t outage_pairs_u32_count = 0;
        if (mode_ == InitMode::OUTAGES) {
            for (uint32_t b = 0; b < batch_k; ++b) {
                uint32_t global_lane = lane_start + b;
                if (global_lane < tripped_edges_per_lane_.size()) {
                    for (uint32_t edge_idx : tripped_edges_per_lane_[global_lane]) {
                        inline_u32.push_back(edge_idx);
                        inline_u32.push_back(b);
                        outage_pairs_u32_count += 2;
                    }
                }
            }
        }

        impulse_vm_context_bind_inline_data(ctx, inline_u32.data(), inline_u32.size() * sizeof(uint32_t));

        std::vector<impulse_instruction_t> prog;
        uint32_t seed_u32_count = static_cast<uint32_t>(group_offset_bytes / sizeof(uint32_t));
        // R1 <- inline node array of seeds/group_nodes
        prog.push_back(impulse_instruction_t{
            OP_LOAD_INLINE_ARRAY, 1, 1,
            static_cast<uint32_t>(0 | (seed_u32_count << 16))
        });

        if (mode_ == InitMode::GROUPS) {
            prog.push_back(impulse_instruction_t{
                OP_LOAD_INLINE_ARRAY, 1, 2,
                static_cast<uint32_t>(static_cast<uint16_t>(group_offset_bytes) |
                                      (static_cast<uint32_t>(group_u32_count) << 16))
            });
            prog.push_back(impulse_instruction_t{
                OP_LANE_INIT_GROUPS, 0, 0,
                static_cast<uint32_t>(1 | (2 << 8) | (batch_k << 16))
            });
        } else {
            prog.push_back(impulse_instruction_t{
                OP_LANE_INIT_SEEDS, 0, 0, 1
            });
        }

        if (mode_ == InitMode::OUTAGES && outage_pairs_u32_count > 0) {
            prog.push_back(impulse_instruction_t{
                OP_LOAD_INLINE_ARRAY, 0, 2,
                static_cast<uint32_t>(static_cast<uint16_t>(outage_pairs_offset_bytes) |
                                      (static_cast<uint32_t>(outage_pairs_u32_count / 2) << 16))
            });
            prog.push_back(impulse_instruction_t{
                OP_LANE_SET_EDGE_MASK, 1, 0,
                static_cast<uint32_t>(0 | (2 << 8) | (static_cast<uint32_t>(outage_rel_id_) << 16))
            });
        }

        // Execute hop & hub pruning steps on batch frontier R0
        for (const auto& step : steps_) {
            if (step.kind == LaneStep::Kind::OUT) {
                uint16_t rel_id = resolve_relation_id_by_name(snapshot_, current_domain_id_, step.str_arg);
                prog.push_back(impulse_instruction_t{
                    OP_LANE_CSR_WALK, 0, 0,
                    static_cast<uint32_t>(0 | (static_cast<uint32_t>(rel_id) << 16))
                });
            } else if (step.kind == LaneStep::Kind::IN) {
                uint16_t rel_id = resolve_relation_id_by_name(snapshot_, current_domain_id_, step.str_arg);
                prog.push_back(impulse_instruction_t{
                    OP_LANE_CSC_WALK, 0, 0,
                    static_cast<uint32_t>(0 | (static_cast<uint32_t>(rel_id) << 16))
                });
            } else if (step.kind == LaneStep::Kind::BOTH || step.kind == LaneStep::Kind::OUT_MULTI ||
                       step.kind == LaneStep::Kind::IN_MULTI || step.kind == LaneStep::Kind::BOTH_MULTI) {
                std::vector<std::pair<uint16_t, bool>> branches;
                if (step.kind == LaneStep::Kind::BOTH) {
                    uint16_t rid = resolve_relation_id_by_name(snapshot_, current_domain_id_, step.str_arg);
                    branches.emplace_back(rid, false);
                    branches.emplace_back(rid, true);
                } else {
                    for (const auto& rname : step.rel_names) {
                        uint16_t rid = resolve_relation_id_by_name(snapshot_, current_domain_id_, rname);
                        if (step.kind == LaneStep::Kind::OUT_MULTI || step.kind == LaneStep::Kind::BOTH_MULTI) {
                            branches.emplace_back(rid, false);
                        }
                        if (step.kind == LaneStep::Kind::IN_MULTI || step.kind == LaneStep::Kind::BOTH_MULTI) {
                            branches.emplace_back(rid, true);
                        }
                    }
                }
                if (!branches.empty()) {
                    prog.push_back(impulse_instruction_t{
                        static_cast<uint8_t>(branches[0].second ? OP_LANE_CSC_WALK : OP_LANE_CSR_WALK), 0, 3,
                        static_cast<uint32_t>(0 | (static_cast<uint32_t>(branches[0].first) << 16))
                    });
                    for (size_t bi = 1; bi < branches.size(); ++bi) {
                        prog.push_back(impulse_instruction_t{
                            static_cast<uint8_t>(branches[bi].second ? OP_LANE_CSC_WALK : OP_LANE_CSR_WALK), 0, 4,
                            static_cast<uint32_t>(0 | (static_cast<uint32_t>(branches[bi].first) << 16))
                        });
                        prog.push_back(impulse_instruction_t{
                            OP_SET_UNION, 0, 3,
                            static_cast<uint32_t>(3 | (4 << 16))
                        });
                    }
                    prog.push_back(impulse_instruction_t{OP_MOV, 0, 0, 3});
                }
            } else if (step.kind == LaneStep::Kind::REPEAT) {
                uint16_t rel_id = resolve_relation_id_by_name(snapshot_, current_domain_id_, step.str_arg);
                for (uint16_t it = 0; it < step.u16_arg; ++it) {
                    prog.push_back(impulse_instruction_t{
                        OP_LANE_CSR_WALK, 0, 0,
                        static_cast<uint32_t>(0 | (static_cast<uint32_t>(rel_id) << 16))
                    });
                }
            } else if (step.kind == LaneStep::Kind::REPEAT_UNTIL_STABLE) {
                uint16_t rel_id = resolve_relation_id_by_name(snapshot_, current_domain_id_, step.str_arg);
                prog.push_back(impulse_instruction_t{
                    OP_LANE_CSR_WALK, 1, 0,
                    static_cast<uint32_t>(0 | (static_cast<uint32_t>(rel_id) << 16))
                });
            } else if (step.kind == LaneStep::Kind::PRUNE_HUBS && words_per_lane == 1) {
                prog.push_back(impulse_instruction_t{
                    OP_LANE_PRUNE_MASK, 2, 0,
                    static_cast<uint32_t>(0 | (static_cast<uint32_t>(step.u16_arg) << 8) | (static_cast<uint32_t>(step.u8_arg) << 24))
                });
            }
        }
        prog.push_back(impulse_instruction_t{OP_HALT, 0, 0, 0});

        impulse_vm_status_t st = impulse_vm_execute(prog.data(), prog.size(), &state, 0);
        if (st == IMPULSE_VM_OK && state.register_types[0] == TYPE_LANE_FRONTIER) {
            size_t l_handle = static_cast<size_t>(state.registers[0]);
            const uint64_t* lane_data = impulse_vm_context_lane_frontier_data(ctx, l_handle);
            size_t lane_nodes = impulse_vm_context_lane_frontier_node_count(ctx, l_handle);
            uint64_t act_mask = impulse_vm_context_lane_frontier_active_mask(ctx, l_handle);
            if (lane_data != nullptr) {
                size_t copy_n = std::min<size_t>(N, lane_nodes);
                for (size_t u = 0; u < copy_n; ++u) {
                    words_per_node[u * words_per_lane + batch_idx] = (lane_data[u] & act_mask);
                }
            }
        }
        impulse_vm_context_release_lane_frontier(ctx, 0);
    }

    impulse_vm_context_destroy(ctx);

    // Apply cross-lane filters (.whereQuorum, .whereMask, .mustHitAtLeast, .mustAvoidAll, .filter, .pruneWhere, .project)
    for (const auto& step : steps_) {
        if (step.kind == LaneStep::Kind::WHERE_QUORUM) {
            for (uint32_t u = 0; u < N; ++u) {
                uint32_t pc = 0;
                uint64_t* row = &words_per_node[static_cast<size_t>(u) * words_per_lane];
                for (uint32_t w = 0; w < words_per_lane; ++w) pc += static_cast<uint32_t>(std::popcount(row[w]));
                if (pc < step.u8_arg) {
                    for (uint32_t w = 0; w < words_per_lane; ++w) row[w] = 0ULL;
                }
            }
        } else if (step.kind == LaneStep::Kind::WHERE_MASK) {
            for (uint32_t u = 0; u < N; ++u) {
                uint64_t* row = &words_per_node[static_cast<size_t>(u) * words_per_lane];
                uint64_t m0 = row[0];
                if ((m0 & step.mask_req) == 0ULL || (m0 & step.mask_forbid) != 0ULL) {
                    for (uint32_t w = 0; w < words_per_lane; ++w) row[w] = 0ULL;
                }
            }
        } else if (step.kind == LaneStep::Kind::MUST_HIT_AT_LEAST) {
            for (uint32_t u = 0; u < N; ++u) {
                uint64_t* row = &words_per_node[static_cast<size_t>(u) * words_per_lane];
                uint32_t hits = 0;
                for (uint32_t lb : step.lane_indices) {
                    uint32_t w = lb / 64U;
                    uint32_t b = lb % 64U;
                    if (w < words_per_lane && ((row[w] >> b) & 1ULL) != 0ULL) {
                        hits++;
                    }
                }
                if (hits < step.u8_arg) {
                    for (uint32_t w = 0; w < words_per_lane; ++w) row[w] = 0ULL;
                }
            }
        } else if (step.kind == LaneStep::Kind::MUST_AVOID_ALL) {
            for (uint32_t u = 0; u < N; ++u) {
                uint64_t* row = &words_per_node[static_cast<size_t>(u) * words_per_lane];
                bool hit_forbid = false;
                for (uint32_t lb : step.lane_indices) {
                    uint32_t w = lb / 64U;
                    uint32_t b = lb % 64U;
                    if (w < words_per_lane && ((row[w] >> b) & 1ULL) != 0ULL) {
                        hit_forbid = true;
                        break;
                    }
                }
                if (hit_forbid) {
                    for (uint32_t w = 0; w < words_per_lane; ++w) row[w] = 0ULL;
                }
            }
        } else if (step.kind == LaneStep::Kind::FILTER_CEL || step.kind == LaneStep::Kind::PRUNE_WHERE_CEL ||
                   step.kind == LaneStep::Kind::PROJECT_CEL) {
            impulse::cel::Parser p(step.str_arg);
            auto ast = p.parse_expression();
            if (ast) {
                auto opt_ast = impulse::cel::AstOptimizer::optimize(ast);
                for (uint32_t u = 0; u < N; ++u) {
                    uint64_t* row = &words_per_node[static_cast<size_t>(u) * words_per_lane];
                    if (row[0] == 0ULL) continue;
                    if (step.kind == LaneStep::Kind::FILTER_CEL) {
                        if (!impulse::cel::CelMultiLaneEvaluator::eval_bool(opt_ast, row[0])) {
                            for (uint32_t w = 0; w < words_per_lane; ++w) row[w] = 0ULL;
                        }
                    } else if (step.kind == LaneStep::Kind::PRUNE_WHERE_CEL) {
                        if (impulse::cel::CelMultiLaneEvaluator::eval_bool(opt_ast, row[0])) {
                            for (uint32_t w = 0; w < words_per_lane; ++w) row[w] = 0ULL;
                        }
                    } else if (step.kind == LaneStep::Kind::PROJECT_CEL) {
                        row[0] = impulse::cel::CelMultiLaneEvaluator::eval_project_lane(opt_ast, row[0]);
                    }
                }
            }
        }
    }

    return MultiLaneResult(snapshot_, current_domain_id_, N, K, lane_keys_, std::move(words_per_node));
}

Traversal MultiLaneTraversal::toQuorumTraversal(uint8_t min_lanes) const {
    auto res = collect();
    auto quorum_nodes32 = res.nodesWithQuorum(min_lanes);
    std::vector<uint64_t> qnodes(quorum_nodes32.begin(), quorum_nodes32.end());
    return Traversal(snapshot_, current_domain_id_, std::move(qnodes), false);
}

std::vector<std::pair<uint64_t, float>> MultiLaneTraversal::topKWithQuorum(
    uint32_t min_lanes, size_t k, const std::vector<float>& query_vector,
    const std::string& attr_name, uint8_t metric) const
{
    return collect().topKWithQuorum(min_lanes, k, query_vector, attr_name, metric);
}

std::vector<std::pair<std::string, float>> MultiLaneTraversal::topKKeysWithQuorum(
    uint32_t min_lanes, size_t k, const std::vector<float>& query_vector,
    const std::string& attr_name, uint8_t metric) const
{
    return collect().topKKeysWithQuorum(min_lanes, k, query_vector, attr_name, metric);
}

std::vector<double> MultiLaneTraversal::reducePerLane(const std::string& /*node_attribute*/, ReducerOp op) const {
    auto res = collect();
    uint32_t K = res.laneCount();
    uint32_t N = res.domainNodeCount();
    std::vector<double> out(K, 0.0);
    if (K == 0 || N == 0 || !snapshot_) return out;

    const float* attr_f32 = nullptr;
    uint64_t attr_bytes = 0;
    if (impulse_snapshot_relation_count(snapshot_) > 0) {
        const void* raw_data = nullptr;
        uint8_t tc = 0;
        uint32_t dim = 0;
        if (impulse_snapshot_get_attribute_buffers(snapshot_, 0, 0, &raw_data, &attr_bytes, nullptr, nullptr, &tc, &dim) == IMPULSE_OK &&
            raw_data != nullptr && tc == IMPULSE_ATTR_TYPE_FLOAT32) {
            attr_f32 = static_cast<const float*>(raw_data);
        }
    }
    size_t attr_count = attr_f32 ? (attr_bytes / sizeof(float)) : 0;

    std::vector<uint32_t> counts(K, 0);
    if (op == ReducerOp::MIN) std::fill(out.begin(), out.end(), 1e30);
    else if (op == ReducerOp::MAX) std::fill(out.begin(), out.end(), -1e30);

    for (uint32_t u = 0; u < N; ++u) {
        double val = (u < attr_count) ? static_cast<double>(attr_f32[u]) : 0.0;
        auto lanes = res.attributionFor(u);
        for (uint32_t b : lanes) {
            if (b >= K) continue;
            counts[b]++;
            switch (op) {
                case ReducerOp::SUM:
                case ReducerOp::AVG:
                    out[b] += val;
                    break;
                case ReducerOp::MIN:
                    if (val < out[b]) out[b] = val;
                    break;
                case ReducerOp::MAX:
                    if (val > out[b]) out[b] = val;
                    break;
                case ReducerOp::COUNT:
                    out[b] += 1.0;
                    break;
                default:
                    break;
            }
        }
    }

    for (uint32_t b = 0; b < K; ++b) {
        if (counts[b] == 0 && (op == ReducerOp::MIN || op == ReducerOp::MAX)) {
            out[b] = 0.0;
        } else if (op == ReducerOp::AVG && counts[b] > 0) {
            out[b] /= static_cast<double>(counts[b]);
        }
    }
    return out;
}

std::vector<int64_t> MultiLaneTraversal::argMinNodesPerLane(const std::string& /*node_attribute*/) const {
    auto res = collect();
    uint32_t K = res.laneCount();
    uint32_t N = res.domainNodeCount();
    std::vector<int64_t> best_id(K, -1);
    if (K == 0 || N == 0 || !snapshot_) return best_id;

    const float* attr_f32 = nullptr;
    uint64_t attr_bytes = 0;
    if (impulse_snapshot_relation_count(snapshot_) > 0) {
        const void* raw_data = nullptr;
        uint8_t tc = 0;
        uint32_t dim = 0;
        if (impulse_snapshot_get_attribute_buffers(snapshot_, 0, 0, &raw_data, &attr_bytes, nullptr, nullptr, &tc, &dim) == IMPULSE_OK &&
            raw_data != nullptr && tc == IMPULSE_ATTR_TYPE_FLOAT32) {
            attr_f32 = static_cast<const float*>(raw_data);
        }
    }
    size_t attr_count = attr_f32 ? (attr_bytes / sizeof(float)) : 0;
    std::vector<double> best_val(K, 1e30);

    for (uint32_t u = 0; u < N; ++u) {
        double val = (u < attr_count) ? static_cast<double>(attr_f32[u]) : 0.0;
        auto lanes = res.attributionFor(u);
        for (uint32_t b : lanes) {
            if (b < K) {
                if (best_id[b] == -1 || val < best_val[b] || (val == best_val[b] && static_cast<int64_t>(u) < best_id[b])) {
                    best_val[b] = val;
                    best_id[b] = static_cast<int64_t>(u);
                }
            }
        }
    }
    return best_id;
}

std::vector<int64_t> MultiLaneTraversal::argMaxNodesPerLane(const std::string& /*node_attribute*/) const {
    auto res = collect();
    uint32_t K = res.laneCount();
    uint32_t N = res.domainNodeCount();
    std::vector<int64_t> best_id(K, -1);
    if (K == 0 || N == 0 || !snapshot_) return best_id;

    const float* attr_f32 = nullptr;
    uint64_t attr_bytes = 0;
    if (impulse_snapshot_relation_count(snapshot_) > 0) {
        const void* raw_data = nullptr;
        uint8_t tc = 0;
        uint32_t dim = 0;
        if (impulse_snapshot_get_attribute_buffers(snapshot_, 0, 0, &raw_data, &attr_bytes, nullptr, nullptr, &tc, &dim) == IMPULSE_OK &&
            raw_data != nullptr && tc == IMPULSE_ATTR_TYPE_FLOAT32) {
            attr_f32 = static_cast<const float*>(raw_data);
        }
    }
    size_t attr_count = attr_f32 ? (attr_bytes / sizeof(float)) : 0;
    std::vector<double> best_val(K, -1e30);

    for (uint32_t u = 0; u < N; ++u) {
        double val = (u < attr_count) ? static_cast<double>(attr_f32[u]) : 0.0;
        auto lanes = res.attributionFor(u);
        for (uint32_t b : lanes) {
            if (b < K) {
                if (best_id[b] == -1 || val > best_val[b] || (val == best_val[b] && static_cast<int64_t>(u) < best_id[b])) {
                    best_val[b] = val;
                    best_id[b] = static_cast<int64_t>(u);
                }
            }
        }
    }
    return best_id;
}

std::vector<std::string> MultiLaneTraversal::argMinKeysPerLane(const std::string& node_attribute) const {
    auto ids = argMinNodesPerLane(node_attribute);
    std::vector<std::string> keys(ids.size());
    for (size_t i = 0; i < ids.size(); ++i) {
        keys[i] = (ids[i] < 0) ? "" : resolve_dense_to_key(snapshot_, current_domain_id_, static_cast<uint32_t>(ids[i]));
    }
    return keys;
}

std::vector<std::string> MultiLaneTraversal::argMaxKeysPerLane(const std::string& node_attribute) const {
    auto ids = argMaxNodesPerLane(node_attribute);
    std::vector<std::string> keys(ids.size());
    for (size_t i = 0; i < ids.size(); ++i) {
        keys[i] = (ids[i] < 0) ? "" : resolve_dense_to_key(snapshot_, current_domain_id_, static_cast<uint32_t>(ids[i]));
    }
    return keys;
}

std::vector<bool> MultiLaneTraversal::checkAllNodesReachedPerLane() const {
    auto res = collect();
    uint32_t K = res.laneCount();
    uint32_t N = res.domainNodeCount();
    std::vector<bool> all_reached(K, N > 0);
    for (uint32_t u = 0; u < N; ++u) {
        auto lanes = res.attributionFor(u);
        std::vector<bool> u_has(K, false);
        for (uint32_t b : lanes) {
            if (b < K) u_has[b] = true;
        }
        for (uint32_t b = 0; b < K; ++b) {
            if (!u_has[b]) all_reached[b] = false;
        }
    }
    return all_reached;
}

SteinerSubgraph MultiLaneTraversal::toSteinerSubgraph(uint8_t min_concept_quorum) const {
    auto res = collect();
    SteinerSubgraph sg;
    sg.bridge_nodes = res.nodesWithQuorum(min_concept_quorum);
    sg.all_nodes = res.nodesWithQuorum(1);

    uint32_t N = res.domainNodeCount();
    if (!snapshot_ || N == 0) return sg;

    std::vector<uint64_t> node_mask(N, 0ULL);
    for (uint32_t u = 0; u < N; ++u) {
        node_mask[u] = res.laneMaskFor(u);
    }
    if (mode_ == InitMode::SEEDS || mode_ == InitMode::OUTAGES) {
        for (size_t b = 0; b < seeds_.size() && b < 64; ++b) {
            uint64_t u = seeds_[b];
            if (u < N) node_mask[u] |= (1ULL << b);
        }
    } else if (mode_ == InitMode::GROUPS) {
        for (const auto& gp : group_pairs_) {
            if (gp.first < N && gp.second < 64) {
                node_mask[gp.first] |= (1ULL << gp.second);
            }
        }
    }

    std::vector<std::pair<uint16_t, std::string>> rels_to_scan;
    auto add_rel = [&](const std::string& rname) {
        if (rname.empty()) return;
        uint16_t rid = resolve_relation_id_by_name(snapshot_, current_domain_id_, rname);
        for (const auto& p : rels_to_scan) {
            if (p.first == rid) return;
        }
        rels_to_scan.emplace_back(rid, rname);
    };

    for (const auto& step : steps_) {
        if (step.kind == LaneStep::Kind::OUT || step.kind == LaneStep::Kind::IN ||
            step.kind == LaneStep::Kind::BOTH || step.kind == LaneStep::Kind::REPEAT ||
            step.kind == LaneStep::Kind::REPEAT_UNTIL_STABLE) {
            add_rel(step.str_arg);
        } else if (step.kind == LaneStep::Kind::OUT_MULTI || step.kind == LaneStep::Kind::IN_MULTI ||
                   step.kind == LaneStep::Kind::BOTH_MULTI) {
            for (const auto& rn : step.rel_names) add_rel(rn);
        }
    }
    if (rels_to_scan.empty()) {
        uint16_t rcnt = impulse_snapshot_relation_count(snapshot_);
        for (uint16_t r = 0; r < rcnt; ++r) {
            rels_to_scan.emplace_back(r, "rel_" + std::to_string(r));
        }
    }

    for (const auto& rp : rels_to_scan) {
        uint16_t rel_id = rp.first;
        const std::string& rel_name = rp.second;
        const uint32_t* offsets = nullptr;
        const uint32_t* targets = nullptr;
        uint64_t node_cnt = 0;
        uint64_t edge_cnt = 0;
        if (impulse_snapshot_get_relation_buffers(snapshot_, rel_id, &offsets, &targets, &node_cnt, &edge_cnt) != IMPULSE_OK ||
            offsets == nullptr || targets == nullptr || node_cnt == 0) {
            continue;
        }
        uint32_t max_u = static_cast<uint32_t>(std::min<uint64_t>(N, node_cnt));
        for (uint32_t u = 0; u < max_u; ++u) {
            uint64_t mu = node_mask[u];
            if (mu == 0ULL) continue;
            uint64_t start = offsets[u];
            uint64_t end = offsets[u + 1];
            for (uint64_t e = start; e < end && e < edge_cnt; ++e) {
                uint32_t v = targets[e];
                if (v < N && node_mask[v] != 0ULL) {
                    uint64_t mv = node_mask[v];
                    uint64_t emask = (mu & mv) != 0ULL ? (mu & mv) : (mu | mv);
                    sg.edges.push_back(SteinerEdge{u, v, rel_id, emask});
                    sg.triples.push_back(SteinerTripleKey{
                        resolve_dense_to_key(snapshot_, current_domain_id_, u),
                        rel_name,
                        resolve_dense_to_key(snapshot_, current_domain_id_, v),
                        emask
                    });
                }
            }
        }
    }

    return sg;
}

// --- MultiLaneBuilder Implementation ---

MultiLaneTraversal MultiLaneBuilder::fromSeeds(std::vector<uint64_t> node_ids) const {
    std::vector<std::string> keys;
    keys.reserve(node_ids.size());
    for (uint64_t id : node_ids) {
        keys.push_back(resolve_dense_to_key(snapshot_, domain_id_, static_cast<uint32_t>(id)));
    }
    return MultiLaneTraversal(snapshot_, domain_id_, MultiLaneTraversal::InitMode::SEEDS, std::move(node_ids), std::move(keys));
}

MultiLaneTraversal MultiLaneBuilder::fromKeys(const std::vector<std::string>& keys) const {
    std::vector<uint64_t> seeds;
    seeds.reserve(keys.size());
    for (const auto& k : keys) {
        seeds.push_back(resolve_key_to_dense(snapshot_, domain_id_, k));
    }
    return MultiLaneTraversal(snapshot_, domain_id_, MultiLaneTraversal::InitMode::SEEDS, std::move(seeds), keys);
}

MultiLaneTraversal MultiLaneBuilder::fromGroups(const std::vector<std::pair<std::string, std::vector<uint64_t>>>& concept_groups) const {
    std::vector<uint64_t> dummy_seeds(concept_groups.size(), 0);
    std::vector<std::string> group_names;
    std::vector<std::pair<uint32_t, uint32_t>> pairs;
    for (size_t g = 0; g < concept_groups.size(); ++g) {
        group_names.push_back(concept_groups[g].first);
        for (uint64_t u : concept_groups[g].second) {
            pairs.emplace_back(static_cast<uint32_t>(u), static_cast<uint32_t>(g));
        }
    }
    return MultiLaneTraversal(snapshot_, domain_id_, MultiLaneTraversal::InitMode::GROUPS,
                              std::move(dummy_seeds), std::move(group_names), std::move(pairs));
}

MultiLaneTraversal MultiLaneBuilder::fromConceptVectors(
    const std::string& index_or_attr,
    const std::vector<std::pair<std::string, std::vector<float>>>& concept_vectors,
    uint16_t hits_per_concept) const
{
    std::vector<std::pair<std::string, std::vector<uint64_t>>> groups;
    DomainView dv(snapshot_, domain_id_);
    for (const auto& cv : concept_vectors) {
        auto hits = dv.vectorSearch(index_or_attr, cv.second, hits_per_concept).collect();
        groups.emplace_back(cv.first, std::move(hits));
    }
    return fromGroups(groups);
}

MultiLaneTraversal MultiLaneBuilder::fromOutages(
    const std::string& relation,
    const std::vector<std::vector<uint32_t>>& tripped_edge_ids_per_lane) const
{
    uint16_t rel_id = resolve_relation_id_by_name(snapshot_, domain_id_, relation);
    std::vector<uint64_t> seeds(tripped_edge_ids_per_lane.size(), 0);
    return MultiLaneTraversal(snapshot_, domain_id_, MultiLaneTraversal::InitMode::OUTAGES,
                              std::move(seeds), {}, {}, rel_id, tripped_edge_ids_per_lane);
}


} // namespace impulse::vm
