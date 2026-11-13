/**
 * @file impulse_vm_fluent_traversal.cpp
 * @brief High-level domain-bound single-lane Traversal, DomainView, and SnapshotView implementation.
 */

#include "impulse_vm_fluent_internal.hpp"

namespace impulse::vm {

// --- Single-Lane Traversal Implementation ---

Traversal::Traversal(const impulse_snapshot_t* snapshot, uint16_t domain_id, std::vector<uint64_t> seeds, bool all_nodes)
    : snapshot_(snapshot),
      initial_domain_id_(domain_id),
      current_domain_id_(domain_id),
      seeds_(std::move(seeds)),
      all_nodes_(all_nodes) {}

Traversal& Traversal::out(const std::string& relation) {
    uint16_t rid = resolve_relation_id_by_name(snapshot_, current_domain_id_, relation);
    current_domain_id_ = advance_domain_id(snapshot_, current_domain_id_, rid, false);
    Step s{};
    s.kind = Step::Kind::OUT;
    s.str1 = relation;
    steps_.push_back(std::move(s));
    return *this;
}

Traversal& Traversal::out(const std::vector<std::string>& relations) {
    if (relations.size() == 1) return out(relations[0]);
    if (!relations.empty()) {
        uint16_t rid = resolve_relation_id_by_name(snapshot_, current_domain_id_, relations[0]);
        current_domain_id_ = advance_domain_id(snapshot_, current_domain_id_, rid, false);
    }
    Step s{};
    s.kind = Step::Kind::OUT_MULTI;
    s.rel_names = relations;
    steps_.push_back(std::move(s));
    return *this;
}

Traversal& Traversal::in(const std::string& relation) {
    uint16_t rid = resolve_relation_id_by_name(snapshot_, current_domain_id_, relation);
    current_domain_id_ = advance_domain_id(snapshot_, current_domain_id_, rid, true);
    Step s{};
    s.kind = Step::Kind::IN;
    s.str1 = relation;
    steps_.push_back(std::move(s));
    return *this;
}

Traversal& Traversal::in(const std::vector<std::string>& relations) {
    if (relations.size() == 1) return in(relations[0]);
    if (!relations.empty()) {
        uint16_t rid = resolve_relation_id_by_name(snapshot_, current_domain_id_, relations[0]);
        current_domain_id_ = advance_domain_id(snapshot_, current_domain_id_, rid, true);
    }
    Step s{};
    s.kind = Step::Kind::IN_MULTI;
    s.rel_names = relations;
    steps_.push_back(std::move(s));
    return *this;
}

Traversal& Traversal::both(const std::string& relation) {
    Step s{};
    s.kind = Step::Kind::BOTH;
    s.str1 = relation;
    steps_.push_back(std::move(s));
    return *this;
}

Traversal& Traversal::both(const std::vector<std::string>& relations) {
    if (relations.size() == 1) return both(relations[0]);
    Step s{};
    s.kind = Step::Kind::BOTH_MULTI;
    s.rel_names = relations;
    steps_.push_back(std::move(s));
    return *this;
}

Traversal& Traversal::outFiltered(const std::string& relation, uint32_t filter_id) {
    uint16_t rid = resolve_relation_id_by_name(snapshot_, current_domain_id_, relation);
    current_domain_id_ = advance_domain_id(snapshot_, current_domain_id_, rid, false);
    Step s{};
    s.kind = Step::Kind::OUT_FILTERED;
    s.str1 = relation;
    s.u32_arg1 = filter_id;
    steps_.push_back(std::move(s));
    return *this;
}

Traversal& Traversal::outPredicate(const std::string& relation, const std::string& edge_cel_predicate) {
    uint16_t rid = resolve_relation_id_by_name(snapshot_, current_domain_id_, relation);
    current_domain_id_ = advance_domain_id(snapshot_, current_domain_id_, rid, false);
    Step s{};
    s.kind = Step::Kind::OUT_PREDICATE;
    s.str1 = relation;
    s.str2 = edge_cel_predicate;
    steps_.push_back(std::move(s));
    return *this;
}

Traversal& Traversal::inPredicate(const std::string& relation, const std::string& edge_cel_predicate) {
    uint16_t rid = resolve_relation_id_by_name(snapshot_, current_domain_id_, relation);
    current_domain_id_ = advance_domain_id(snapshot_, current_domain_id_, rid, true);
    Step s{};
    s.kind = Step::Kind::IN_PREDICATE;
    s.str1 = relation;
    s.str2 = edge_cel_predicate;
    steps_.push_back(std::move(s));
    return *this;
}

Traversal& Traversal::out2Hop(const std::string& rel1, const std::string& rel2) {
    uint16_t r1 = resolve_relation_id_by_name(snapshot_, current_domain_id_, rel1);
    current_domain_id_ = advance_domain_id(snapshot_, current_domain_id_, r1, false);
    uint16_t r2 = resolve_relation_id_by_name(snapshot_, current_domain_id_, rel2);
    current_domain_id_ = advance_domain_id(snapshot_, current_domain_id_, r2, false);
    Step s{};
    s.kind = Step::Kind::OUT_2HOP;
    s.str1 = rel1;
    s.str2 = rel2;
    steps_.push_back(std::move(s));
    return *this;
}

Traversal& Traversal::repeat(int iterations, const std::string& relation) {
    Step s{};
    s.kind = Step::Kind::REPEAT;
    s.str1 = relation;
    s.u32_arg1 = static_cast<uint32_t>(std::max(0, iterations));
    steps_.push_back(std::move(s));
    return *this;
}

Traversal& Traversal::repeatUntilStable(const std::string& relation, uint32_t max_hops) {
    Step s{};
    s.kind = Step::Kind::REPEAT_UNTIL_STABLE;
    s.str1 = relation;
    s.u32_arg1 = max_hops;
    steps_.push_back(std::move(s));
    return *this;
}

Traversal& Traversal::filter(const std::string& node_cel_predicate) {
    Step s{};
    s.kind = Step::Kind::FILTER_CEL;
    s.str1 = node_cel_predicate;
    steps_.push_back(std::move(s));
    return *this;
}

Traversal& Traversal::project(const std::string& cel_expression) {
    Step s{};
    s.kind = Step::Kind::PROJECT_CEL;
    s.str1 = cel_expression;
    steps_.push_back(std::move(s));
    return *this;
}

Traversal& Traversal::validAt(int64_t epoch_millis) {
    Step s{};
    s.kind = Step::Kind::VALID_AT;
    s.i64_arg = epoch_millis;
    steps_.push_back(std::move(s));
    return *this;
}

Traversal& Traversal::unionWith(const Traversal& other) {
    Step s{};
    s.kind = Step::Kind::SET_UNION;
    s.other_nodes = other.collect();
    steps_.push_back(std::move(s));
    return *this;
}

Traversal& Traversal::intersect(const Traversal& other) {
    Step s{};
    s.kind = Step::Kind::SET_INTERSECT;
    s.other_nodes = other.collect();
    steps_.push_back(std::move(s));
    return *this;
}

Traversal& Traversal::difference(const Traversal& other) {
    Step s{};
    s.kind = Step::Kind::SET_DIFFERENCE;
    s.other_nodes = other.collect();
    steps_.push_back(std::move(s));
    return *this;
}

Traversal& Traversal::mulNodeAttr(const std::string& attr_name) {
    Step s{};
    s.kind = Step::Kind::MUL_ATTR;
    s.str1 = attr_name;
    steps_.push_back(std::move(s));
    return *this;
}

Traversal& Traversal::scaleState(float factor) {
    Step s{};
    s.kind = Step::Kind::SCALE_STATE;
    s.f32_arg = factor;
    steps_.push_back(std::move(s));
    return *this;
}

Traversal& Traversal::divNodeAttr(const std::string& attr_name) {
    Step s{};
    s.kind = Step::Kind::DIV_ATTR;
    s.str1 = attr_name;
    steps_.push_back(std::move(s));
    return *this;
}

double Traversal::l1NormDiff(const Traversal& other) const {
    auto v1 = evaluateStateVector();
    auto v2 = other.evaluateStateVector();
    size_t n = std::max(v1.size(), v2.size());
    double diff = 0.0;
    for (size_t i = 0; i < n; ++i) {
        double a = (i < v1.size()) ? v1[i] : 0.0;
        double b = (i < v2.size()) ? v2[i] : 0.0;
        diff += std::abs(a - b);
    }
    return diff;
}

Traversal& Traversal::mxv(const std::string& relation, uint8_t semiring) {
    uint16_t rid = resolve_relation_id_by_name(snapshot_, current_domain_id_, relation);
    current_domain_id_ = advance_domain_id(snapshot_, current_domain_id_, rid, false);
    Step s{};
    s.kind = Step::Kind::MXV;
    s.str1 = relation;
    s.u32_arg1 = semiring;
    steps_.push_back(std::move(s));
    return *this;
}

Traversal& Traversal::vxm(const std::string& relation, uint8_t semiring) {
    uint16_t rid = resolve_relation_id_by_name(snapshot_, current_domain_id_, relation);
    current_domain_id_ = advance_domain_id(snapshot_, current_domain_id_, rid, true);
    Step s{};
    s.kind = Step::Kind::VXM;
    s.str1 = relation;
    s.u32_arg1 = semiring;
    steps_.push_back(std::move(s));
    return *this;
}

Traversal& Traversal::ewiseAdd(const Traversal& other, uint8_t binary_op) {
    Step s{};
    s.kind = Step::Kind::EWISE_ADD;
    s.u32_arg1 = binary_op;
    s.other_nodes = other.collect();
    steps_.push_back(std::move(s));
    return *this;
}

Traversal& Traversal::ewiseMult(const Traversal& other, uint8_t binary_op) {
    Step s{};
    s.kind = Step::Kind::EWISE_MULT;
    s.u32_arg1 = binary_op;
    s.other_nodes = other.collect();
    steps_.push_back(std::move(s));
    return *this;
}

Traversal& Traversal::degree(const std::string& relation) {
    Step s{};
    s.kind = Step::Kind::DEGREE;
    s.str1 = relation;
    steps_.push_back(std::move(s));
    return *this;
}

Traversal& Traversal::sampleNeighbors(const std::string& relation, int32_t k, uint32_t seed) {
    uint16_t rid = resolve_relation_id_by_name(snapshot_, current_domain_id_, relation);
    current_domain_id_ = advance_domain_id(snapshot_, current_domain_id_, rid, false);
    Step s{};
    s.kind = Step::Kind::SAMPLE_NEIGHBORS;
    s.str1 = relation;
    s.u32_arg1 = static_cast<uint32_t>(k);
    s.u32_arg2 = seed;
    steps_.push_back(std::move(s));
    return *this;
}

Traversal& Traversal::randomWalk(const std::string& relation, int32_t steps_cnt, uint32_t seed) {
    uint16_t rid = resolve_relation_id_by_name(snapshot_, current_domain_id_, relation);
    current_domain_id_ = advance_domain_id(snapshot_, current_domain_id_, rid, false);
    Step s{};
    s.kind = Step::Kind::RANDOM_WALK;
    s.str1 = relation;
    s.u32_arg1 = static_cast<uint32_t>(steps_cnt);
    s.u32_arg2 = seed;
    steps_.push_back(std::move(s));
    return *this;
}

Traversal& Traversal::kCore(int32_t k) {
    Step s{};
    s.kind = Step::Kind::KCORE;
    s.u32_arg1 = static_cast<uint32_t>(k);
    steps_.push_back(std::move(s));
    return *this;
}

Traversal& Traversal::rerank(const std::vector<float>& query_vector, const std::string& attr_name, uint8_t metric) {
    Step s{};
    s.kind = Step::Kind::RERANK;
    s.str1 = attr_name;
    s.u32_arg1 = metric;
    s.float_vec = query_vector;
    steps_.push_back(std::move(s));
    return *this;
}

CompiledQuery Traversal::compile() const {
    QueryBuilder qb(0);
    if (all_nodes_ || seeds_.size() != 1) {
        qb.inputSet(0);
    } else {
        qb.inputNode(0);
    }
    uint16_t step_dom = initial_domain_id_;
    for (const auto& step : steps_) {
        uint16_t rel_id = resolve_relation_id_by_name(snapshot_, step_dom, step.str1);
        switch (step.kind) {
            case Step::Kind::OUT:
            case Step::Kind::MXV:
                qb.walkEdge(rel_id);
                step_dom = advance_domain_id(snapshot_, step_dom, rel_id, false);
                break;
            case Step::Kind::IN:
            case Step::Kind::VXM:
                qb.walkCsc(rel_id);
                step_dom = advance_domain_id(snapshot_, step_dom, rel_id, true);
                break;
            case Step::Kind::OUT_FILTERED:
                qb.walkEdgeFiltered(rel_id, step.u32_arg1);
                step_dom = advance_domain_id(snapshot_, step_dom, rel_id, false);
                break;
            case Step::Kind::OUT_PREDICATE:
                qb.walkEdgePredicate(rel_id, 0);
                step_dom = advance_domain_id(snapshot_, step_dom, rel_id, false);
                break;
            case Step::Kind::IN_PREDICATE:
                qb.walkCsc(rel_id);
                step_dom = advance_domain_id(snapshot_, step_dom, rel_id, true);
                break;
            case Step::Kind::OUT_2HOP: {
                uint16_t mid_dom = advance_domain_id(snapshot_, step_dom, rel_id, false);
                uint16_t r2 = resolve_relation_id_by_name(snapshot_, mid_dom, step.str2);
                qb.walkEdge2Hop(rel_id, r2);
                step_dom = advance_domain_id(snapshot_, mid_dom, r2, false);
                break;
            }
            case Step::Kind::REPEAT:
                qb.repeat(static_cast<int>(step.u32_arg1), [rel_id](QueryBuilder& b) {
                    b.walkEdge(rel_id);
                });
                break;
            case Step::Kind::REPEAT_UNTIL_STABLE:
                qb.repeatUntilStable([rel_id](QueryBuilder& b) {
                    b.walkEdge(rel_id);
                });
                break;
            case Step::Kind::FILTER_CEL:
                qb.filterCel(step.str1);
                break;
            case Step::Kind::PROJECT_CEL:
                qb.project(step.str1);
                break;
            case Step::Kind::VALID_AT:
                qb.loadConstInt(step.i64_arg, 3);
                qb.setCurrentRegister(0);
                qb.vectorTimeValidAt(3, 1, 2);
                break;
            case Step::Kind::DEGREE:
                qb.walkDegree(rel_id);
                break;
            case Step::Kind::SAMPLE_NEIGHBORS:
                qb.sampleNeighbors(rel_id, static_cast<int32_t>(step.u32_arg1), step.u32_arg2);
                step_dom = advance_domain_id(snapshot_, step_dom, rel_id, false);
                break;
            case Step::Kind::RANDOM_WALK:
                qb.randomWalk(rel_id, static_cast<int32_t>(step.u32_arg1), step.u32_arg2);
                step_dom = advance_domain_id(snapshot_, step_dom, rel_id, false);
                break;
            case Step::Kind::KCORE:
                qb.kcoreDecomposition();
                break;
            case Step::Kind::SCALE_STATE:
                qb.floatVectorScale(step.f32_arg);
                break;
            default:
                break;
        }
    }
    qb.collectBitset();
    return qb.compile();
}

std::vector<uint64_t> Traversal::collect() const {
    if (!snapshot_) return {};
    uint32_t init_n = get_domain_node_count(snapshot_, initial_domain_id_);

    impulse_vm_context_t* ctx = impulse_vm_context_create(snapshot_);
    if (!ctx) return {};

    impulse_vm_state_t state{};
    state.query_context = ctx;

    // Always seed initial frontier in R0 via OP_INIT_INPUT_SET so R0 is guaranteed TYPE_BITSET_HANDLE
    std::vector<uint64_t> init_words(ctx->words_per_bitset, 0ULL);
    if (all_nodes_) {
        for (uint32_t u = 0; u < init_n; ++u) {
            size_t w = u / 64;
            if (w < init_words.size()) {
                init_words[w] |= (1ULL << (u % 64));
            }
        }
    } else {
        for (uint64_t u : seeds_) {
            if (u < init_n) {
                size_t w = static_cast<size_t>(u / 64);
                if (w < init_words.size()) {
                    init_words[w] |= (1ULL << (u % 64));
                }
            }
        }
    }

    impulse_instruction_t init_prog[2] = {
        {OP_INIT_INPUT_SET, 0, 0, 0},
        {OP_HALT, 0, 0, 0}
    };
    state.pc = 0;
    impulse_vm_execute(init_prog, 2, &state, reinterpret_cast<uint64_t>(init_words.data()));

    auto exec_r0_step = [&](const impulse_instruction_t* prog_ptr, size_t prog_len) {
        int old_h0 = (state.register_types[0] == TYPE_BITSET_HANDLE) ? static_cast<int>(state.registers[0]) : -1;
        state.pc = 0;
        impulse_vm_execute(prog_ptr, prog_len, &state, 0);
        int new_h0 = (state.register_types[0] == TYPE_BITSET_HANDLE) ? static_cast<int>(state.registers[0]) : -1;
        if (old_h0 >= 0 && old_h0 != new_h0) {
            release_bitset(ctx, old_h0);
        }
    };

    uint16_t step_dom = initial_domain_id_;
    for (const auto& step : steps_) {
        uint16_t rel_id = resolve_relation_id_by_name(snapshot_, step_dom, step.str1);
        if (step.kind == Step::Kind::OUT || step.kind == Step::Kind::MXV) {
            impulse_instruction_t insts[2] = {
                {OP_CSR_WALK, 0, 0, static_cast<uint32_t>((rel_id << 16) | 0)},
                {OP_HALT, 0, 0, 0}
            };
            exec_r0_step(insts, 2);
            step_dom = advance_domain_id(snapshot_, step_dom, rel_id, false);
        } else if (step.kind == Step::Kind::IN || step.kind == Step::Kind::VXM) {
            impulse_instruction_t insts[2] = {
                {OP_CSC_WALK, 0, 0, static_cast<uint32_t>((rel_id << 16) | 0)},
                {OP_HALT, 0, 0, 0}
            };
            exec_r0_step(insts, 2);
            step_dom = advance_domain_id(snapshot_, step_dom, rel_id, true);
        } else if (step.kind == Step::Kind::BOTH || step.kind == Step::Kind::OUT_MULTI ||
                   step.kind == Step::Kind::IN_MULTI || step.kind == Step::Kind::BOTH_MULTI) {
            std::vector<std::pair<uint16_t, bool>> branches; // (rel_id, is_csc)
            if (step.kind == Step::Kind::BOTH) {
                branches.emplace_back(rel_id, false);
                branches.emplace_back(rel_id, true);
            } else {
                for (const auto& rname : step.rel_names) {
                    uint16_t rid = resolve_relation_id_by_name(snapshot_, step_dom, rname);
                    if (step.kind == Step::Kind::OUT_MULTI || step.kind == Step::Kind::BOTH_MULTI) {
                        branches.emplace_back(rid, false);
                    }
                    if (step.kind == Step::Kind::IN_MULTI || step.kind == Step::Kind::BOTH_MULTI) {
                        branches.emplace_back(rid, true);
                    }
                }
            }
            if (branches.empty()) {
                if (state.register_types[0] == TYPE_BITSET_HANDLE) {
                    ctx->bitsets[state.registers[0]].clear();
                }
            } else {
                std::vector<impulse_instruction_t> b_prog;
                b_prog.push_back({static_cast<uint8_t>(branches[0].second ? OP_CSC_WALK : OP_CSR_WALK), 0, 5,
                                  static_cast<uint32_t>((branches[0].first << 16) | 0)});
                for (size_t bi = 1; bi < branches.size(); ++bi) {
                    b_prog.push_back({static_cast<uint8_t>(branches[bi].second ? OP_CSC_WALK : OP_CSR_WALK), 0, 6,
                                      static_cast<uint32_t>((branches[bi].first << 16) | 0)});
                    b_prog.push_back({OP_SET_UNION, 0, 5, static_cast<uint32_t>(5 | (6 << 16))});
                }
                b_prog.push_back({OP_MOV, 0, 0, 5});
                b_prog.push_back({OP_HALT, 0, 0, 0});
                exec_r0_step(b_prog.data(), b_prog.size());
                if (state.register_types[5] == TYPE_BITSET_HANDLE && state.registers[5] != state.registers[0]) {
                    release_bitset(ctx, static_cast<int>(state.registers[5]));
                }
                if (state.register_types[6] == TYPE_BITSET_HANDLE && state.registers[6] != state.registers[0]) {
                    release_bitset(ctx, static_cast<int>(state.registers[6]));
                }
                state.registers[5] = 0;
                state.register_types[5] = TYPE_NULL;
                state.registers[6] = 0;
                state.register_types[6] = TYPE_NULL;
                if (step.kind == Step::Kind::OUT_MULTI) {
                    step_dom = advance_domain_id(snapshot_, step_dom, branches[0].first, false);
                } else if (step.kind == Step::Kind::IN_MULTI) {
                    step_dom = advance_domain_id(snapshot_, step_dom, branches[0].first, true);
                }
            }
        } else if (step.kind == Step::Kind::OUT_2HOP) {
            uint16_t mid_dom = advance_domain_id(snapshot_, step_dom, rel_id, false);
            uint16_t r2 = resolve_relation_id_by_name(snapshot_, mid_dom, step.str2);
            bool use_two_walks = true;
            if (rel_id < ctx->slots.size() && r2 < ctx->slots.size()) {
                const auto& s1 = ctx->slots[rel_id];
                const auto& s2 = ctx->slots[r2];
                if (!s1.is_virtual && !s1.is_raw_coo && s1.offsets_ptr && s1.targets_ptr &&
                    s1.node_id_width == 4 && s1.edge_index_width == 4 &&
                    !s2.is_virtual && !s2.is_raw_coo && s2.offsets_ptr && s2.targets_ptr &&
                    s2.node_id_width == 4 && s2.edge_index_width == 4) {
                    use_two_walks = false;
                }
            }
            if (use_two_walks) {
                impulse_instruction_t w1[2] = {
                    {OP_CSR_WALK, 0, 0, static_cast<uint32_t>((rel_id << 16) | 0)},
                    {OP_HALT, 0, 0, 0}
                };
                exec_r0_step(w1, 2);
                impulse_instruction_t w2[2] = {
                    {OP_CSR_WALK, 0, 0, static_cast<uint32_t>((r2 << 16) | 0)},
                    {OP_HALT, 0, 0, 0}
                };
                exec_r0_step(w2, 2);
            } else {
                impulse_instruction_t insts[2] = {
                    {OP_CSR_WALK_2HOP, 0, 0, static_cast<uint32_t>(rel_id | (r2 << 16))},
                    {OP_HALT, 0, 0, 0}
                };
                exec_r0_step(insts, 2);
            }
            step_dom = advance_domain_id(snapshot_, mid_dom, r2, false);
        } else if (step.kind == Step::Kind::OUT_FILTERED) {
            impulse_instruction_t insts[2] = {
                {OP_CSR_WALK_FILTERED, 0, 0, static_cast<uint32_t>((rel_id << 16) | ((step.u32_arg1 & 0xFF) << 8) | 0)},
                {OP_HALT, 0, 0, 0}
            };
            exec_r0_step(insts, 2);
            step_dom = advance_domain_id(snapshot_, step_dom, rel_id, false);
        } else if (step.kind == Step::Kind::OUT_PREDICATE || step.kind == Step::Kind::IN_PREDICATE) {
            bool is_rev = (step.kind == Step::Kind::IN_PREDICATE);
            if (!is_rev && step.str2.empty()) {
                impulse_instruction_t insts[2] = {
                    {OP_CSR_WALK_PREDICATE, 0, 0, static_cast<uint32_t>((rel_id << 16) | 0)},
                    {OP_HALT, 0, 0, 0}
                };
                exec_r0_step(insts, 2);
            } else if (state.register_types[0] == TYPE_BITSET_HANDLE) {
                impulse::cel::Parser parser(step.str2);
                auto ast = parser.parse_expression();
                auto opt = ast ? impulse::cel::AstOptimizer::optimize(ast) : nullptr;

                int old_h0 = static_cast<int>(state.registers[0]);
                int h_dst = acquire_bitset(ctx);
                if (h_dst >= 0) {
                    auto& bs_src = ctx->bitsets[old_h0];
                    auto& bs_dst = ctx->bitsets[h_dst];
                    bs_dst.clear();

                    uint32_t src_dom_n = get_domain_node_count(snapshot_, step_dom);
                    std::vector<uint32_t> active_src;
                    extract_active_bits(bs_src, active_src);

                    auto eval_rel_edges = [&](auto& self, uint16_t rid, uint32_t depth) -> void {
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
                                    if (u < src_dom_n && bitset_test(bs_src, u, ctx->max_nodes) && test_edge(e)) {
                                        bitset_add(bs_dst, slot.get_csr_target(e), ctx->max_nodes);
                                    }
                                }
                            } else if (slot.offsets_ptr && slot.targets_ptr) {
                                for (uint32_t u : active_src) {
                                    if (u >= src_dom_n || u >= slot.node_count) continue;
                                    uint64_t start = slot.get_csr_offset(u);
                                    uint64_t end = std::min<uint64_t>(slot.get_csr_offset(u + 1), slot.edge_count);
                                    for (uint64_t e = start; e < end; ++e) {
                                        if (test_edge(e)) {
                                            bitset_add(bs_dst, slot.get_csr_target(e), ctx->max_nodes);
                                        }
                                    }
                                }
                            } else if (!slot.offsets_ptr && slot.targets_ptr) {
                                uint64_t sent = slot.csr_sentinel();
                                if (slot.edge_count == slot.node_count) {
                                    for (uint32_t u : active_src) {
                                        if (u >= src_dom_n || u >= slot.node_count) continue;
                                        uint64_t v = slot.get_csr_target(u);
                                        if (v != sent && test_edge(u)) {
                                            bitset_add(bs_dst, v, ctx->max_nodes);
                                        }
                                    }
                                } else {
                                    uint64_t edge_rank = 0;
                                    for (uint64_t u = 0; u < slot.node_count; ++u) {
                                        uint64_t v = slot.get_csr_target(u);
                                        if (v != sent) {
                                            if (u < src_dom_n && bitset_test(bs_src, u, ctx->max_nodes) && test_edge(edge_rank)) {
                                                bitset_add(bs_dst, v, ctx->max_nodes);
                                            }
                                            edge_rank++;
                                        }
                                    }
                                }
                            }
                        } else {
                            if (slot.csc_offsets_ptr && slot.csc_targets_ptr) {
                                for (uint32_t v : active_src) {
                                    if (v >= src_dom_n || v >= slot.csc_node_count) continue;
                                    uint64_t start = slot.get_csc_offset(v);
                                    uint64_t end = std::min<uint64_t>(slot.get_csc_offset(v + 1), slot.edge_count);
                                    for (uint64_t ce = start; ce < end; ++ce) {
                                        uint64_t csr_e = (ce < slot.csc_to_csr_edge_idx.size()) ? slot.csc_to_csr_edge_idx[ce] : ce;
                                        if (test_edge(csr_e)) {
                                            bitset_add(bs_dst, slot.get_csc_target(ce), ctx->max_nodes);
                                        }
                                    }
                                }
                            } else if (slot.is_raw_coo && slot.offsets_ptr && slot.targets_ptr) {
                                for (uint64_t e = 0; e < slot.edge_count; ++e) {
                                    uint64_t v = slot.get_csr_target(e);
                                    if (v < src_dom_n && bitset_test(bs_src, v, ctx->max_nodes) && test_edge(e)) {
                                        bitset_add(bs_dst, slot.get_raw_coo_src(e), ctx->max_nodes);
                                    }
                                }
                            } else if (slot.offsets_ptr && slot.targets_ptr) {
                                for (uint64_t u = 0; u < slot.node_count; ++u) {
                                    uint64_t start = slot.get_csr_offset(u);
                                    uint64_t end = std::min<uint64_t>(slot.get_csr_offset(u + 1), slot.edge_count);
                                    for (uint64_t e = start; e < end; ++e) {
                                        uint64_t v = slot.get_csr_target(e);
                                        if (v < src_dom_n && bitset_test(bs_src, v, ctx->max_nodes) && test_edge(e)) {
                                            bitset_add(bs_dst, u, ctx->max_nodes);
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
                                        if (v < src_dom_n && bitset_test(bs_src, v, ctx->max_nodes) && test_edge(eidx)) {
                                            bitset_add(bs_dst, u, ctx->max_nodes);
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
                                        if (v < src_dom_n && bitset_test(bs_src, v, ctx->max_nodes) && test_edge(eidx)) {
                                            bitset_add(bs_dst, u, ctx->max_nodes);
                                        }
                                    }
                                }
                            }
                        }
                    };

                    if (!active_src.empty()) {
                        eval_rel_edges(eval_rel_edges, rel_id, 0);
                    }
                    state.registers[0] = static_cast<uint64_t>(h_dst);
                    state.register_types[0] = TYPE_BITSET_HANDLE;
                    release_bitset(ctx, old_h0);
                }
            }
            step_dom = advance_domain_id(snapshot_, step_dom, rel_id, is_rev);
        } else if (step.kind == Step::Kind::REPEAT) {
            for (uint32_t it = 0; it < step.u32_arg1; ++it) {
                impulse_instruction_t insts[2] = {
                    {OP_CSR_WALK, 0, 0, static_cast<uint32_t>((rel_id << 16) | 0)},
                    {OP_HALT, 0, 0, 0}
                };
                exec_r0_step(insts, 2);
                step_dom = advance_domain_id(snapshot_, step_dom, rel_id, false);
            }
        } else if (step.kind == Step::Kind::REPEAT_UNTIL_STABLE) {
            ctx->fixpoint_max_hops = step.u32_arg1;
            ctx->has_fixpoint_max_hops = true;
            impulse_instruction_t insts[2] = {
                {OP_FIXPOINT_KLEENE_STAR, 0, 0, static_cast<uint32_t>((rel_id << 16) | 0)},
                {OP_HALT, 0, 0, 0}
            };
            exec_r0_step(insts, 2);
            ctx->has_fixpoint_max_hops = false;
            ctx->fixpoint_max_hops = 0;
        } else if (step.kind == Step::Kind::SET_UNION || step.kind == Step::Kind::SET_INTERSECT ||
                   step.kind == Step::Kind::SET_DIFFERENCE || step.kind == Step::Kind::EWISE_ADD ||
                   step.kind == Step::Kind::EWISE_MULT) {
            uint32_t step_n = get_domain_node_count(snapshot_, step_dom);
            std::vector<uint64_t> other_words(ctx->words_per_bitset, 0ULL);
            for (uint64_t u : step.other_nodes) {
                if (u < step_n) {
                    size_t w = static_cast<size_t>(u / 64);
                    if (w < other_words.size()) {
                        other_words[w] |= (1ULL << (u % 64));
                    }
                }
            }
            if (state.register_types[3] == TYPE_BITSET_HANDLE) {
                release_bitset(ctx, static_cast<int>(state.registers[3]));
                state.registers[3] = 0;
                state.register_types[3] = TYPE_NULL;
            }
            impulse_instruction_t init3[2] = {{OP_INIT_INPUT_SET, 0, 3, 0}, {OP_HALT, 0, 0, 0}};
            state.pc = 0;
            impulse_vm_execute(init3, 2, &state, reinterpret_cast<uint64_t>(other_words.data()));

            uint8_t op = OP_SET_UNION;
            if (step.kind == Step::Kind::SET_INTERSECT || step.kind == Step::Kind::EWISE_MULT) op = OP_SET_INTERSECT;
            else if (step.kind == Step::Kind::SET_DIFFERENCE) op = OP_SET_DIFFERENCE;
            impulse_instruction_t comb[2] = {
                {op, 0, 0, static_cast<uint32_t>(0 | (3 << 16))},
                {OP_HALT, 0, 0, 0}
            };
            exec_r0_step(comb, 2);
            if (state.register_types[3] == TYPE_BITSET_HANDLE) {
                release_bitset(ctx, static_cast<int>(state.registers[3]));
                state.registers[3] = 0;
                state.register_types[3] = TYPE_NULL;
            }
        } else if (step.kind == Step::Kind::SAMPLE_NEIGHBORS) {
            impulse_instruction_t insts[2] = {
                {OP_SAMPLE_NEIGHBORS, 0, 0, static_cast<uint32_t>((rel_id << 16) | (step.u32_arg1 & 0xFFFF))},
                {OP_HALT, 0, 0, 0}
            };
            exec_r0_step(insts, 2);
            step_dom = advance_domain_id(snapshot_, step_dom, rel_id, false);
        } else if (step.kind == Step::Kind::RANDOM_WALK) {
            impulse_instruction_t insts[2] = {
                {OP_RANDOM_WALK, 0, 0, static_cast<uint32_t>((rel_id << 16) | (step.u32_arg1 & 0xFFFF))},
                {OP_HALT, 0, 0, 0}
            };
            exec_r0_step(insts, 2);
            step_dom = advance_domain_id(snapshot_, step_dom, rel_id, false);
        } else if (step.kind == Step::Kind::KCORE) {
            impulse_instruction_t insts[2] = {
                {OP_KCORE_DECOMPOSITION, 0, 0, step.u32_arg1},
                {OP_HALT, 0, 0, 0}
            };
            exec_r0_step(insts, 2);
        } else if (step.kind == Step::Kind::FILTER_CEL) {
            impulse::cel::Parser parser(step.str1);
            auto ast = parser.parse_expression();
            if (ast && state.register_types[0] == TYPE_BITSET_HANDLE) {
                auto opt = impulse::cel::AstOptimizer::optimize(ast);
                uint64_t handle = state.registers[0];
                uint32_t step_n = get_domain_node_count(snapshot_, step_dom);
                auto dom_attrs = build_domain_attr_cache(snapshot_, step_dom);
                std::vector<uint32_t> active_u;
                extract_active_bits(ctx->bitsets[handle], active_u);
                for (uint32_t u : active_u) {
                    if (u >= step_n ||
                        !impulse::cel::CelKleeneEvaluator::eval_filter_bool(
                            opt, [&](const std::string& aname) { return dom_attrs.resolve(u, aname); })) {
                        impulse_vm_context_bitset_remove(ctx, handle, u);
                    }
                }
            }
        } else if (step.kind == Step::Kind::PROJECT_CEL) {
            impulse::cel::Parser p(step.str1);
            auto ast = p.parse_expression();
            if (ast && state.register_types[0] == TYPE_BITSET_HANDLE) {
                auto opt_ast = impulse::cel::AstOptimizer::optimize(ast);
                if (opt_ast->kind == impulse::cel::AstKind::BINARY_OP &&
                    (opt_ast->text == "==" || opt_ast->text == "!=" || opt_ast->text == "<" ||
                     opt_ast->text == "<=" || opt_ast->text == ">" || opt_ast->text == ">=")) {
                    uint64_t handle = state.registers[0];
                    uint32_t step_n = get_domain_node_count(snapshot_, step_dom);
                    auto dom_attrs = build_domain_attr_cache(snapshot_, step_dom);
                    std::vector<uint32_t> active_u;
                    extract_active_bits(ctx->bitsets[handle], active_u);
                    for (uint32_t u : active_u) {
                        if (u >= step_n ||
                            !impulse::cel::CelKleeneEvaluator::eval_filter_bool(
                                opt_ast, [&](const std::string& aname) { return dom_attrs.resolve(u, aname); })) {
                            impulse_vm_context_bitset_remove(ctx, handle, u);
                        }
                    }
                }
            }
        }
    }

