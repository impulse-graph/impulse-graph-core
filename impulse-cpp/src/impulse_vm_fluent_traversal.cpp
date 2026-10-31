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
    Step s{};
    s.kind = Step::Kind::OUT;
    s.str1 = relation;
    steps_.push_back(std::move(s));
    return *this;
}

Traversal& Traversal::out(const std::vector<std::string>& relations) {
    if (relations.size() == 1) return out(relations[0]);
    Step s{};
    s.kind = Step::Kind::OUT_MULTI;
    s.rel_names = relations;
    steps_.push_back(std::move(s));
    return *this;
}

Traversal& Traversal::in(const std::string& relation) {
    Step s{};
    s.kind = Step::Kind::IN;
    s.str1 = relation;
    steps_.push_back(std::move(s));
    return *this;
}

Traversal& Traversal::in(const std::vector<std::string>& relations) {
    if (relations.size() == 1) return in(relations[0]);
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
    Step s{};
    s.kind = Step::Kind::OUT_FILTERED;
    s.str1 = relation;
    s.u32_arg1 = filter_id;
    steps_.push_back(std::move(s));
    return *this;
}

Traversal& Traversal::outPredicate(const std::string& relation, const std::string& edge_cel_predicate) {
    Step s{};
    s.kind = Step::Kind::OUT_PREDICATE;
    s.str1 = relation;
    s.str2 = edge_cel_predicate;
    steps_.push_back(std::move(s));
    return *this;
}

Traversal& Traversal::inPredicate(const std::string& relation, const std::string& edge_cel_predicate) {
    Step s{};
    s.kind = Step::Kind::IN_PREDICATE;
    s.str1 = relation;
    s.str2 = edge_cel_predicate;
    steps_.push_back(std::move(s));
    return *this;
}

