/**
 * @file impulse_datalog.hpp
 * @brief Zero-Dependency C++20 Datalog (ImpLog) Frontend Compiler, Stratification Validator & Fixpoint Rewriter.
 *
 * Translates declarative Datalog logic rules, ReBAC security policies, and recursive transitive closures
 * directly into ImpScheme S-Expressions and optimized ImpulseVM pipelines.
 */

#ifndef IMPULSE_DATALOG_HPP
#define IMPULSE_DATALOG_HPP

#include "impulse_compiler.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <algorithm>

namespace impulse::datalog {

struct DependencyEdge {
    std::string from;
    std::string to;
    bool is_negated{false};
};

class StratificationValidator {
public:
    static bool validate(const std::vector<DependencyEdge>& edges, std::string& error_msg) {
        std::unordered_set<std::string> predicates;
        std::unordered_map<std::string, std::vector<std::pair<std::string, bool>>> adj;

        for (const auto& edge : edges) {
            predicates.insert(edge.from);
            predicates.insert(edge.to);
            adj[edge.from].push_back({edge.to, edge.is_negated});
        }

        std::unordered_set<std::string> in_stack;
        std::vector<std::pair<std::string, bool>> path;

        for (const auto& pred : predicates) {
            if (!dfs_check(pred, adj, in_stack, path, error_msg)) {
                return false;
            }
        }
        return true;
    }

    static bool is_recursive(const std::string& predicate, const std::vector<DependencyEdge>& edges) {
        std::unordered_map<std::string, std::vector<std::string>> adj;
        for (const auto& e : edges) {
            if (!e.is_negated) {
                adj[e.from].push_back(e.to);
            }
        }
        std::unordered_set<std::string> visited;
        std::vector<std::string> stack;
        auto it = adj.find(predicate);
        if (it != adj.end()) {
            stack = it->second;
        }
        while (!stack.empty()) {
            std::string cur = stack.back();
            stack.pop_back();
            if (cur == predicate) {
                return true;
            }
            if (visited.insert(cur).second) {
                auto nit = adj.find(cur);
                if (nit != adj.end()) {
                    for (const auto& nxt : nit->second) {
                        stack.push_back(nxt);
                    }
                }
            }
        }
        return false;
    }

private:
    static bool dfs_check(
        const std::string& current,
        const std::unordered_map<std::string, std::vector<std::pair<std::string, bool>>>& adj,
        std::unordered_set<std::string>& in_stack,
        std::vector<std::pair<std::string, bool>>& path,
        std::string& error_msg)
    {
        in_stack.insert(current);
        auto it = adj.find(current);
        if (it != adj.end()) {
            for (const auto& edge : it->second) {
                const std::string& next = edge.first;
                bool is_negated = edge.second;
                path.push_back({next, is_negated});
                if (in_stack.find(next) != in_stack.end()) {
                    auto pos_it = std::find_if(path.begin(), path.end(), [&](const auto& p) { return p.first == next; });
                    if (pos_it != path.end()) {
                        bool has_negation = false;
                        for (auto iter = pos_it; iter != path.end(); ++iter) {
                            if (iter->second) { has_negation = true; break; }
                        }
                        if (has_negation) {
                            error_msg = "Non-stratifiable negation cycle detected involving predicate: " + next;
                            return false;
                        }
                    }
                } else {
                    if (!dfs_check(next, adj, in_stack, path, error_msg)) return false;
                }
                path.pop_back();
            }
        }
        in_stack.erase(current);
        return true;
    }
};

class MagicSetsTransformation {
public:
    static std::pair<std::string, std::string> transform_query(const std::string& predicate, const std::string& bound_val) {
        std::string magic_name = "m_" + predicate + "_b";
        std::string magic_ast = "(magic:seed " + magic_name + " \"" + bound_val + "\")";
        return {magic_name, magic_ast};
    }