    uint32_t final_max_n = get_domain_node_count(snapshot_, step_dom);
    std::vector<uint64_t> out;
    if (state.register_types[0] == TYPE_BITSET_HANDLE) {
        std::vector<uint32_t> active_u;
        extract_active_bits(ctx->bitsets[state.registers[0]], active_u);
        out.reserve(active_u.size());
        for (uint32_t u : active_u) {
            if (u < final_max_n) {
                out.push_back(u);
            }
        }
    }
    impulse_vm_context_destroy(ctx);
    return out;
}

std::vector<std::string> Traversal::collectKeys() const {
    auto nodes = collect();
    if (nodes.empty() || !snapshot_) return {};

    impulse_vm_context_t* ctx = impulse_vm_context_create(snapshot_);
    if (ctx) {
        impulse_vm_state_t state{};
        state.query_context = ctx;
        std::vector<uint64_t> words(ctx->words_per_bitset, 0ULL);
        for (uint64_t u : nodes) {
            size_t w = static_cast<size_t>(u / 64);
            if (w < words.size()) {
                words[w] |= (1ULL << (u % 64));
            }
        }
        impulse_instruction_t prog[3] = {
            {OP_INIT_INPUT_SET, 0, 0, 0},
            {OP_MAP_DENSE_TO_KEYS, 0, 1, static_cast<uint32_t>(0 | (static_cast<uint32_t>(current_domain_id_) << 8))},
            {OP_HALT, 0, 0, 0}
        };
        if (impulse_vm_execute(prog, 3, &state, reinterpret_cast<uint64_t>(words.data())) == IMPULSE_VM_OK &&
            state.register_types[1] == TYPE_STRING_VECTOR) {
            size_t h_s = static_cast<size_t>(state.registers[1]);
            const auto& svec = ctx->string_vectors[h_s];
            if (svec.size() == nodes.size()) {
                std::vector<std::string> vm_keys;
                vm_keys.reserve(svec.size());
                for (const char* s : svec) {
                    vm_keys.emplace_back(s ? s : "");
                }
                impulse_vm_context_destroy(ctx);
                return vm_keys;
            }
        }
        impulse_vm_context_destroy(ctx);
    }

    std::vector<std::string> keys;
    keys.reserve(nodes.size());
    for (uint64_t u : nodes) {
        keys.push_back(resolve_dense_to_key(snapshot_, current_domain_id_, static_cast<uint32_t>(u)));
    }
    return keys;
}