Traversal& Traversal::out2Hop(const std::string& rel1, const std::string& rel2) {
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

Traversal& Traversal::repeatUntilStable(const std::string& relation) {
    Step s{};
    s.kind = Step::Kind::REPEAT_UNTIL_STABLE;
    s.str1 = relation;
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
    Step s{};
    s.kind = Step::Kind::MXV;
    s.str1 = relation;
    s.u32_arg1 = semiring;
    steps_.push_back(std::move(s));
    return *this;
}

Traversal& Traversal::vxm(const std::string& relation, uint8_t semiring) {
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
    Step s{};
    s.kind = Step::Kind::SAMPLE_NEIGHBORS;
    s.str1 = relation;
    s.u32_arg1 = static_cast<uint32_t>(k);
    s.u32_arg2 = seed;
    steps_.push_back(std::move(s));
    return *this;
}

Traversal& Traversal::randomWalk(const std::string& relation, int32_t steps_cnt, uint32_t seed) {
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
    for (const auto& step : steps_) {
        uint16_t rel_id = resolve_relation_id_by_name(snapshot_, current_domain_id_, step.str1);
        switch (step.kind) {
            case Step::Kind::OUT:
            case Step::Kind::MXV:
                qb.walkEdge(rel_id);
                break;
            case Step::Kind::IN:
            case Step::Kind::VXM:
                qb.walkCsc(rel_id);
                break;
            case Step::Kind::OUT_FILTERED:
                qb.walkEdgeFiltered(rel_id, step.u32_arg1);
                break;
            case Step::Kind::OUT_PREDICATE:
                qb.walkEdgePredicate(rel_id, 0);
                break;
            case Step::Kind::IN_PREDICATE:
                qb.walkCsc(rel_id);
                break;
            case Step::Kind::OUT_2HOP: {
                uint16_t r2 = resolve_relation_id_by_name(snapshot_, current_domain_id_, step.str2);
                qb.walkEdge2Hop(rel_id, r2);
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
                break;
            case Step::Kind::RANDOM_WALK:
                qb.randomWalk(rel_id, static_cast<int32_t>(step.u32_arg1), step.u32_arg2);
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
    uint32_t max_n = get_domain_node_count(snapshot_, initial_domain_id_);
    if (max_n == 0) return {};

    impulse_vm_context_t* ctx = impulse_vm_context_create(snapshot_);
    if (!ctx) return {};

    impulse_vm_state_t state{};
    state.query_context = ctx;

    // Seed initial frontier in R0
    std::vector<impulse_instruction_t> init_prog;
    if (all_nodes_) {
        init_prog.push_back(impulse_instruction_t{OP_INIT_INPUT_SET, 0, 0, 0});
    } else if (seeds_.empty()) {
        init_prog.push_back(impulse_instruction_t{OP_CLEAR_REG, 0, 0, 0});
    } else {
        init_prog.push_back(impulse_instruction_t{OP_INIT_INPUT_NODE, 0, 0, 0});
    }
    init_prog.push_back(impulse_instruction_t{OP_HALT, 0, 0, 0});

    if (all_nodes_ || seeds_.empty()) {
        state.pc = 0;
        impulse_vm_execute(init_prog.data(), init_prog.size(), &state, 0);
    } else {
        // Union all seeds into R0 via VM
        state.pc = 0;
        impulse_vm_execute(init_prog.data(), init_prog.size(), &state, seeds_[0]);
        for (size_t i = 1; i < seeds_.size(); ++i) {
            impulse_instruction_t add_seed[3] = {
                {OP_INIT_INPUT_NODE, 0, 2, 0},
                {OP_SET_UNION, 0, 0, static_cast<uint32_t>(0 | (2 << 16))},
                {OP_HALT, 0, 0, 0}
            };
            state.pc = 0;
            impulse_vm_execute(add_seed, 3, &state, seeds_[i]);
        }
    }

    for (const auto& step : steps_) {
        uint16_t rel_id = resolve_relation_id_by_name(snapshot_, current_domain_id_, step.str1);
        if (step.kind == Step::Kind::OUT || step.kind == Step::Kind::MXV) {
            impulse_instruction_t insts[2] = {
                {OP_CSR_WALK, 0, 0, static_cast<uint32_t>((rel_id << 16) | 0)},
                {OP_HALT, 0, 0, 0}
            };
            state.pc = 0;
            impulse_vm_execute(insts, 2, &state, 0);
        } else if (step.kind == Step::Kind::IN || step.kind == Step::Kind::VXM) {
            impulse_instruction_t insts[2] = {
                {OP_CSC_WALK, 0, 0, static_cast<uint32_t>((rel_id << 16) | 0)},
                {OP_HALT, 0, 0, 0}
            };
            state.pc = 0;
            impulse_vm_execute(insts, 2, &state, 0);
        } else if (step.kind == Step::Kind::BOTH || step.kind == Step::Kind::OUT_MULTI ||
                   step.kind == Step::Kind::IN_MULTI || step.kind == Step::Kind::BOTH_MULTI) {
            std::vector<std::pair<uint16_t, bool>> branches; // (rel_id, is_csc)
            if (step.kind == Step::Kind::BOTH) {
                branches.emplace_back(rel_id, false);
                branches.emplace_back(rel_id, true);
            } else {
                for (const auto& rname : step.rel_names) {
                    uint16_t rid = resolve_relation_id_by_name(snapshot_, current_domain_id_, rname);
                    if (step.kind == Step::Kind::OUT_MULTI || step.kind == Step::Kind::BOTH_MULTI) {
                        branches.emplace_back(rid, false);
                    }
                    if (step.kind == Step::Kind::IN_MULTI || step.kind == Step::Kind::BOTH_MULTI) {
                        branches.emplace_back(rid, true);
                    }
                }
            }
            if (!branches.empty()) {
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
                state.pc = 0;
                impulse_vm_execute(b_prog.data(), b_prog.size(), &state, 0);
            }
        } else if (step.kind == Step::Kind::OUT_2HOP) {
            uint16_t r2 = resolve_relation_id_by_name(snapshot_, current_domain_id_, step.str2);
            impulse_instruction_t insts[2] = {
                {OP_CSR_WALK_2HOP, 0, 0, static_cast<uint32_t>(rel_id | (r2 << 16))},
                {OP_HALT, 0, 0, 0}
            };
            state.pc = 0;
            impulse_vm_execute(insts, 2, &state, 0);
        } else if (step.kind == Step::Kind::OUT_FILTERED) {
            impulse_instruction_t insts[2] = {
                {OP_CSR_WALK_FILTERED, 0, 0, static_cast<uint32_t>((rel_id << 16) | ((step.u32_arg1 & 0xFF) << 8) | 0)},
                {OP_HALT, 0, 0, 0}
            };
            state.pc = 0;
            impulse_vm_execute(insts, 2, &state, 0);
        } else if (step.kind == Step::Kind::OUT_PREDICATE) {
            impulse_instruction_t insts[2] = {
                {OP_CSR_WALK_PREDICATE, 0, 0, static_cast<uint32_t>((rel_id << 16) | 0)},
                {OP_HALT, 0, 0, 0}
            };
            state.pc = 0;
            impulse_vm_execute(insts, 2, &state, 0);
        } else if (step.kind == Step::Kind::REPEAT) {
            for (uint32_t it = 0; it < step.u32_arg1; ++it) {
                impulse_instruction_t insts[2] = {
                    {OP_CSR_WALK, 0, 0, static_cast<uint32_t>((rel_id << 16) | 0)},
                    {OP_HALT, 0, 0, 0}
                };
                state.pc = 0;
                impulse_vm_execute(insts, 2, &state, 0);
            }
        } else if (step.kind == Step::Kind::REPEAT_UNTIL_STABLE) {
            impulse_instruction_t insts[3] = {
                {OP_CSR_WALK, 0, 0, static_cast<uint32_t>((rel_id << 16) | 0)},
                {OP_STABLE_CHECK, 0, 0, static_cast<uint32_t>(-1)},
                {OP_HALT, 0, 0, 0}
            };
            state.pc = 0;
            impulse_vm_execute(insts, 3, &state, 0);
        } else if (step.kind == Step::Kind::SET_UNION || step.kind == Step::Kind::SET_INTERSECT ||
                   step.kind == Step::Kind::SET_DIFFERENCE || step.kind == Step::Kind::EWISE_ADD ||
                   step.kind == Step::Kind::EWISE_MULT) {
            // Load other_nodes into R3 via VM
            impulse_instruction_t clr[2] = {{OP_CLEAR_REG, 0, 3, 0}, {OP_HALT, 0, 0, 0}};
            state.pc = 0;
            impulse_vm_execute(clr, 2, &state, 0);
            if (!step.other_nodes.empty()) {
                impulse_instruction_t init3[2] = {{OP_INIT_INPUT_NODE, 0, 3, 0}, {OP_HALT, 0, 0, 0}};
                state.pc = 0;
                impulse_vm_execute(init3, 2, &state, step.other_nodes[0]);
                for (size_t i = 1; i < step.other_nodes.size(); ++i) {
                    impulse_instruction_t u3[3] = {
                        {OP_INIT_INPUT_NODE, 0, 4, 0},
                        {OP_SET_UNION, 0, 3, static_cast<uint32_t>(3 | (4 << 16))},
                        {OP_HALT, 0, 0, 0}
                    };
                    state.pc = 0;
                    impulse_vm_execute(u3, 3, &state, step.other_nodes[i]);
                }
            }
            uint8_t op = OP_SET_UNION;
            if (step.kind == Step::Kind::SET_INTERSECT || step.kind == Step::Kind::EWISE_MULT) op = OP_SET_INTERSECT;
            else if (step.kind == Step::Kind::SET_DIFFERENCE) op = OP_SET_DIFFERENCE;
            impulse_instruction_t comb[2] = {
                {op, 0, 0, static_cast<uint32_t>(0 | (3 << 16))},
                {OP_HALT, 0, 0, 0}
            };
            state.pc = 0;
            impulse_vm_execute(comb, 2, &state, 0);
        } else if (step.kind == Step::Kind::SAMPLE_NEIGHBORS) {
            impulse_instruction_t insts[2] = {
                {OP_SAMPLE_NEIGHBORS, 0, 0, static_cast<uint32_t>((rel_id << 16) | (step.u32_arg1 & 0xFFFF))},
                {OP_HALT, 0, 0, 0}
            };
            state.pc = 0;
            impulse_vm_execute(insts, 2, &state, 0);
        } else if (step.kind == Step::Kind::RANDOM_WALK) {
            impulse_instruction_t insts[2] = {
                {OP_RANDOM_WALK, 0, 0, static_cast<uint32_t>((rel_id << 16) | (step.u32_arg1 & 0xFFFF))},
                {OP_HALT, 0, 0, 0}
            };
            state.pc = 0;
            impulse_vm_execute(insts, 2, &state, 0);
        } else if (step.kind == Step::Kind::KCORE) {
            impulse_instruction_t insts[2] = {
                {OP_KCORE_DECOMPOSITION, 0, 0, step.u32_arg1},
                {OP_HALT, 0, 0, 0}
            };
            state.pc = 0;
            impulse_vm_execute(insts, 2, &state, 0);
        }
    }

    std::vector<uint64_t> out;
    if (state.register_types[0] == TYPE_BITSET_HANDLE) {
        uint64_t handle = state.registers[0];
        for (uint32_t u = 0; u < max_n; ++u) {
            if (impulse_vm_context_bitset_test(ctx, handle, u)) {
                out.push_back(u);
            }
        }
    }
    impulse_vm_context_destroy(ctx);
    return out;
}

std::vector<std::string> Traversal::collectKeys() const {
    auto nodes = collect();
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

std::vector<float> Traversal::evaluateStateVector(std::vector<uint64_t>* out_active_nodes) const {
    auto active = collect();
    if (out_active_nodes) *out_active_nodes = active;
    uint32_t max_n = get_domain_node_count(snapshot_, current_domain_id_);
    std::vector<float> state_vec(max_n, 0.0f);

    const float* attr_f32 = nullptr;
    uint64_t attr_bytes = 0;
    if (snapshot_ && impulse_snapshot_relation_count(snapshot_) > 0) {
        const void* raw_data = nullptr;
        uint8_t tc = 0;
        uint32_t dim = 0;
        if (impulse_snapshot_get_attribute_buffers(snapshot_, 0, 0, &raw_data, &attr_bytes, nullptr, nullptr, &tc, &dim) == IMPULSE_OK &&
            raw_data != nullptr && tc == IMPULSE_ATTR_TYPE_FLOAT32) {
            attr_f32 = static_cast<const float*>(raw_data);
        }
    }
    size_t attr_count = attr_f32 ? (attr_bytes / sizeof(float)) : 0;

    for (uint64_t u : active) {
        if (u < max_n) {
            state_vec[u] = (u < attr_count) ? attr_f32[u] : 1.0f;
        }
    }

    for (const auto& step : steps_) {
        if (step.kind == Step::Kind::SCALE_STATE) {
            for (uint64_t u : active) {
                if (u < max_n) state_vec[u] *= step.f32_arg;
            }
        } else if (step.kind == Step::Kind::MUL_ATTR && attr_f32) {
            for (uint64_t u : active) {
                if (u < max_n && u < attr_count) state_vec[u] *= attr_f32[u];
            }
        } else if (step.kind == Step::Kind::DIV_ATTR && attr_f32) {
            for (uint64_t u : active) {
                if (u < max_n && u < attr_count && attr_f32[u] != 0.0f) state_vec[u] /= attr_f32[u];
            }
        } else if (step.kind == Step::Kind::RERANK && snapshot_ && !step.float_vec.empty() && !active.empty()) {
            uint16_t aid = resolve_attr_or_index_id(snapshot_, step.str1);
            uint8_t metric_flag = static_cast<uint8_t>(step.u32_arg1 & 0x03U);
            impulse_vm_context_t* rctx = impulse_vm_context_create(snapshot_);
            if (rctx) {
                impulse_vm_state_t rstate{};
                rstate.query_context = rctx;
                impulse_vm_context_bind_inline_data(rctx, step.float_vec.data(), step.float_vec.size() * sizeof(float));

                impulse_instruction_t init0[2] = {{OP_INIT_INPUT_NODE, 0, 0, 0}, {OP_HALT, 0, 0, 0}};
                rstate.pc = 0;
                impulse_vm_execute(init0, 2, &rstate, active[0]);
                for (size_t i = 1; i < active.size(); ++i) {
                    impulse_instruction_t add_u[3] = {
                        {OP_INIT_INPUT_NODE, 0, 3, 0},
                        {OP_SET_UNION, 0, 0, static_cast<uint32_t>(0 | (3 << 16))},
                        {OP_HALT, 0, 0, 0}
                    };
                    rstate.pc = 0;
                    impulse_vm_execute(add_u, 3, &rstate, active[i]);
                }

                impulse_instruction_t rprog[3] = {
                    {OP_LOAD_INLINE_ARRAY, 0, 1, static_cast<uint32_t>(0 | (static_cast<uint32_t>(step.float_vec.size()) << 16))},
                    {OP_VECTOR_RERANK_DOT, metric_flag, 2, static_cast<uint32_t>(0 | (1 << 8) | (aid << 16))},
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

std::vector<std::pair<uint64_t, float>> Traversal::topK(size_t k) const {
    if (k == 0) return {};
    std::vector<uint64_t> active;
    auto vec = evaluateStateVector(&active);
    std::vector<std::pair<uint64_t, float>> scored;
    scored.reserve(active.size());
    for (uint64_t u : active) {
        float sc = (u < vec.size()) ? vec[u] : 0.0f;
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