    static std::pair<std::string, std::shared_ptr<impulse::compiler::ScmLaneInitSeeds>> transform_multi_seed(
        const std::string& predicate,
        const std::vector<uint64_t>& seeds,
        uint16_t seed_reg = 0,
        uint8_t width_flag = 0)
    {
        std::string magic_name = "m_" + predicate + "_lanes";
        auto node = impulse::compiler::ScmLaneInitSeeds::from_inline_seeds(seeds, seed_reg, width_flag);
        return {magic_name, node};
    }

    static std::shared_ptr<impulse::compiler::ScmLaneInitSeeds> transform_multi_seed(
        const std::vector<uint64_t>& seeds,
        uint16_t seed_reg = 0,
        uint8_t width_flag = 0)
    {
        return impulse::compiler::ScmLaneInitSeeds::from_inline_seeds(seeds, seed_reg, width_flag);
    }

    static std::unordered_set<std::string> compute_reachable_predicates(
        const std::string& query_predicate,
        const std::vector<DependencyEdge>& edges)
    {
        std::unordered_set<std::string> reachable;
        if (query_predicate.empty()) return reachable;
        std::vector<std::string> queue;
        reachable.insert(query_predicate);
        queue.push_back(query_predicate);
        while (!queue.empty()) {
            std::string cur = queue.back();
            queue.pop_back();
            for (const auto& edge : edges) {
                if (edge.from == cur && reachable.insert(edge.to).second) {
                    queue.push_back(edge.to);
                }
            }
        }
        return reachable;
    }
};

class DatalogParser {
public:
    struct ParsedAtom {
        std::string predicate;
        std::vector<std::string> args;
        bool negated{false};
        bool reverse_direction{false};
    };

    struct ParsedRule {
        std::string head_predicate;
        std::vector<std::string> head_args;
        std::vector<ParsedAtom> body;
    };

    static std::shared_ptr<impulse::compiler::ScmProgram> parse(const std::string& source) {
        return compile_internal(source, {}, {}, 0, {});
    }

    static std::shared_ptr<impulse::compiler::ScmProgram> parse_multi_seed(
        const std::string& source,
        const std::vector<uint64_t>& seeds)
    {
        return compile_internal(source, seeds, {}, 0, {});
    }

    static std::shared_ptr<impulse::compiler::ScmProgram> parse_groups(
        const std::string& source,
        const std::vector<uint64_t>& node_group_pairs,
        uint16_t num_groups)
    {
        return compile_internal(source, {}, node_group_pairs, num_groups, {});
    }

    static std::shared_ptr<impulse::compiler::ScmProgram> parse_allow_deny_split(
        const std::string& source,
        const std::vector<uint64_t>& allow_seeds,
        const std::vector<uint64_t>& deny_seeds)
    {
        size_t half = std::max(allow_seeds.size(), deny_seeds.size());
        if (half == 0 || half > 32) {
            throw std::invalid_argument("Allow/deny split requires between 1 and 32 lanes per half");
        }
        std::vector<uint64_t> combined;
        combined.reserve(half * 2);
        for (size_t i = 0; i < half; ++i) {
            combined.push_back(i < allow_seeds.size() ? allow_seeds[i] : 0ULL);
        }
        for (size_t i = 0; i < half; ++i) {
            combined.push_back(i < deny_seeds.size() ? deny_seeds[i] : 0ULL);
        }
        auto base = compile_internal(source, combined, {}, 0, {});
        std::vector<impulse::compiler::AstPtr> steps;
        for (const auto& st : base->steps) {
            if (st->kind() == impulse::compiler::NodeKind::COLLECT) {
                steps.push_back(impulse::compiler::ScmLanePruneMask::allow_deny_split(static_cast<uint16_t>(half)));
            }
            steps.push_back(st);
        }
        return std::make_shared<impulse::compiler::ScmProgram>(std::move(steps));
    }

private:
    static std::string trim_str(const std::string& s) {
        size_t a = s.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) return "";
        size_t b = s.find_last_not_of(" \t\r\n");
        return s.substr(a, b - a + 1);
    }