size_t Traversal::count() const {
    return collect().size();
}

std::optional<double> Traversal::reduceAttr(const std::string& attr_or_cel_expr, ReducerOp op) const {
    auto active = collect();
    if (!snapshot_) {
        if (op == ReducerOp::SUM || op == ReducerOp::COUNT) return 0.0;
        return std::nullopt;
    }
    if (op == ReducerOp::COUNT) {
        return static_cast<double>(active.size());
    }
    if (active.empty()) {
        if (op == ReducerOp::SUM) return 0.0;
        return std::nullopt;
    }

    auto dom_attrs = build_domain_attr_cache(snapshot_, current_domain_id_);
    impulse::cel::Parser parser(attr_or_cel_expr);
    auto ast = parser.parse_expression();
    auto opt = ast ? impulse::cel::AstOptimizer::optimize(ast) : nullptr;

    size_t non_null_count = 0;
    double sum_val = 0.0;
    double min_val = 0.0;
    double max_val = 0.0;

    for (uint64_t u : active) {
        impulse::cel::CelValue cv = opt
            ? impulse::cel::CelKleeneEvaluator::eval(
                  opt, [&](const std::string& aname) { return dom_attrs.resolve(u, aname); })
            : dom_attrs.resolve(u, attr_or_cel_expr);
        double dv = 0.0;
        if (!cv.to_double(dv)) continue; // skip NULL / NaN
        if (non_null_count == 0) {
            min_val = dv;
            max_val = dv;
        } else {
            if (dv < min_val) min_val = dv;
            if (dv > max_val) max_val = dv;
        }
        sum_val += dv;
        ++non_null_count;
    }

    if (non_null_count == 0) {
        if (op == ReducerOp::SUM) return 0.0;
        return std::nullopt;
    }
    switch (op) {
        case ReducerOp::SUM: return sum_val;
        case ReducerOp::MIN: return min_val;
        case ReducerOp::MAX: return max_val;
        case ReducerOp::AVG: return sum_val / static_cast<double>(non_null_count);
        default: return std::nullopt;
    }
}

