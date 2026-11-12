/**
 * @file impulse_vm_fluent_multilane.cpp
 * @brief Bit-sliced Multi-Lane Graph Traversal (MultiLaneResult, MultiLaneTraversal, MultiLaneBuilder).
 */

#include "impulse_vm_fluent_internal.hpp"
#include "impulse_vm_internal.hpp"

namespace impulse::vm {

MultiLaneResult::MultiLaneResult(const impulse_snapshot_t* snapshot,
                                 uint16_t domain_id,
                                 uint32_t domain_node_count,
                                 uint32_t lane_count,
                                 std::vector<std::string> lane_keys,
                                 std::vector<uint64_t> words_per_node,
                                 std::vector<float> projected_values,
                                 bool has_projected_values)
    : snapshot_(snapshot),
      domain_id_(domain_id),
      domain_node_count_(domain_node_count),
      lane_count_(lane_count),
      words_per_lane_((lane_count + 63U) / 64U),
      lane_keys_(std::move(lane_keys)),
      words_per_node_(std::move(words_per_node)),
      projected_values_(std::move(projected_values)),
      has_projected_values_(has_projected_values) {}

std::vector<std::pair<uint32_t, float>> MultiLaneResult::nodeValues() const {
    std::vector<std::pair<uint32_t, float>> out;
    if (words_per_lane_ == 0 || lane_count_ == 0) return out;
    for (uint32_t u = 0; u < domain_node_count_; ++u) {
        if (lanePopcountFor(u) > 0) {
            out.emplace_back(u, valueFor(u));
        }
    }
    return out;
}

std::vector<std::pair<std::string, float>> MultiLaneResult::keyValues() const {
    std::vector<std::pair<std::string, float>> out;
    if (words_per_lane_ == 0 || lane_count_ == 0) return out;
    for (uint32_t u = 0; u < domain_node_count_; ++u) {
        if (lanePopcountFor(u) > 0) {
            out.emplace_back(resolve_dense_to_key(snapshot_, domain_id_, u), valueFor(u));
        }
    }
    return out;
}

std::vector<std::pair<uint32_t, float>> MultiLaneResult::nodeValuesForLane(uint32_t lane) const {
    std::vector<std::pair<uint32_t, float>> out;
    if (lane >= lane_count_ || words_per_lane_ == 0) return out;
    uint32_t w = lane / 64U;
    uint32_t b = lane % 64U;
    uint64_t mask = (1ULL << b);
    for (uint32_t u = 0; u < domain_node_count_; ++u) {
        const uint64_t* row = &words_per_node_[static_cast<size_t>(u) * words_per_lane_];
        if ((row[w] & mask) != 0ULL) {
            out.emplace_back(u, valueFor(u));
        }
    }
    return out;
}

std::vector<std::pair<uint32_t, float>> MultiLaneResult::nodeValuesWithQuorum(uint32_t min_lanes) const {
    std::vector<std::pair<uint32_t, float>> out;
    if (words_per_lane_ == 0 || lane_count_ == 0) return out;
    for (uint32_t u = 0; u < domain_node_count_; ++u) {
        if (lanePopcountFor(u) >= min_lanes) {
            out.emplace_back(u, valueFor(u));
        }
    }
    return out;
}

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
      initial_domain_id_(domain_id),
      current_domain_id_(domain_id),
      mode_(mode),
      seeds_(std::move(seeds)),
      lane_keys_(std::move(lane_keys)),
      group_pairs_(std::move(group_pairs)),
      outage_rel_id_(outage_rel_id),
      tripped_edges_per_lane_(std::move(tripped_edges_per_lane)) {
    if (mode_ == InitMode::GROUPS && seeds_.empty() && !group_pairs_.empty()) {
        uint32_t max_g = 0;
        for (const auto& gp : group_pairs_) {
            if (gp.second > max_g) max_g = gp.second;
        }
        seeds_.assign(static_cast<size_t>(max_g) + 1, 0ULL);
    }
}

MultiLaneTraversal& MultiLaneTraversal::out(const std::string& relation) {
    uint16_t rid = resolve_relation_id_by_name(snapshot_, current_domain_id_, relation);
    current_domain_id_ = advance_domain_id(snapshot_, current_domain_id_, rid, false);
    LaneStep s{};
    s.kind = LaneStep::Kind::OUT;
    s.str_arg = relation;
    steps_.push_back(std::move(s));
    return *this;
}

MultiLaneTraversal& MultiLaneTraversal::out(const std::string& relation, const std::string& edge_cel_predicate) {
    return outPredicate(relation, edge_cel_predicate);
}

MultiLaneTraversal& MultiLaneTraversal::outPredicate(const std::string& relation, const std::string& edge_cel_predicate) {
    uint16_t rid = resolve_relation_id_by_name(snapshot_, current_domain_id_, relation);
    current_domain_id_ = advance_domain_id(snapshot_, current_domain_id_, rid, false);
    LaneStep s{};
    s.kind = LaneStep::Kind::OUT_PREDICATE;
    s.str_arg = relation;
    s.str_arg2 = edge_cel_predicate;
    steps_.push_back(std::move(s));
    return *this;
}

MultiLaneTraversal& MultiLaneTraversal::out(const std::vector<std::string>& relations) {
    if (relations.size() == 1) return out(relations[0]);
    if (!relations.empty()) {
        uint16_t rid = resolve_relation_id_by_name(snapshot_, current_domain_id_, relations[0]);
        current_domain_id_ = advance_domain_id(snapshot_, current_domain_id_, rid, false);
    }
    LaneStep s{};
    s.kind = LaneStep::Kind::OUT_MULTI;
    s.rel_names = relations;
    steps_.push_back(std::move(s));
    return *this;
}

MultiLaneTraversal& MultiLaneTraversal::in(const std::string& relation) {
    uint16_t rid = resolve_relation_id_by_name(snapshot_, current_domain_id_, relation);
    current_domain_id_ = advance_domain_id(snapshot_, current_domain_id_, rid, true);
    LaneStep s{};
    s.kind = LaneStep::Kind::IN;
    s.str_arg = relation;
    steps_.push_back(std::move(s));
    return *this;
}

MultiLaneTraversal& MultiLaneTraversal::in(const std::string& relation, const std::string& edge_cel_predicate) {
    return inPredicate(relation, edge_cel_predicate);
}

MultiLaneTraversal& MultiLaneTraversal::inPredicate(const std::string& relation, const std::string& edge_cel_predicate) {
    uint16_t rid = resolve_relation_id_by_name(snapshot_, current_domain_id_, relation);
    current_domain_id_ = advance_domain_id(snapshot_, current_domain_id_, rid, true);
    LaneStep s{};
    s.kind = LaneStep::Kind::IN_PREDICATE;
    s.str_arg = relation;
    s.str_arg2 = edge_cel_predicate;
    steps_.push_back(std::move(s));
    return *this;
}

MultiLaneTraversal& MultiLaneTraversal::in(const std::vector<std::string>& relations) {
    if (relations.size() == 1) return in(relations[0]);
    if (!relations.empty()) {
        uint16_t rid = resolve_relation_id_by_name(snapshot_, current_domain_id_, relations[0]);
        current_domain_id_ = advance_domain_id(snapshot_, current_domain_id_, rid, true);
    }
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
    uint16_t reps = static_cast<uint16_t>(std::max(0, iterations));
    if (reps > 0) {
        uint16_t rid = resolve_relation_id_by_name(snapshot_, current_domain_id_, relation);
        for (uint16_t it = 0; it < reps; ++it) {
            current_domain_id_ = advance_domain_id(snapshot_, current_domain_id_, rid, false);
        }
    }
    LaneStep s{};
    s.kind = LaneStep::Kind::REPEAT;
    s.str_arg = relation;
    s.u16_arg = reps;
    steps_.push_back(std::move(s));
    return *this;
}

MultiLaneTraversal& MultiLaneTraversal::repeatUntilStable(const std::string& relation, uint32_t max_hops) {
    LaneStep s{};
    s.kind = LaneStep::Kind::REPEAT_UNTIL_STABLE;
    s.str_arg = relation;
    s.u32_arg = max_hops;
    steps_.push_back(std::move(s));
    return *this;
}

MultiLaneTraversal& MultiLaneTraversal::unionWith(const MultiLaneTraversal& other) {
    auto ores = other.collect();
    LaneStep s{};
    s.kind = LaneStep::Kind::SET_UNION;
    s.other_words = ores.rawWordsPerNode();
    s.other_words_per_lane = ores.wordsPerLane();
    s.other_node_count = ores.domainNodeCount();
    steps_.push_back(std::move(s));
    return *this;
}

MultiLaneTraversal& MultiLaneTraversal::intersect(const MultiLaneTraversal& other) {
    auto ores = other.collect();
    LaneStep s{};
    s.kind = LaneStep::Kind::SET_INTERSECT;
    s.other_words = ores.rawWordsPerNode();
    s.other_words_per_lane = ores.wordsPerLane();
    s.other_node_count = ores.domainNodeCount();
    steps_.push_back(std::move(s));
    return *this;
}

MultiLaneTraversal& MultiLaneTraversal::difference(const MultiLaneTraversal& other) {
    auto ores = other.collect();
    LaneStep s{};
    s.kind = LaneStep::Kind::SET_DIFFERENCE;
    s.other_words = ores.rawWordsPerNode();
    s.other_words_per_lane = ores.wordsPerLane();
    s.other_node_count = ores.domainNodeCount();
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

MultiLaneTraversal& MultiLaneTraversal::whereQuorum(uint8_t min_lanes, uint64_t required_mask, uint64_t forbidden_mask) {
    LaneStep s{};
    s.kind = LaneStep::Kind::WHERE_QUORUM_MASK;
    s.u8_arg = min_lanes;
    s.mask_req = required_mask;
    s.mask_forbid = forbidden_mask;
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
    uint32_t final_N = get_domain_node_count(snapshot_, current_domain_id_);
    uint32_t K = static_cast<uint32_t>(seeds_.size());
    uint32_t words_per_lane = (K + 63U) / 64U;
    std::vector<uint64_t> words_per_node(static_cast<size_t>(final_N) * words_per_lane, 0ULL);
    std::vector<float> proj_values(final_N, 0.0f);
    bool has_projected = false;

    if (!snapshot_ || K == 0 || words_per_lane == 0) {
        return MultiLaneResult(snapshot_, current_domain_id_, final_N, K, lane_keys_, std::move(words_per_node));
    }

    impulse_vm_context_t* ctx = impulse_vm_context_create(snapshot_);
    if (!ctx) {
        return MultiLaneResult(snapshot_, current_domain_id_, final_N, K, lane_keys_, std::move(words_per_node));
    }

    uint32_t init_N = get_domain_node_count(snapshot_, initial_domain_id_);
    size_t max_snap_nodes = std::max<size_t>(impulse_snapshot_max_node_count(snapshot_), std::max<size_t>(init_N, final_N));

    // Maintain one live VM lane-frontier handle per 64-lane batch so cross-lane and hop steps interleave in exact order
    std::vector<int> batch_handles(words_per_lane, -1);

    auto rebuild_active_words = [](VmLaneFrontier& lf) {
        size_t n = lf.lanes.size();
        size_t wcnt = (n + 63) / 64;
        lf.active_words.assign(wcnt, 0ULL);
        for (size_t u = 0; u < n; ++u) {
            uint64_t m = lf.lanes[u] & lf.active_mask;
            lf.lanes[u] = m;
            if (m != 0ULL) {
                lf.active_words[u >> 6] |= (1ULL << (u & 63));
            }
        }
    };

    for (uint32_t batch_idx = 0; batch_idx < words_per_lane; ++batch_idx) {
        uint32_t lane_start = batch_idx * 64U;
        uint32_t batch_k = std::min(64U, K - lane_start);

        impulse_vm_state_t state{};
        state.query_context = ctx;

        std::vector<uint32_t> inline_u32;
        std::vector<uint32_t> group_u32;
        if (mode_ == InitMode::SEEDS || mode_ == InitMode::OUTAGES) {
            for (uint32_t b = 0; b < batch_k; ++b) {
                uint64_t su = seeds_[lane_start + b];
                inline_u32.push_back((su < init_N) ? static_cast<uint32_t>(su) : UINT32_MAX);
            }
        } else if (mode_ == InitMode::GROUPS) {
            for (const auto& gp : group_pairs_) {
                if (gp.first < init_N && gp.second >= lane_start && gp.second < lane_start + batch_k) {
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

        std::vector<impulse_instruction_t> init_prog;
        uint32_t seed_u32_count = static_cast<uint32_t>(group_offset_bytes / sizeof(uint32_t));
        init_prog.push_back(impulse_instruction_t{
            OP_LOAD_INLINE_ARRAY, 1, 1,
            static_cast<uint32_t>(0 | (seed_u32_count << 16))
        });

        if (mode_ == InitMode::GROUPS) {
            init_prog.push_back(impulse_instruction_t{
                OP_LOAD_INLINE_ARRAY, 1, 2,
                static_cast<uint32_t>(static_cast<uint16_t>(group_offset_bytes) |
                                      (static_cast<uint32_t>(group_u32_count) << 16))
            });
            init_prog.push_back(impulse_instruction_t{
                OP_LANE_INIT_GROUPS, 0, 0,
                static_cast<uint32_t>(1 | (2 << 8) | (batch_k << 16))
            });
        } else {
            init_prog.push_back(impulse_instruction_t{
                OP_LANE_INIT_SEEDS, 0, 0, 1
            });
        }

        if (mode_ == InitMode::OUTAGES && outage_pairs_u32_count > 0) {
            init_prog.push_back(impulse_instruction_t{
                OP_LOAD_INLINE_ARRAY, 0, 2,
                static_cast<uint32_t>(static_cast<uint16_t>(outage_pairs_offset_bytes) |
                                      (static_cast<uint32_t>(outage_pairs_u32_count / 2) << 16))
            });
            init_prog.push_back(impulse_instruction_t{
                OP_LANE_SET_EDGE_MASK, 1, 0,
                static_cast<uint32_t>(0 | (2 << 8) | (static_cast<uint32_t>(outage_rel_id_) << 16))
            });
        }
        init_prog.push_back(impulse_instruction_t{OP_HALT, 0, 0, 0});

        if (impulse_vm_execute(init_prog.data(), init_prog.size(), &state, 0) == IMPULSE_VM_OK &&
            state.register_types[0] == TYPE_LANE_FRONTIER) {
            int h_lf = static_cast<int>(state.registers[0]);
            batch_handles[batch_idx] = h_lf;
            auto& lf = ctx->lane_frontiers[h_lf];
            if (lf.lanes.size() < max_snap_nodes) {
                lf.lanes.resize(max_snap_nodes, 0ULL);
            }
            for (size_t u = init_N; u < lf.lanes.size(); ++u) {
                lf.lanes[u] = 0ULL;
            }
            rebuild_active_words(lf);
        }
    }

    auto exec_batch_vm_step = [&](uint32_t batch_idx, const impulse_instruction_t* insts, size_t cnt) {
        int old_h = batch_handles[batch_idx];
        if (old_h < 0) return;
        impulse_vm_state_t st{};
        st.query_context = ctx;
        st.registers[0] = static_cast<uint64_t>(old_h);
        st.register_types[0] = TYPE_LANE_FRONTIER;
        st.pc = 0;
        if (impulse_vm_execute(insts, cnt, &st, 0) == IMPULSE_VM_OK &&
            st.register_types[0] == TYPE_LANE_FRONTIER) {
            int new_h = static_cast<int>(st.registers[0]);
            batch_handles[batch_idx] = new_h;
            if (old_h != new_h) {
                impulse_vm_context_release_lane_frontier(ctx, static_cast<size_t>(old_h));
            }
        }
    };

    uint16_t step_dom = initial_domain_id_;
    for (const auto& step : steps_) {
        uint32_t step_N = get_domain_node_count(snapshot_, step_dom);

        if (step.kind == LaneStep::Kind::OUT) {
            uint16_t rel_id = resolve_relation_id_by_name(snapshot_, step_dom, step.str_arg);
            impulse_instruction_t insts[2] = {
                {OP_LANE_CSR_WALK, 0, 0, static_cast<uint32_t>(0 | (static_cast<uint32_t>(rel_id) << 16))},
                {OP_HALT, 0, 0, 0}
            };
            for (uint32_t b = 0; b < words_per_lane; ++b) exec_batch_vm_step(b, insts, 2);
            step_dom = advance_domain_id(snapshot_, step_dom, rel_id, false);
        } else if (step.kind == LaneStep::Kind::IN) {
            uint16_t rel_id = resolve_relation_id_by_name(snapshot_, step_dom, step.str_arg);
            impulse_instruction_t insts[2] = {
                {OP_LANE_CSC_WALK, 0, 0, static_cast<uint32_t>(0 | (static_cast<uint32_t>(rel_id) << 16))},
                {OP_HALT, 0, 0, 0}
            };
            for (uint32_t b = 0; b < words_per_lane; ++b) exec_batch_vm_step(b, insts, 2);
            step_dom = advance_domain_id(snapshot_, step_dom, rel_id, true);
        } else if (step.kind == LaneStep::Kind::BOTH || step.kind == LaneStep::Kind::OUT_MULTI ||
                   step.kind == LaneStep::Kind::IN_MULTI || step.kind == LaneStep::Kind::BOTH_MULTI) {
            std::vector<std::pair<uint16_t, bool>> branches;
            if (step.kind == LaneStep::Kind::BOTH) {
                uint16_t rid = resolve_relation_id_by_name(snapshot_, step_dom, step.str_arg);
                branches.emplace_back(rid, false);
                branches.emplace_back(rid, true);
            } else {
                for (const auto& rname : step.rel_names) {
                    uint16_t rid = resolve_relation_id_by_name(snapshot_, step_dom, rname);
                    if (step.kind == LaneStep::Kind::OUT_MULTI || step.kind == LaneStep::Kind::BOTH_MULTI) {
                        branches.emplace_back(rid, false);
                    }
                    if (step.kind == LaneStep::Kind::IN_MULTI || step.kind == LaneStep::Kind::BOTH_MULTI) {
                        branches.emplace_back(rid, true);
                    }
                }
            }
            for (uint32_t b = 0; b < words_per_lane; ++b) {
                int h_cur = batch_handles[b];
                if (h_cur < 0) continue;
                if (branches.empty()) {
                    auto& lf = ctx->lane_frontiers[h_cur];
                    std::fill(lf.lanes.begin(), lf.lanes.end(), 0ULL);
                    std::fill(lf.active_words.begin(), lf.active_words.end(), 0ULL);
                    continue;
                }
                impulse_vm_state_t st{};
                st.query_context = ctx;
                st.registers[0] = static_cast<uint64_t>(h_cur);
                st.register_types[0] = TYPE_LANE_FRONTIER;

                impulse_instruction_t w0[2] = {
                    {static_cast<uint8_t>(branches[0].second ? OP_LANE_CSC_WALK : OP_LANE_CSR_WALK), 0, 3,
                     static_cast<uint32_t>(0 | (static_cast<uint32_t>(branches[0].first) << 16))},
                    {OP_HALT, 0, 0, 0}
                };
                st.pc = 0;
                if (impulse_vm_execute(w0, 2, &st, 0) != IMPULSE_VM_OK || st.register_types[3] != TYPE_LANE_FRONTIER) {
                    continue;
                }
                int h_acc = static_cast<int>(st.registers[3]);
                for (size_t bi = 1; bi < branches.size(); ++bi) {
                    impulse_instruction_t wi[2] = {
                        {static_cast<uint8_t>(branches[bi].second ? OP_LANE_CSC_WALK : OP_LANE_CSR_WALK), 0, 4,
                         static_cast<uint32_t>(0 | (static_cast<uint32_t>(branches[bi].first) << 16))},
                        {OP_HALT, 0, 0, 0}
                    };
                    st.pc = 0;
                    if (impulse_vm_execute(wi, 2, &st, 0) == IMPULSE_VM_OK && st.register_types[4] == TYPE_LANE_FRONTIER) {
                        int h_br = static_cast<int>(st.registers[4]);
                        auto& lf_acc = ctx->lane_frontiers[h_acc];
                        const auto& lf_br = ctx->lane_frontiers[h_br];
                        if (lf_acc.lanes.size() < lf_br.lanes.size()) {
                            lf_acc.lanes.resize(lf_br.lanes.size(), 0ULL);
                        }
                        for (size_t u = 0; u < lf_br.lanes.size(); ++u) {
                            lf_acc.lanes[u] |= (lf_br.lanes[u] & lf_acc.active_mask);
                        }
                        rebuild_active_words(lf_acc);
                        impulse_vm_context_release_lane_frontier(ctx, static_cast<size_t>(h_br));
                        st.registers[4] = 0;
                        st.register_types[4] = TYPE_NULL;
                    }
                }
                batch_handles[b] = h_acc;
                if (h_cur != h_acc) {
                    impulse_vm_context_release_lane_frontier(ctx, static_cast<size_t>(h_cur));
                }
            }
            if (!branches.empty()) {
                if (step.kind == LaneStep::Kind::OUT_MULTI) {
                    step_dom = advance_domain_id(snapshot_, step_dom, branches[0].first, false);
                } else if (step.kind == LaneStep::Kind::IN_MULTI) {
                    step_dom = advance_domain_id(snapshot_, step_dom, branches[0].first, true);
                }
            }
        } else if (step.kind == LaneStep::Kind::OUT_PREDICATE || step.kind == LaneStep::Kind::IN_PREDICATE) {
            bool is_rev = (step.kind == LaneStep::Kind::IN_PREDICATE);
            uint16_t rel_id = resolve_relation_id_by_name(snapshot_, step_dom, step.str_arg);
            uint16_t next_dom = advance_domain_id(snapshot_, step_dom, rel_id, is_rev);
            uint32_t next_N = get_domain_node_count(snapshot_, next_dom);

            impulse::cel::Parser parser(step.str_arg2);
            auto ast = parser.parse_expression();
            auto opt = ast ? impulse::cel::AstOptimizer::optimize(ast) : nullptr;

            for (uint32_t b = 0; b < words_per_lane; ++b) {
                int h_cur = batch_handles[b];
                if (h_cur < 0) continue;
                auto& lf_src = ctx->lane_frontiers[h_cur];
                std::vector<uint64_t> dst_lanes(std::max<size_t>(lf_src.lanes.size(), next_N), 0ULL);

                auto eval_lane_rel = [&](auto& self, uint16_t rid, uint32_t depth) -> void {
                    if (depth > 8 || rid >= ctx->slots.size()) return;
                    const auto& slot = ctx->slots[rid];
                    if (slot.is_virtual) {
                        for (uint16_t cid : slot.virtual_children) {
                            self(self, cid, depth + 1);
                        }
                        return;
                    }
                    auto edge_attrs = build_relation_attr_cache(snapshot_, rid);
                    auto test_edge = [&](uint64_t edge_idx) -> bool {
                        if (!opt) return false;
                        return impulse::cel::CelKleeneEvaluator::eval_filter_bool(
                            opt, [&](const std::string& aname) { return edge_attrs.resolve(edge_idx, aname); });
                    };

                    if (!is_rev) {
                        if (slot.is_raw_coo && slot.offsets_ptr && slot.targets_ptr) {
                            for (uint64_t e = 0; e < slot.edge_count; ++e) {
                                uint64_t u = slot.get_raw_coo_src(e);
                                if (u >= step_N || u >= lf_src.lanes.size()) continue;
                                uint64_t mu = lf_src.lanes[u] & lf_src.active_mask;
                                if (mu != 0ULL && test_edge(e)) {
                                    uint64_t v = slot.get_csr_target(e);
                                    if (v < next_N && v < dst_lanes.size()) dst_lanes[v] |= mu;
                                }
                            }
                        } else if (slot.offsets_ptr && slot.targets_ptr) {
                            uint64_t lim_u = std::min<uint64_t>({static_cast<uint64_t>(step_N), slot.node_count, lf_src.lanes.size()});
                            for (uint64_t u = 0; u < lim_u; ++u) {
                                uint64_t mu = lf_src.lanes[u] & lf_src.active_mask;
                                if (mu == 0ULL) continue;
                                uint64_t start = slot.get_csr_offset(u);
                                uint64_t end = std::min<uint64_t>(slot.get_csr_offset(u + 1), slot.edge_count);
                                for (uint64_t e = start; e < end; ++e) {
                                    if (test_edge(e)) {
                                        uint64_t v = slot.get_csr_target(e);
                                        if (v < next_N && v < dst_lanes.size()) dst_lanes[v] |= mu;
                                    }
                                }
                            }
                        } else if (!slot.offsets_ptr && slot.targets_ptr) {
                            uint64_t sent = slot.csr_sentinel();
                            uint64_t edge_rank = 0;
                            for (uint64_t u = 0; u < slot.node_count; ++u) {
                                uint64_t v = slot.get_csr_target(u);
                                if (v != sent) {
                                    uint64_t eidx = (slot.edge_count == slot.node_count) ? u : edge_rank;
                                    edge_rank++;
                                    if (u < step_N && u < lf_src.lanes.size()) {
                                        uint64_t mu = lf_src.lanes[u] & lf_src.active_mask;
                                        if (mu != 0ULL && v < next_N && v < dst_lanes.size() && test_edge(eidx)) {
                                            dst_lanes[v] |= mu;
                                        }
                                    }
                                }
                            }
                        }
                    } else {
                        if (slot.csc_offsets_ptr && slot.csc_targets_ptr) {
                            uint64_t lim_v = std::min<uint64_t>({static_cast<uint64_t>(step_N), slot.csc_node_count, lf_src.lanes.size()});
                            for (uint64_t v = 0; v < lim_v; ++v) {
                                uint64_t mv = lf_src.lanes[v] & lf_src.active_mask;
                                if (mv == 0ULL) continue;
                                uint64_t start = slot.get_csc_offset(v);
                                uint64_t end = std::min<uint64_t>(slot.get_csc_offset(v + 1), slot.edge_count);
                                for (uint64_t ce = start; ce < end; ++ce) {
                                    uint64_t csr_e = (ce < slot.csc_to_csr_edge_idx.size()) ? slot.csc_to_csr_edge_idx[ce] : ce;
                                    if (test_edge(csr_e)) {
                                        uint64_t u = slot.get_csc_target(ce);
                                        if (u < next_N && u < dst_lanes.size()) dst_lanes[u] |= mv;
                                    }
                                }
                            }
                        } else if (slot.is_raw_coo && slot.offsets_ptr && slot.targets_ptr) {
                            for (uint64_t e = 0; e < slot.edge_count; ++e) {
                                uint64_t v = slot.get_csr_target(e);
                                if (v >= step_N || v >= lf_src.lanes.size()) continue;
                                uint64_t mv = lf_src.lanes[v] & lf_src.active_mask;
                                if (mv != 0ULL && test_edge(e)) {
                                    uint64_t u = slot.get_raw_coo_src(e);
                                    if (u < next_N && u < dst_lanes.size()) dst_lanes[u] |= mv;
                                }
                            }
                        } else if (slot.offsets_ptr && slot.targets_ptr) {
                            for (uint64_t u = 0; u < slot.node_count && u < next_N && u < dst_lanes.size(); ++u) {
                                uint64_t start = slot.get_csr_offset(u);
                                uint64_t end = std::min<uint64_t>(slot.get_csr_offset(u + 1), slot.edge_count);
                                for (uint64_t e = start; e < end; ++e) {
                                    uint64_t v = slot.get_csr_target(e);
                                    if (v >= step_N || v >= lf_src.lanes.size()) continue;
                                    uint64_t mv = lf_src.lanes[v] & lf_src.active_mask;
                                    if (mv != 0ULL && test_edge(e)) {
                                        dst_lanes[u] |= mv;
                                    }
                                }
                            }
                        } else if (!slot.offsets_ptr && slot.targets_ptr) {
                            uint64_t sent = slot.csr_sentinel();
                            uint64_t edge_rank = 0;
                            for (uint64_t u = 0; u < slot.node_count; ++u) {
                                uint64_t v = slot.get_csr_target(u);
                                if (v != sent) {
                                    uint64_t eidx = (slot.edge_count == slot.node_count) ? u : edge_rank;
                                    edge_rank++;
                                    if (u < next_N && u < dst_lanes.size() && v < step_N && v < lf_src.lanes.size()) {
                                        uint64_t mv = lf_src.lanes[v] & lf_src.active_mask;
                                        if (mv != 0ULL && test_edge(eidx)) {
                                            dst_lanes[u] |= mv;
                                        }
                                    }
                                }
                            }
                        } else if (!slot.csc_offsets_ptr && slot.csc_targets_ptr) {
                            uint64_t sent = slot.csc_sentinel();
                            uint64_t edge_rank = 0;
                            for (uint64_t v = 0; v < slot.csc_node_count; ++v) {
                                uint64_t u = slot.get_csc_target(v);
                                if (u != sent) {
                                    uint64_t eidx = (slot.edge_count == slot.csc_node_count) ? v : edge_rank;
                                    edge_rank++;
                                    if (v < step_N && v < lf_src.lanes.size() && u < next_N && u < dst_lanes.size()) {
                                        uint64_t mv = lf_src.lanes[v] & lf_src.active_mask;
                                        if (mv != 0ULL && test_edge(eidx)) {
                                            dst_lanes[u] |= mv;
                                        }
                                    }
                                }
                            }
                        }
                    }
                };

                eval_lane_rel(eval_lane_rel, rel_id, 0);
                lf_src.lanes = std::move(dst_lanes);
                rebuild_active_words(lf_src);
            }
            step_dom = next_dom;
        } else if (step.kind == LaneStep::Kind::REPEAT) {
            uint16_t rel_id = resolve_relation_id_by_name(snapshot_, step_dom, step.str_arg);
            impulse_instruction_t insts[2] = {
                {OP_LANE_CSR_WALK, 0, 0, static_cast<uint32_t>(0 | (static_cast<uint32_t>(rel_id) << 16))},
                {OP_HALT, 0, 0, 0}
            };
            for (uint16_t it = 0; it < step.u16_arg; ++it) {
                for (uint32_t b = 0; b < words_per_lane; ++b) exec_batch_vm_step(b, insts, 2);
                step_dom = advance_domain_id(snapshot_, step_dom, rel_id, false);
            }
        } else if (step.kind == LaneStep::Kind::REPEAT_UNTIL_STABLE) {
            uint16_t rel_id = resolve_relation_id_by_name(snapshot_, step_dom, step.str_arg);
            ctx->fixpoint_max_hops = step.u32_arg;
            ctx->has_fixpoint_max_hops = true;
            impulse_instruction_t insts[2] = {
                {OP_LANE_CSR_WALK, 0x04, 0, static_cast<uint32_t>(0 | (static_cast<uint32_t>(rel_id) << 16))},
                {OP_HALT, 0, 0, 0}
            };
            for (uint32_t b = 0; b < words_per_lane; ++b) exec_batch_vm_step(b, insts, 2);
            ctx->has_fixpoint_max_hops = false;
            ctx->fixpoint_max_hops = 0;
        } else if (step.kind == LaneStep::Kind::PRUNE_HUBS && words_per_lane == 1) {
            impulse_instruction_t insts[2] = {
                {OP_LANE_PRUNE_MASK, 2, 0,
                 static_cast<uint32_t>(0 | (static_cast<uint32_t>(step.u16_arg) << 8) | (static_cast<uint32_t>(step.u8_arg) << 24))},
                {OP_HALT, 0, 0, 0}
            };
            exec_batch_vm_step(0, insts, 2);
        } else if (step.kind == LaneStep::Kind::SET_UNION || step.kind == LaneStep::Kind::SET_INTERSECT ||
                   step.kind == LaneStep::Kind::SET_DIFFERENCE) {
            for (uint32_t b = 0; b < words_per_lane; ++b) {
                int h_cur = batch_handles[b];
                if (h_cur < 0) continue;
                auto& lf = ctx->lane_frontiers[h_cur];
                if (lf.lanes.size() < step_N) lf.lanes.resize(step_N, 0ULL);
                for (uint32_t u = 0; u < step_N; ++u) {
                    uint64_t ow = (u < step.other_node_count && b < step.other_words_per_lane)
                        ? step.other_words[static_cast<size_t>(u) * step.other_words_per_lane + b]
                        : 0ULL;
                    if (step.kind == LaneStep::Kind::SET_UNION) {
                        lf.lanes[u] = (lf.lanes[u] | ow) & lf.active_mask;
                    } else if (step.kind == LaneStep::Kind::SET_INTERSECT) {
                        lf.lanes[u] = (lf.lanes[u] & ow) & lf.active_mask;
                    } else {
                        lf.lanes[u] = (lf.lanes[u] & ~ow) & lf.active_mask;
                    }
                }
                for (size_t u = step_N; u < lf.lanes.size(); ++u) lf.lanes[u] = 0ULL;
                rebuild_active_words(lf);
            }
        } else if (step.kind == LaneStep::Kind::WHERE_QUORUM) {
            for (uint32_t u = 0; u < step_N; ++u) {
                uint32_t pc = 0;
                for (uint32_t b = 0; b < words_per_lane; ++b) {
                    int h = batch_handles[b];
                    if (h >= 0 && u < ctx->lane_frontiers[h].lanes.size()) {
                        pc += static_cast<uint32_t>(std::popcount(ctx->lane_frontiers[h].lanes[u] & ctx->lane_frontiers[h].active_mask));
                    }
                }
                if (pc < step.u8_arg) {
                    for (uint32_t b = 0; b < words_per_lane; ++b) {
                        int h = batch_handles[b];
                        if (h >= 0 && u < ctx->lane_frontiers[h].lanes.size()) {
                            ctx->lane_frontiers[h].lanes[u] = 0ULL;
                        }
                    }
                }
            }
            for (uint32_t b = 0; b < words_per_lane; ++b) {
                if (batch_handles[b] >= 0) rebuild_active_words(ctx->lane_frontiers[batch_handles[b]]);
            }
        } else if (step.kind == LaneStep::Kind::WHERE_QUORUM_MASK) {
            for (uint32_t u = 0; u < step_N; ++u) {
                uint64_t m0 = 0ULL;
                if (batch_handles[0] >= 0 && u < ctx->lane_frontiers[batch_handles[0]].lanes.size()) {
                    m0 = ctx->lane_frontiers[batch_handles[0]].lanes[u] & ctx->lane_frontiers[batch_handles[0]].active_mask;
                }
                uint32_t pc = static_cast<uint32_t>(std::popcount(m0 & step.mask_req));
                if (pc < step.u8_arg || (m0 & step.mask_forbid) != 0ULL) {
                    for (uint32_t b = 0; b < words_per_lane; ++b) {
                        int h = batch_handles[b];
                        if (h >= 0 && u < ctx->lane_frontiers[h].lanes.size()) {
                            ctx->lane_frontiers[h].lanes[u] = 0ULL;
                        }
                    }
                }
            }
            for (uint32_t b = 0; b < words_per_lane; ++b) {
                if (batch_handles[b] >= 0) rebuild_active_words(ctx->lane_frontiers[batch_handles[b]]);
            }
        } else if (step.kind == LaneStep::Kind::WHERE_MASK) {
            for (uint32_t u = 0; u < step_N; ++u) {
                uint64_t m0 = 0ULL;
                if (batch_handles[0] >= 0 && u < ctx->lane_frontiers[batch_handles[0]].lanes.size()) {
                    m0 = ctx->lane_frontiers[batch_handles[0]].lanes[u] & ctx->lane_frontiers[batch_handles[0]].active_mask;
                }
                if ((m0 & step.mask_req) == 0ULL || (m0 & step.mask_forbid) != 0ULL) {
                    for (uint32_t b = 0; b < words_per_lane; ++b) {
                        int h = batch_handles[b];
                        if (h >= 0 && u < ctx->lane_frontiers[h].lanes.size()) {
                            ctx->lane_frontiers[h].lanes[u] = 0ULL;
                        }
                    }
                }
            }
            for (uint32_t b = 0; b < words_per_lane; ++b) {
                if (batch_handles[b] >= 0) rebuild_active_words(ctx->lane_frontiers[batch_handles[b]]);
            }
        } else if (step.kind == LaneStep::Kind::MUST_HIT_AT_LEAST) {
            for (uint32_t u = 0; u < step_N; ++u) {
                uint32_t hits = 0;
                for (uint32_t lb : step.lane_indices) {
                    uint32_t w = lb / 64U;
                    uint32_t bit = lb % 64U;
                    if (w < words_per_lane && batch_handles[w] >= 0) {
                        const auto& lf = ctx->lane_frontiers[batch_handles[w]];
                        if (u < lf.lanes.size() && (((lf.lanes[u] & lf.active_mask) >> bit) & 1ULL) != 0ULL) {
                            hits++;
                        }
                    }
                }
                if (hits < step.u8_arg) {
                    for (uint32_t b = 0; b < words_per_lane; ++b) {
                        int h = batch_handles[b];
                        if (h >= 0 && u < ctx->lane_frontiers[h].lanes.size()) {
                            ctx->lane_frontiers[h].lanes[u] = 0ULL;
                        }
                    }
                }
            }
            for (uint32_t b = 0; b < words_per_lane; ++b) {
                if (batch_handles[b] >= 0) rebuild_active_words(ctx->lane_frontiers[batch_handles[b]]);
            }
        } else if (step.kind == LaneStep::Kind::MUST_AVOID_ALL) {
            for (uint32_t u = 0; u < step_N; ++u) {
                bool hit_forbid = false;
                for (uint32_t lb : step.lane_indices) {
                    uint32_t w = lb / 64U;
                    uint32_t bit = lb % 64U;
                    if (w < words_per_lane && batch_handles[w] >= 0) {
                        const auto& lf = ctx->lane_frontiers[batch_handles[w]];
                        if (u < lf.lanes.size() && (((lf.lanes[u] & lf.active_mask) >> bit) & 1ULL) != 0ULL) {
                            hit_forbid = true;
                            break;
                        }
                    }
                }
                if (hit_forbid) {
                    for (uint32_t b = 0; b < words_per_lane; ++b) {
                        int h = batch_handles[b];
                        if (h >= 0 && u < ctx->lane_frontiers[h].lanes.size()) {
                            ctx->lane_frontiers[h].lanes[u] = 0ULL;
                        }
                    }
                }
            }
            for (uint32_t b = 0; b < words_per_lane; ++b) {
                if (batch_handles[b] >= 0) rebuild_active_words(ctx->lane_frontiers[batch_handles[b]]);
            }
        } else if (step.kind == LaneStep::Kind::FILTER_CEL || step.kind == LaneStep::Kind::PRUNE_WHERE_CEL ||
                   step.kind == LaneStep::Kind::PROJECT_CEL) {
            impulse::cel::Parser p(step.str_arg);
            auto ast = p.parse_expression();
            if (ast) {
                auto opt_ast = impulse::cel::AstOptimizer::optimize(ast);
                auto dom_attrs = build_domain_attr_cache(snapshot_, step_dom);
                std::vector<uint64_t> row_buf(words_per_lane, 0ULL);
                if (step.kind == LaneStep::Kind::PROJECT_CEL && proj_values.size() < step_N) {
                    proj_values.resize(step_N, 0.0f);
                }
                for (uint32_t u = 0; u < step_N; ++u) {
                    bool is_active = false;
                    for (uint32_t b = 0; b < words_per_lane; ++b) {
                        int h = batch_handles[b];
                        uint64_t wval = (h >= 0 && u < ctx->lane_frontiers[h].lanes.size())
                            ? (ctx->lane_frontiers[h].lanes[u] & ctx->lane_frontiers[h].active_mask)
                            : 0ULL;
                        row_buf[b] = wval;
                        if (wval != 0ULL) is_active = true;
                    }
                    if (!is_active && step.kind != LaneStep::Kind::PROJECT_CEL) continue;

                    auto resolve_fn = [&](const std::string& aname) { return dom_attrs.resolve(u, aname); };
                    if (step.kind == LaneStep::Kind::FILTER_CEL) {
                        if (!impulse::cel::CelKleeneEvaluator::eval_filter_bool(opt_ast, resolve_fn, row_buf.data(), words_per_lane)) {
                            for (uint32_t b = 0; b < words_per_lane; ++b) {
                                int h = batch_handles[b];
                                if (h >= 0 && u < ctx->lane_frontiers[h].lanes.size()) {
                                    ctx->lane_frontiers[h].lanes[u] = 0ULL;
                                }
                            }
                        }
                    } else if (step.kind == LaneStep::Kind::PRUNE_WHERE_CEL) {
                        if (impulse::cel::CelKleeneEvaluator::eval_filter_bool(opt_ast, resolve_fn, row_buf.data(), words_per_lane)) {
                            for (uint32_t b = 0; b < words_per_lane; ++b) {
                                int h = batch_handles[b];
                                if (h >= 0 && u < ctx->lane_frontiers[h].lanes.size()) {
                                    ctx->lane_frontiers[h].lanes[u] = 0ULL;
                                }
                            }
                        }
                    } else if (step.kind == LaneStep::Kind::PROJECT_CEL) {
                        auto cv = impulse::cel::CelKleeneEvaluator::eval(opt_ast, resolve_fn, row_buf.data(), words_per_lane);
                        double dv = 0.0;
                        proj_values[u] = cv.to_double(dv) ? static_cast<float>(dv) : 0.0f;
                        has_projected = true;
                    }
                }
                for (uint32_t b = 0; b < words_per_lane; ++b) {
                    if (batch_handles[b] >= 0) rebuild_active_words(ctx->lane_frontiers[batch_handles[b]]);
                }
            }
        }
    }

    // Materialize final multi-lane words for final_N nodes in step_dom
    final_N = get_domain_node_count(snapshot_, step_dom);
    words_per_node.assign(static_cast<size_t>(final_N) * words_per_lane, 0ULL);
    proj_values.resize(final_N, 0.0f);

    for (uint32_t b = 0; b < words_per_lane; ++b) {
        int h = batch_handles[b];
        if (h < 0) continue;
        const auto& lf = ctx->lane_frontiers[h];
        size_t copy_n = std::min<size_t>(final_N, lf.lanes.size());
        for (size_t u = 0; u < copy_n; ++u) {
            words_per_node[u * words_per_lane + b] = (lf.lanes[u] & lf.active_mask);
        }
        impulse_vm_context_release_lane_frontier(ctx, static_cast<size_t>(h));
    }

    impulse_vm_context_destroy(ctx);
    return MultiLaneResult(snapshot_, step_dom, final_N, K, lane_keys_, std::move(words_per_node), std::move(proj_values), has_projected);
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

std::vector<double> MultiLaneTraversal::reducePerLane(const std::string& node_attribute, ReducerOp op) const {
    auto res = collect();
    uint32_t K = res.laneCount();
    uint32_t N = res.domainNodeCount();
    std::vector<double> out(K, 0.0);
    if (K == 0 || N == 0 || !snapshot_) return out;

    impulse_vm_context_t* ctx = impulse_vm_context_create(snapshot_);
    if (!ctx) return out;

    impulse_vm_state_t state{};
    state.query_context = ctx;

    int h_lf = acquire_lane_frontier(ctx, N, K);
    if (h_lf < 0) {
        impulse_vm_context_destroy(ctx);
        return out;
    }
    auto& lf = ctx->lane_frontiers[h_lf];
    lf.num_lanes = K;
    lf.lanes.assign(N, 0ULL);
    lf.active_mask = (K >= 64) ? ~0ULL : ((1ULL << K) - 1ULL);
    lf.active_words.assign((N + 63) / 64, 0ULL);
    for (uint32_t u = 0; u < N; ++u) {
        uint64_t mask = res.laneMaskFor(u);
        lf.lanes[u] = mask;
        if ((mask & lf.active_mask) != 0ULL) {
            lf.active_words[u >> 6] |= (1ULL << (u & 63));
        }
    }
    state.registers[3] = static_cast<uint64_t>(h_lf);
    state.register_types[3] = TYPE_LANE_FRONTIER;

    uint8_t flags = 0;
    uint8_t attr_or_reg = 0;
    if (res.hasProjectedValues() && node_attribute.empty()) {
        flags = 0x01;
        int h_fvec = impulse_vm_context_acquire_float_vector(ctx);
        if (h_fvec >= 0) {
            for (uint32_t u = 0; u < N; ++u) {
                impulse_vm_context_float_vector_set(ctx, static_cast<size_t>(h_fvec), u, res.valueFor(u));
            }
            state.registers[2] = static_cast<uint64_t>(h_fvec);
            state.register_types[2] = TYPE_FLOAT_VECTOR;
            attr_or_reg = 2;
        }
    } else {
        flags = 0x00;
        attr_or_reg = static_cast<uint8_t>(resolve_attr_or_index_id(snapshot_, node_attribute, current_domain_id_));
    }

    uint16_t red_code = BINARY_OP_ADD;
    if (op == ReducerOp::MIN) red_code = BINARY_OP_MIN;
    else if (op == ReducerOp::MAX) red_code = BINARY_OP_MAX;
    else if (op == ReducerOp::COUNT) red_code = 4u;
    else if (op == ReducerOp::AVG) red_code = BINARY_OP_ADD;

    impulse_instruction_t prog[2] = {
        {OP_LANE_REDUCE_ATTR, flags, 4, static_cast<uint32_t>(3 | (attr_or_reg << 8) | (red_code << 16))},
        {OP_HALT, 0, 0, 0}
    };
    state.pc = 0;
    if (impulse_vm_execute(prog, 2, &state, 0) == IMPULSE_VM_OK &&
        state.register_types[4] == TYPE_FLOAT_VECTOR) {
        size_t h_out = static_cast<size_t>(state.registers[4]);
        const float* vals = impulse_vm_context_get_float_vector(ctx, h_out);
        if (vals) {
            for (uint32_t b = 0; b < K; ++b) {
                out[b] = static_cast<double>(vals[b]);
            }
        }
    }

    if (op == ReducerOp::AVG) {
        impulse_instruction_t cnt_prog[2] = {
            {OP_LANE_REDUCE_ATTR, flags, 5, static_cast<uint32_t>(3 | (attr_or_reg << 8) | (4u << 16))},
            {OP_HALT, 0, 0, 0}
        };
        state.pc = 0;
        if (impulse_vm_execute(cnt_prog, 2, &state, 0) == IMPULSE_VM_OK &&
            state.register_types[5] == TYPE_FLOAT_VECTOR) {
            size_t h_cnt = static_cast<size_t>(state.registers[5]);
            const float* counts = impulse_vm_context_get_float_vector(ctx, h_cnt);
            if (counts) {
                for (uint32_t b = 0; b < K; ++b) {
                    if (counts[b] > 0.0f) {
                        out[b] /= static_cast<double>(counts[b]);
                    }
                }
            }
        }
    }

    impulse_vm_context_destroy(ctx);
    return out;
}

std::vector<int64_t> MultiLaneTraversal::argMinNodesPerLane(const std::string& node_attribute) const {
    auto res = collect();
    uint32_t K = res.laneCount();
    uint32_t N = res.domainNodeCount();
    std::vector<int64_t> best_id(K, -1);
    if (K == 0 || N == 0 || !snapshot_) return best_id;

    impulse_vm_context_t* ctx = impulse_vm_context_create(snapshot_);
    if (!ctx) return best_id;

    impulse_vm_state_t state{};
    state.query_context = ctx;

    int h_lf = acquire_lane_frontier(ctx, N, K);
    if (h_lf < 0) {
        impulse_vm_context_destroy(ctx);
        return best_id;
    }
    auto& lf = ctx->lane_frontiers[h_lf];
    lf.num_lanes = K;
    lf.lanes.assign(N, 0ULL);
    lf.active_mask = (K >= 64) ? ~0ULL : ((1ULL << K) - 1ULL);
    lf.active_words.assign((N + 63) / 64, 0ULL);
    for (uint32_t u = 0; u < N; ++u) {
        uint64_t mask = res.laneMaskFor(u);
        lf.lanes[u] = mask;
        if ((mask & lf.active_mask) != 0ULL) {
            lf.active_words[u >> 6] |= (1ULL << (u & 63));
        }
    }
    state.registers[3] = static_cast<uint64_t>(h_lf);
    state.register_types[3] = TYPE_LANE_FRONTIER;

    uint8_t flags = 0;
    uint8_t attr_or_reg = 0;
    if (res.hasProjectedValues() && node_attribute.empty()) {
        flags = 0x01;
        int h_fvec = impulse_vm_context_acquire_float_vector(ctx);
        if (h_fvec >= 0) {
            for (uint32_t u = 0; u < N; ++u) {
                impulse_vm_context_float_vector_set(ctx, static_cast<size_t>(h_fvec), u, res.valueFor(u));
            }
            state.registers[2] = static_cast<uint64_t>(h_fvec);
            state.register_types[2] = TYPE_FLOAT_VECTOR;
            attr_or_reg = 2;
        }
    } else {
        flags = 0x00;
        attr_or_reg = static_cast<uint8_t>(resolve_attr_or_index_id(snapshot_, node_attribute, current_domain_id_));
    }

    impulse_instruction_t prog[2] = {
        {OP_LANE_REDUCE_ATTR, flags, 4, static_cast<uint32_t>(3 | (attr_or_reg << 8) | (BINARY_OP_ARGMIN << 16))},
        {OP_HALT, 0, 0, 0}
    };
    state.pc = 0;
    if (impulse_vm_execute(prog, 2, &state, 0) == IMPULSE_VM_OK &&
        state.register_types[4] == TYPE_UINT64_VECTOR) {
        size_t h_out = static_cast<size_t>(state.registers[4]);
        const uint64_t* ptr = impulse_vm_context_get_node_vector(ctx, h_out);
        if (ptr) {
            for (uint32_t b = 0; b < K; ++b) {
                best_id[b] = (ptr[b] == ~0ULL) ? -1 : static_cast<int64_t>(ptr[b]);
            }
        }
    }
    impulse_vm_context_destroy(ctx);
    return best_id;
}

std::vector<int64_t> MultiLaneTraversal::argMaxNodesPerLane(const std::string& node_attribute) const {
    auto res = collect();
    uint32_t K = res.laneCount();
    uint32_t N = res.domainNodeCount();
    std::vector<int64_t> best_id(K, -1);
    if (K == 0 || N == 0 || !snapshot_) return best_id;

    impulse_vm_context_t* ctx = impulse_vm_context_create(snapshot_);
    if (!ctx) return best_id;

    impulse_vm_state_t state{};
    state.query_context = ctx;

    int h_lf = acquire_lane_frontier(ctx, N, K);
    if (h_lf < 0) {
        impulse_vm_context_destroy(ctx);
        return best_id;
    }
    auto& lf = ctx->lane_frontiers[h_lf];
    lf.num_lanes = K;
    lf.lanes.assign(N, 0ULL);
    lf.active_mask = (K >= 64) ? ~0ULL : ((1ULL << K) - 1ULL);
    lf.active_words.assign((N + 63) / 64, 0ULL);
    for (uint32_t u = 0; u < N; ++u) {
        uint64_t mask = res.laneMaskFor(u);
        lf.lanes[u] = mask;
        if ((mask & lf.active_mask) != 0ULL) {
            lf.active_words[u >> 6] |= (1ULL << (u & 63));
        }
    }
    state.registers[3] = static_cast<uint64_t>(h_lf);
    state.register_types[3] = TYPE_LANE_FRONTIER;

    uint8_t flags = 0;
    uint8_t attr_or_reg = 0;
    if (res.hasProjectedValues() && node_attribute.empty()) {
        flags = 0x01;
        int h_fvec = impulse_vm_context_acquire_float_vector(ctx);
        if (h_fvec >= 0) {
            for (uint32_t u = 0; u < N; ++u) {
                impulse_vm_context_float_vector_set(ctx, static_cast<size_t>(h_fvec), u, res.valueFor(u));
            }
            state.registers[2] = static_cast<uint64_t>(h_fvec);
            state.register_types[2] = TYPE_FLOAT_VECTOR;
            attr_or_reg = 2;
        }
    } else {
        flags = 0x00;
        attr_or_reg = static_cast<uint8_t>(resolve_attr_or_index_id(snapshot_, node_attribute, current_domain_id_));
    }

    impulse_instruction_t prog[2] = {
        {OP_LANE_REDUCE_ATTR, flags, 4, static_cast<uint32_t>(3 | (attr_or_reg << 8) | (BINARY_OP_ARGMAX << 16))},
        {OP_HALT, 0, 0, 0}
    };
    state.pc = 0;
    if (impulse_vm_execute(prog, 2, &state, 0) == IMPULSE_VM_OK &&
        state.register_types[4] == TYPE_UINT64_VECTOR) {
        size_t h_out = static_cast<size_t>(state.registers[4]);
        const uint64_t* ptr = impulse_vm_context_get_node_vector(ctx, h_out);
        if (ptr) {
            for (uint32_t b = 0; b < K; ++b) {
                best_id[b] = (ptr[b] == ~0ULL) ? -1 : static_cast<int64_t>(ptr[b]);
            }
        }
    }
    impulse_vm_context_destroy(ctx);
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
    if (initial_domain_id_ == current_domain_id_) {
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