    static std::string strip_inline_comment(const std::string& line) {
        std::string out = line;
        size_t slash = out.find("//");
        if (slash != std::string::npos) out = out.substr(0, slash);
        size_t pct = out.find('%');
        if (pct != std::string::npos) out = out.substr(0, pct);
        return out;
    }

    static std::vector<std::string> split_top_level_atoms(const std::string& body) {
        std::vector<std::string> out;
        int depth = 0;
        size_t start = 0;
        for (size_t i = 0; i < body.size(); ++i) {
            char c = body[i];
            if (c == '(') depth++;
            else if (c == ')') depth = std::max(0, depth - 1);
            else if (c == ',' && depth == 0) {
                std::string token = trim_str(body.substr(start, i - start));
                if (!token.empty()) out.push_back(token);
                start = i + 1;
            }
        }
        if (start < body.size()) {
            std::string token = trim_str(body.substr(start));
            if (!token.empty()) out.push_back(token);
        }
        return out;
    }

    static ParsedAtom parse_atom(const std::string& raw) {
        std::string t = trim_str(raw);
        bool neg = false;
        if (t.starts_with("!")) {
            neg = true;
            t = trim_str(t.substr(1));
        } else if (t.starts_with("not ") || t.starts_with("not\t") || t.starts_with("NOT ") || t.starts_with("NOT\t")) {
            neg = true;
            t = trim_str(t.substr(4));
        }

        bool rev = false;
        if (t.starts_with("^") || t.starts_with("~")) {
            rev = true;
            t = trim_str(t.substr(1));
        }

        size_t lp = t.find('(');
        size_t rp = t.rfind(')');
        std::string pred;
        std::vector<std::string> args;
        if (lp != std::string::npos) {
            pred = trim_str(t.substr(0, lp));
            if (rp != std::string::npos && rp > lp + 1) {
                std::string arg_str = t.substr(lp + 1, rp - lp - 1);
                std::istringstream as(arg_str);
                std::string tok;
                while (std::getline(as, tok, ',')) {
                    std::string clean = trim_str(tok);
                    if (!clean.empty()) args.push_back(clean);
                }
            }
        } else {
            pred = trim_str(t);
        }
        pred.erase(std::remove_if(pred.begin(), pred.end(), ::isspace), pred.end());
        return {pred, args, neg, rev};
    }

    static bool try_parse_u64(const std::string& tok, uint64_t& out) {
        std::string clean = trim_str(tok);
        if (clean.empty()) return false;
        for (char c : clean) {
            if (!std::isdigit(static_cast<unsigned char>(c))) return false;
        }
        out = std::stoull(clean);
        return true;
    }