std::vector<float> Traversal::evaluateStateVector(std::vector<uint64_t>* out_active_nodes) const {
    auto active = collect();
    if (out_active_nodes) *out_active_nodes = active;
    uint32_t max_n = get_domain_node_count(snapshot_, current_domain_id_);
    std::vector<float> state_vec(max_n, 0.0f);

    const float* default_attr_f32 = nullptr;
    uint64_t default_attr_bytes = 0;
    uint8_t default_tc = 0;
    uint32_t default_dim = 0;
    const void* raw_default = nullptr;
    if (resolve_named_attribute_buffer(snapshot_, "", &raw_default, &default_attr_bytes, &default_tc, &default_dim, current_domain_id_) &&
        raw_default && ((default_tc & 0x7F) == IMPULSE_ATTR_TYPE_FLOAT32 ||
                        ((default_tc & 0x7F) == 0x05 && snapshot_->header.version == 9))) {
        default_attr_f32 = static_cast<const float*>(raw_default);
    }
    size_t default_count = default_attr_f32 ? (default_attr_bytes / sizeof(float)) : 0;

    bool has_explicit_state_step = false;
    for (const auto& s : steps_) {
        if (s.kind == Step::Kind::MUL_ATTR || s.kind == Step::Kind::DIV_ATTR ||
            s.kind == Step::Kind::SCALE_STATE || s.kind == Step::Kind::PROJECT_CEL ||
            s.kind == Step::Kind::RERANK) {
            has_explicit_state_step = true;
            break;
        }
    }

    for (uint64_t u : active) {
        if (u < max_n) {
            state_vec[u] = (!has_explicit_state_step && u < default_count) ? default_attr_f32[u] : 1.0f;
        }
    }

    auto dom_attrs = build_domain_attr_cache(snapshot_, current_domain_id_);

    for (const auto& step : steps_) {
        if (step.kind == Step::Kind::SCALE_STATE) {
            for (uint64_t u : active) {
                if (u < max_n) state_vec[u] *= step.f32_arg;
            }
        } else if (step.kind == Step::Kind::MUL_ATTR) {
            if (!step.str1.empty()) {
                for (uint64_t u : active) {
                    if (u >= max_n) continue;
                    double dv = 0.0;
                    if (dom_attrs.resolve(u, step.str1).to_double(dv)) {
                        state_vec[u] *= static_cast<float>(dv);
                    } else {
                        state_vec[u] = 0.0f;
                    }
                }
            } else if (default_attr_f32) {
                for (uint64_t u : active) {
                    if (u < max_n && u < default_count) state_vec[u] *= default_attr_f32[u];
                }
            }
        } else if (step.kind == Step::Kind::DIV_ATTR) {
            if (!step.str1.empty()) {
                for (uint64_t u : active) {
                    if (u >= max_n) continue;
                    double dv = 0.0;
                    if (dom_attrs.resolve(u, step.str1).to_double(dv) && dv != 0.0) {
                        state_vec[u] /= static_cast<float>(dv);
                    }
                }
            } else if (default_attr_f32) {
                for (uint64_t u : active) {
                    if (u < max_n && u < default_count && default_attr_f32[u] != 0.0f) state_vec[u] /= default_attr_f32[u];
                }
            }
        } else if (step.kind == Step::Kind::PROJECT_CEL) {
            impulse::cel::Parser p(step.str1);
            auto ast = p.parse_expression();
            if (ast) {
                auto opt_ast = impulse::cel::AstOptimizer::optimize(ast);
                for (uint64_t u : active) {
                    if (u >= max_n) continue;
                    auto cv = impulse::cel::CelKleeneEvaluator::eval(
                        opt_ast, [&](const std::string& aname) { return dom_attrs.resolve(u, aname); });
                    double val = 0.0;
                    state_vec[u] = cv.to_double(val) ? static_cast<float>(val) : 0.0f;
                }
            }
        } else if (step.kind == Step::Kind::RERANK && snapshot_ && !step.float_vec.empty() && !active.empty()) {
            uint16_t aid = resolve_attr_or_index_id(snapshot_, step.str1, current_domain_id_);
            uint16_t encoded_aid = static_cast<uint16_t>(((current_domain_id_ & 0xFFu) << 8) | (aid & 0xFFu));
            uint8_t metric_flag = static_cast<uint8_t>(step.u32_arg1 & 0x03U);
            impulse_vm_context_t* rctx = impulse_vm_context_create(snapshot_);
            if (rctx) {
                impulse_vm_state_t rstate{};
                rstate.query_context = rctx;
                impulse_vm_context_bind_inline_data(rctx, step.float_vec.data(), step.float_vec.size() * sizeof(float));

                std::vector<uint64_t> awords(rctx->words_per_bitset, 0ULL);
                for (uint64_t u : active) {
                    size_t w = static_cast<size_t>(u / 64);
                    if (w < awords.size()) {
                        awords[w] |= (1ULL << (u % 64));
                    }
                }
                impulse_instruction_t init0[2] = {{OP_INIT_INPUT_SET, 0, 0, 0}, {OP_HALT, 0, 0, 0}};
                rstate.pc = 0;
                impulse_vm_execute(init0, 2, &rstate, reinterpret_cast<uint64_t>(awords.data()));

                impulse_instruction_t rprog[3] = {
                    {OP_LOAD_INLINE_ARRAY, 0, 1, static_cast<uint32_t>(0 | (static_cast<uint32_t>(step.float_vec.size()) << 16))},
                    {OP_VECTOR_RERANK_DOT, metric_flag, 2, static_cast<uint32_t>(0 | (1 << 8) | (static_cast<uint32_t>(encoded_aid) << 16))},
                    {OP_HALT, 0, 0, 0}
                };
                rstate.pc = 0;
                if (impulse_vm_execute(rprog, 3, &rstate, 0) == IMPULSE_VM_OK &&
                    rstate.register_types[2] == TYPE_FLOAT_VECTOR) {
                    size_t h_f = static_cast<size_t>(rstate.registers[2]);
                    const float* fdata = impulse_vm_context_get_float_vector(rctx, h_f);
                    size_t flen = impulse_vm_context_float_vector_size(rctx, h_f);
                    if (fdata != nullptr) {
                        size_t lim = std::min(active.size(), flen);
                        for (size_t i = 0; i < lim; ++i) {
                            if (active[i] < max_n) {
                                state_vec[active[i]] = fdata[i];
                            }
                        }
                    }
                }
                impulse_vm_context_destroy(rctx);
            }
        }
    }
    return state_vec;
}

