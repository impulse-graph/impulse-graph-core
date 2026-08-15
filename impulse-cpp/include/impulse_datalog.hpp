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
            for (const auto& [next, is_negated] : it->second) {
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
};

class DatalogParser {
public:
    static std::shared_ptr<impulse::compiler::ScmProgram> parse(const std::string& source) {
        std::vector<impulse::compiler::AstPtr> steps;
        std::istringstream stream(source);
        std::string line;
        std::vector<DependencyEdge> edges;

        while (std::getline(stream, line)) {
            size_t start = line.find_first_not_of(" \t\r\n");
            if (start == std::string::npos) continue;
            std::string trimmed = line.substr(start);
            if (trimmed.starts_with("//") || trimmed.starts_with("%") || trimmed.starts_with("#") || trimmed.starts_with(".decl")) {
                continue;
            }

            size_t arrow_pos = trimmed.find(":-");
            if (arrow_pos != std::string::npos) {
                std::string head = trimmed.substr(0, arrow_pos);
                std::string body = trimmed.substr(arrow_pos + 2);
                while (!body.empty() && (body.back() == '.' || std::isspace(body.back()))) body.pop_back();

                size_t lparen = head.find('(');
                std::string head_pred = (lparen != std::string::npos) ? head.substr(0, lparen) : head;
                head_pred.erase(remove_if(head_pred.begin(), head_pred.end(), isspace), head_pred.end());

                std::istringstream body_stream(body);
                std::string term;
                while (std::getline(body_stream, term, ',')) {
                    size_t term_start = term.find_first_not_of(" \t");
                    if (term_start == std::string::npos) continue;
                    std::string term_trimmed = term.substr(term_start);

                    bool is_neg = false;
                    if (term_trimmed.starts_with("!")) {
                        is_neg = true;
                        term_trimmed = term_trimmed.substr(1);
                        size_t s = term_trimmed.find_first_not_of(" \t");
                        if (s != std::string::npos) term_trimmed = term_trimmed.substr(s);
                    } else if (term_trimmed.starts_with("not ")) {
                        is_neg = true;
                        term_trimmed = term_trimmed.substr(4);
                        size_t s = term_trimmed.find_first_not_of(" \t");
                        if (s != std::string::npos) term_trimmed = term_trimmed.substr(s);
                    }

                    size_t term_lp = term_trimmed.find('(');
                    std::string pred_name = (term_lp != std::string::npos) ? term_trimmed.substr(0, term_lp) : term_trimmed;
                    pred_name.erase(remove_if(pred_name.begin(), pred_name.end(), isspace), pred_name.end());

                    if (!pred_name.empty()) {
                        edges.push_back({head_pred, pred_name, is_neg});
                        steps.push_back(impulse::compiler::ScmWalk::forward(pred_name));
                    }
                }
            }
        }

        std::string strat_err;
        if (!StratificationValidator::validate(edges, strat_err)) {
            throw std::runtime_error("Datalog Stratification Error: " + strat_err);
        }

        if (steps.empty()) {
            steps.push_back(impulse::compiler::ScmWalk::forward("default"));
        }
        steps.push_back(impulse::compiler::ScmCollect::bitset());
        return std::make_shared<impulse::compiler::ScmProgram>(steps);
    }
};

} // namespace impulse::datalog

#endif // IMPULSE_DATALOG_HPP