    static void parse_fact_or_directive(
        const std::string& line,
        std::vector<uint64_t>& seeds,
        std::vector<uint64_t>& group_pairs,
        std::vector<uint64_t>& deny_nodes,
        uint16_t& allow_deny_half)
    {
        std::string s = line;
        while (!s.empty() && (s.back() == '.' || std::isspace(static_cast<unsigned char>(s.back())))) {
            s.pop_back();
        }
        if (!s.empty() && s.front() == '.') {
            s = trim_str(s.substr(1));
        }
        if (s.empty()) return;

        ParsedAtom atom = parse_atom(s);
        std::string lower_pred = atom.predicate;
        std::transform(lower_pred.begin(), lower_pred.end(), lower_pred.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        if (lower_pred == "seed" || lower_pred == "seeds") {
            for (const auto& a : atom.args) {
                uint64_t v = 0;
                if (try_parse_u64(a, v)) seeds.push_back(v);
            }
        } else if (lower_pred == "group" || lower_pred == "groups") {
            if (atom.args.size() >= 2) {
                uint64_t u = 0, g = 0;
                if (try_parse_u64(atom.args[0], u) && try_parse_u64(atom.args[1], g)) {
                    group_pairs.push_back(u);
                    group_pairs.push_back(g);
                }
            }
        } else if (lower_pred == "denied" || lower_pred == "deny") {
            for (const auto& a : atom.args) {
                uint64_t v = 0;
                if (try_parse_u64(a, v)) deny_nodes.push_back(v);
            }
        } else if (lower_pred == "rebac_allow_deny" || lower_pred == "allow_deny_split") {
            if (!atom.args.empty()) {
                uint64_t v = 0;
                if (try_parse_u64(atom.args[0], v)) allow_deny_half = static_cast<uint16_t>(v);
            }
        }
    }

    static std::shared_ptr<impulse::compiler::ScmProgram> compile_internal(
        const std::string& source,
        const std::vector<uint64_t>& explicit_seeds,
        const std::vector<uint64_t>& explicit_group_pairs,
        uint16_t explicit_num_groups,
        const std::vector<uint64_t>& explicit_deny_nodes)
    {
        std::vector<uint64_t> seeds = explicit_seeds;
        std::vector<uint64_t> group_pairs = explicit_group_pairs;
        std::vector<uint64_t> deny_nodes = explicit_deny_nodes;
        uint16_t allow_deny_half = 0;

        std::vector<ParsedRule> rules;
        std::vector<DependencyEdge> edges;

        std::istringstream stream(source);
        std::string raw_line;
        while (std::getline(stream, raw_line)) {
            std::string trimmed = trim_str(strip_inline_comment(raw_line));
            if (trimmed.empty() || trimmed.starts_with("//") || trimmed.starts_with("%") ||
                trimmed.starts_with("#") || trimmed.starts_with(".decl") || trimmed.starts_with("?-")) {
                continue;
            }

            size_t arrow_pos = trimmed.find(":-");
            if (arrow_pos == std::string::npos) {
                parse_fact_or_directive(trimmed, seeds, group_pairs, deny_nodes, allow_deny_half);
                continue;
            }

            std::string head_str = trim_str(trimmed.substr(0, arrow_pos));
            std::string body_str = trim_str(trimmed.substr(arrow_pos + 2));
            while (!body_str.empty() && (body_str.back() == '.' || std::isspace(static_cast<unsigned char>(body_str.back())))) {
                body_str.pop_back();
            }

            ParsedAtom head_atom = parse_atom(head_str);
            if (head_atom.predicate.empty()) continue;

            auto raw_terms = split_top_level_atoms(body_str);
            std::vector<ParsedAtom> body_atoms;
            for (const auto& rt : raw_terms) {
                ParsedAtom atom = parse_atom(rt);
                if (!atom.predicate.empty()) {
                    body_atoms.push_back(atom);
                    edges.push_back({head_atom.predicate, atom.predicate, atom.negated});
                }
            }
            rules.push_back({head_atom.predicate, head_atom.args, std::move(body_atoms)});
        }

        std::string strat_err;
        if (!StratificationValidator::validate(edges, strat_err)) {
            throw std::runtime_error("Datalog Stratification Error: " + strat_err);
        }

        uint16_t max_group_plus_one = explicit_num_groups;
        for (size_t i = 1; i < group_pairs.size(); i += 2) {
            uint16_t g1 = static_cast<uint16_t>(group_pairs[i] + 1);
            if (g1 > max_group_plus_one) max_group_plus_one = g1;
        }
        bool multi_lane = !seeds.empty() || !group_pairs.empty() || allow_deny_half > 0;

        std::unordered_map<std::string, std::vector<ParsedRule>> rules_by_head;
        std::vector<std::string> head_order;
        for (const auto& r : rules) {
            if (rules_by_head.find(r.head_predicate) == rules_by_head.end()) {
                head_order.push_back(r.head_predicate);
            }
            rules_by_head[r.head_predicate].push_back(r);
        }

        std::unordered_set<std::string> recursive_preds;
        for (const auto& hp : head_order) {
            if (StratificationValidator::is_recursive(hp, edges)) {
                recursive_preds.insert(hp);
            }
        }

        std::vector<impulse::compiler::AstPtr> steps;
        if (!group_pairs.empty()) {
            steps.push_back(impulse::compiler::ScmLaneInitGroups::from_inline_pairs(
                group_pairs, std::max<uint16_t>(1, max_group_plus_one)));
        } else if (!seeds.empty() || allow_deny_half > 0) {
            steps.push_back(impulse::compiler::ScmLaneInitSeeds::from_inline_seeds(seeds));
        }

        std::unordered_set<std::string> emitted_recursive_rels;
        std::vector<std::string> negated_preds;

        for (const auto& rule : rules) {
            bool rule_is_recursive = recursive_preds.count(rule.head_predicate) > 0;
            for (const auto& atom : rule.body) {
                if (atom.negated) {
                    negated_preds.push_back(atom.predicate);
                    continue;
                }
                if (atom.predicate == rule.head_predicate) {
                    continue;
                }
                if (rules_by_head.count(atom.predicate) > 0 && recursive_preds.count(atom.predicate) == 0) {
                    // Derived non-recursive IDB predicate already expanded by its own rule
                    continue;
                }
                auto dir = atom.reverse_direction
                    ? impulse::compiler::WalkDirection::REVERSE_CSC
                    : impulse::compiler::WalkDirection::FORWARD_CSR;

                if (multi_lane) {
                    if (!rule_is_recursive || emitted_recursive_rels.insert(atom.predicate).second) {
                        steps.push_back(std::make_shared<impulse::compiler::ScmLaneWalk>(
                            atom.predicate, -1, dir, rule_is_recursive, rule_is_recursive ? 0x04 : 0x00));
                    }
                } else {
                    if (rule_is_recursive) {
                        if (emitted_recursive_rels.insert(atom.predicate).second) {
                            auto inner_walk = (dir == impulse::compiler::WalkDirection::REVERSE_CSC)
                                ? impulse::compiler::ScmWalk::reverse(atom.predicate)
                                : impulse::compiler::ScmWalk::forward(atom.predicate);
                            steps.push_back(std::make_shared<impulse::compiler::ScmFixpoint>(
                                impulse::compiler::ScmProgram::of(inner_walk)));
                        }
                    } else {
                        auto w = (dir == impulse::compiler::WalkDirection::REVERSE_CSC)
                            ? impulse::compiler::ScmWalk::reverse(atom.predicate)
                            : impulse::compiler::ScmWalk::forward(atom.predicate);
                        steps.push_back(w);
                    }
                }
            }
        }

        if (multi_lane) {
            if (allow_deny_half > 0) {
                steps.push_back(impulse::compiler::ScmLanePruneMask::allow_deny_split(allow_deny_half));
            } else if (!deny_nodes.empty()) {
                steps.push_back(impulse::compiler::ScmLanePruneMask::from_inline_deny(deny_nodes));
            } else if (!negated_preds.empty()) {
                steps.push_back(impulse::compiler::ScmLanePruneMask::from_register(0, 0));
            }
        } else if (!negated_preds.empty()) {
            if (steps.empty()) {
                steps.push_back(impulse::compiler::ScmWalk::forward("default"));
            }
            impulse::compiler::AstPtr pos_node = (steps.size() == 1)
                ? steps[0]
                : std::make_shared<impulse::compiler::ScmProgram>(steps);
            steps.clear();
            for (const auto& neg_pred : negated_preds) {
                pos_node = impulse::compiler::ScmSetOp::diff(
                    pos_node, impulse::compiler::ScmWalk::forward(neg_pred));
            }
            steps.push_back(pos_node);
        }

        if (steps.empty()) {
            steps.push_back(impulse::compiler::ScmWalk::forward("default"));
        }
        steps.push_back(impulse::compiler::ScmCollect::bitset());
        IMPULSE_ASSERT(!steps.empty());
        IMPULSE_AUDIT_ASSERT(steps.back()->kind() == impulse::compiler::NodeKind::COLLECT);
        return std::make_shared<impulse::compiler::ScmProgram>(steps);
    }
};

} // namespace impulse::datalog

#endif // IMPULSE_DATALOG_HPP