std::vector<float> Traversal::collectStateVector() const {
    return evaluateStateVector();
}

std::vector<std::pair<uint64_t, float>> Traversal::collectNodeValues() const {
    std::vector<uint64_t> active;
    auto svec = evaluateStateVector(&active);
    std::vector<std::pair<uint64_t, float>> out;
    out.reserve(active.size());
    for (uint64_t u : active) {
        float val = (u < svec.size()) ? svec[u] : 0.0f;
        out.emplace_back(u, val);
    }
    return out;
}

std::vector<std::pair<std::string, float>> Traversal::collectKeyValues() const {
    std::vector<uint64_t> active;
    auto svec = evaluateStateVector(&active);
    std::vector<std::pair<std::string, float>> out;
    if (active.empty() || !snapshot_) return out;

    impulse_vm_context_t* ctx = impulse_vm_context_create(snapshot_);
    if (!ctx) return out;

    impulse_vm_state_t state{};
    state.query_context = ctx;

    std::vector<uint64_t> awords(ctx->words_per_bitset, 0ULL);
    for (uint64_t u : active) {
        size_t w = static_cast<size_t>(u / 64);
        if (w < awords.size()) {
            awords[w] |= (1ULL << (u % 64));
        }
    }
    impulse_instruction_t init0[2] = {{OP_INIT_INPUT_SET, 0, 0, 0}, {OP_HALT, 0, 0, 0}};
    state.pc = 0;
    impulse_vm_execute(init0, 2, &state, reinterpret_cast<uint64_t>(awords.data()));

    int h_fvec = impulse_vm_context_acquire_float_vector(ctx);
    if (h_fvec >= 0) {
        for (uint64_t u : active) {
            float val = (u < svec.size()) ? svec[u] : 0.0f;
            impulse_vm_context_float_vector_set(ctx, static_cast<size_t>(h_fvec), u, val);
        }
        state.registers[1] = static_cast<uint64_t>(h_fvec);
        state.register_types[1] = TYPE_FLOAT_VECTOR;

        impulse_instruction_t vmap_prog[2] = {
            {OP_COLLECT_VALUE_MAP, 0, 2, static_cast<uint32_t>(0 | (1 << 8) | (current_domain_id_ << 16))},
            {OP_HALT, 0, 0, 0}
        };
        state.pc = 0;
        if (impulse_vm_execute(vmap_prog, 2, &state, 0) == IMPULSE_VM_OK &&
            state.register_types[2] == TYPE_VALUE_MAP) {
            size_t h_vmap = static_cast<size_t>(state.registers[2]);
            size_t sz = impulse_vm_context_value_map_size(ctx, h_vmap);
            out.reserve(sz);
            for (size_t i = 0; i < sz; ++i) {
                const char* k = impulse_vm_context_value_map_get_key(ctx, h_vmap, i);
                float v = impulse_vm_context_value_map_get_value(ctx, h_vmap, i);
                out.emplace_back(k ? k : "", v);
            }
        }
    }
    impulse_vm_context_destroy(ctx);
    return out;
}

std::vector<std::pair<uint64_t, float>> Traversal::topK(size_t k) const {
    if (k == 0) return {};
    std::vector<uint64_t> active;
    auto vec = evaluateStateVector(&active);
    std::vector<std::pair<uint64_t, float>> scored;
    scored.reserve(active.size());
    for (uint64_t u : active) {
        float sc = (u < vec.size()) ? vec[u] : 0.0f;
        if (std::isnan(sc)) continue;
        scored.emplace_back(u, sc);
    }
    std::sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) {
        if (a.second != b.second) return a.second > b.second;
        return a.first < b.first;
    });
    if (scored.size() > k) scored.resize(k);
    return scored;
}

std::vector<std::pair<std::string, float>> Traversal::topKKeys(size_t k) const {
    auto top_dense = topK(k);
    std::vector<std::pair<std::string, float>> out;
    out.reserve(top_dense.size());
    for (const auto& p : top_dense) {
        out.emplace_back(resolve_dense_to_key(snapshot_, current_domain_id_, static_cast<uint32_t>(p.first)), p.second);
    }
    return out;
}

double Traversal::reduceSum() const {
    std::vector<uint64_t> active;
    auto vec = evaluateStateVector(&active);
    double sum = 0.0;
    for (uint64_t u : active) {
        if (u < vec.size()) sum += vec[u];
    }
    return sum;
}

double Traversal::walkReduceSum(const std::string& relation, const std::string& /*attr_name*/) const {
    Traversal next = *this;
    next.out(relation);
    return next.reduceSum();
}

double Traversal::reduceMin() const {
    std::vector<uint64_t> active;
    auto vec = evaluateStateVector(&active);
    if (active.empty()) return 0.0;
    double best = vec[active[0]];
    for (uint64_t u : active) {
        if (u < vec.size() && vec[u] < best) best = vec[u];
    }
    return best;
}

double Traversal::reduceMax() const {
    std::vector<uint64_t> active;
    auto vec = evaluateStateVector(&active);
    if (active.empty()) return 0.0;
    double best = vec[active[0]];
    for (uint64_t u : active) {
        if (u < vec.size() && vec[u] > best) best = vec[u];
    }
    return best;
}

int64_t Traversal::argMin() const {
    std::vector<uint64_t> active;
    auto vec = evaluateStateVector(&active);
    if (active.empty()) return -1;
    int64_t best_u = static_cast<int64_t>(active[0]);
    double best_v = vec[active[0]];
    for (uint64_t u : active) {
        if (u < vec.size()) {
            if (vec[u] < best_v || (vec[u] == best_v && static_cast<int64_t>(u) < best_u)) {
                best_v = vec[u];
                best_u = static_cast<int64_t>(u);
            }
        }
    }
    return best_u;
}

int64_t Traversal::argMax() const {
    std::vector<uint64_t> active;
    auto vec = evaluateStateVector(&active);
    if (active.empty()) return -1;
    int64_t best_u = static_cast<int64_t>(active[0]);
    double best_v = vec[active[0]];
    for (uint64_t u : active) {
        if (u < vec.size()) {
            if (vec[u] > best_v || (vec[u] == best_v && static_cast<int64_t>(u) < best_u)) {
                best_v = vec[u];
                best_u = static_cast<int64_t>(u);
            }
        }
    }
    return best_u;
}

std::string Traversal::argMinKey() const {
    int64_t u = argMin();
    return (u < 0) ? "" : resolve_dense_to_key(snapshot_, current_domain_id_, static_cast<uint32_t>(u));
}

std::string Traversal::argMaxKey() const {
    int64_t u = argMax();
    return (u < 0) ? "" : resolve_dense_to_key(snapshot_, current_domain_id_, static_cast<uint32_t>(u));
}


// --- DomainView & SnapshotView Implementation ---

Traversal DomainView::all() const {
    return Traversal(snapshot_, domain_id_, {}, true);
}

Traversal DomainView::from(std::vector<uint64_t> node_ids) const {
    return Traversal(snapshot_, domain_id_, std::move(node_ids), false);
}

Traversal DomainView::fromKey(const std::string& key) const {
    return Traversal(snapshot_, domain_id_, {resolve_key_to_dense(snapshot_, domain_id_, key)}, false);
}

Traversal DomainView::fromKeys(const std::vector<std::string>& keys) const {
    std::vector<uint64_t> ids;
    ids.reserve(keys.size());
    for (const auto& k : keys) {
        ids.push_back(resolve_key_to_dense(snapshot_, domain_id_, k));
    }
    return Traversal(snapshot_, domain_id_, std::move(ids), false);
}

Traversal DomainView::vectorSearch(const std::string& index_or_attr, const std::vector<float>& query_vector, uint16_t top_k) const {
    if (!snapshot_ || query_vector.empty() || top_k == 0) {
        return Traversal(snapshot_, domain_id_, {}, false);
    }
    uint16_t idx_id = resolve_attr_or_index_id(snapshot_, index_or_attr);
    impulse_vm_context_t* ctx = impulse_vm_context_create(snapshot_);
    if (!ctx) return Traversal(snapshot_, domain_id_, {}, false);

    impulse_vm_state_t state{};
    state.query_context = ctx;
    impulse_vm_context_bind_inline_data(ctx, query_vector.data(), query_vector.size() * sizeof(float));

    impulse_instruction_t prog[3] = {
        {OP_LOAD_INLINE_ARRAY, 0, 1, static_cast<uint32_t>(0 | (static_cast<uint32_t>(query_vector.size()) << 16))},
        {OP_VECTOR_ANN_SEARCH, 0, 0, static_cast<uint32_t>(1 | ((top_k & 0xFF) << 8) | ((idx_id & 0xFFFF) << 16))},
        {OP_HALT, 0, 0, 0}
    };
    std::vector<uint64_t> hits;
    if (impulse_vm_execute(prog, 3, &state, 0) == IMPULSE_VM_OK &&
        state.register_types[0] == TYPE_BITSET_HANDLE) {
        uint64_t h = state.registers[0];
        uint32_t max_n = get_domain_node_count(snapshot_, domain_id_);
        for (uint32_t u = 0; u < max_n; ++u) {
            if (impulse_vm_context_bitset_test(ctx, h, u)) {
                hits.push_back(u);
            }
        }
    }
    impulse_vm_context_destroy(ctx);
    return Traversal(snapshot_, domain_id_, std::move(hits), false);
}

DomainView SnapshotView::domain(const std::string& domain_name) const {
    return DomainView(snapshot_, resolve_domain_id_by_name(snapshot_, domain_name));
}

} // namespace impulse::vm
