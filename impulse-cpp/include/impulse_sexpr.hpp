/**
 * @file impulse_sexpr.hpp
 * @brief Standard S-Expression parser for ImpScheme (.impscm).
 */

#ifndef IMPULSE_SEXPR_HPP
#define IMPULSE_SEXPR_HPP

#include <string>
#include <vector>
#include <cctype>
#include <stdexcept>
#include <memory>
#include "impulse_compiler.hpp"

namespace impulse::impscm {

using namespace impulse::compiler;

struct SExpr {
    std::string atom;
    std::vector<SExpr> list;
    bool is_list = false;
};

class SExprParser {
    std::string src;
    size_t pos = 0;

    void skip_whitespace() {
        while (pos < src.size()) {
            if (std::isspace(src[pos])) {
                pos++;
            } else if (src[pos] == ';') {
                while (pos < src.size() && src[pos] != '\n') pos++;
            } else {
                break;
            }
        }
    }

public:
    explicit SExprParser(std::string s) : src(std::move(s)) {}

    SExpr parse() {
        skip_whitespace();
        if (pos >= src.size()) return {};

        if (src[pos] == '(' || src[pos] == '[') {
            char close = (src[pos] == '(') ? ')' : ']';
            pos++;
            SExpr expr;
            expr.is_list = true;
            while (pos < src.size()) {
                skip_whitespace();
                if (pos >= src.size()) break;
                if (src[pos] == close) {
                    pos++;
                    break;
                }
                expr.list.push_back(parse());
            }
            return expr;
        } else if (src[pos] == '"') {
            pos++;
            SExpr expr;
            while (pos < src.size() && src[pos] != '"') {
                expr.atom += src[pos++];
            }
            if (pos < src.size()) pos++; // skip closing quote
            return expr;
        } else {
            SExpr expr;
            while (pos < src.size() && !std::isspace(src[pos]) && src[pos] != '(' && src[pos] != ')' && src[pos] != '[' && src[pos] != ']') {
                expr.atom += src[pos++];
            }
            return expr;
        }
    }
};

class ImpScmAstBuilder {
public:
        static AstPtr build(const SExpr& expr) {
        if (!expr.is_list || expr.list.empty()) {
            if (!expr.is_list && !expr.atom.empty()) {
                if (std::isdigit(expr.atom[0])) {
                    auto lit = std::make_shared<ScmLiteral>();
                    lit->type = ScmLiteral::LitType::INT;
                    lit->int_val = std::stoll(expr.atom);
                    return lit;
                }
                auto v = std::make_shared<ScmVarRef>();
                v->var = expr.atom;
                return v;
            }
            return nullptr;
        }
        const auto& head = expr.list[0].atom;

        if (head == "impk-pipeline" || head == "pipeline" || head == "query" || head == "module") {
            std::vector<AstPtr> steps;
            for (size_t i = 1; i < expr.list.size(); ++i) {
                if (expr.list[i].is_list && !expr.list[i].list.empty() && expr.list[i].list[0].atom == "define-query") {
                    return build(expr.list[i]);
                }
                if (expr.list[i].is_list && !expr.list[i].list.empty() && expr.list[i].list[0].atom == "define-kernel") {
                    continue;
                }
                auto step = build(expr.list[i]);
                if (step) steps.push_back(step);
            }
            return std::make_shared<ScmProgram>(std::move(steps));
        } else if (head == "define-query") {
            if (expr.list.size() > 2) {
                std::vector<AstPtr> steps;
                for (size_t i = 2; i < expr.list.size(); ++i) {
                    auto step = build(expr.list[i]);
                    if (step) steps.push_back(step);
                }
                return std::make_shared<ScmProgram>(std::move(steps));
            }
        } else if (head == "let" && expr.list.size() > 1) {
            auto let_node = std::make_shared<ScmLet>();
            const auto& bindings = expr.list[1];
            if (bindings.is_list) {
                for (const auto& bind : bindings.list) {
                    if (bind.is_list && bind.list.size() == 2) {
                        let_node->vars.push_back(bind.list[0].atom);
                        let_node->inits.push_back(build(bind.list[1]));
                    }
                }
            }
            for (size_t i = 2; i < expr.list.size(); ++i) {
                auto step = build(expr.list[i]);
                if (step) let_node->body.push_back(step);
            }
            return let_node;
        } else if (head == "loop-while") {
            if (expr.list.size() > 1) {
                auto loop_node = std::make_shared<ScmLoopWhile>();
                loop_node->condition = build(expr.list[1]);
                for (size_t i = 2; i < expr.list.size(); ++i) {
                    auto step = build(expr.list[i]);
                    if (step) loop_node->body.push_back(step);
                }
                return loop_node;
            }
        } else if (head == "set!" && expr.list.size() == 3) {
            auto set_node = std::make_shared<ScmSet>();
            set_node->var = expr.list[1].atom;
            set_node->expr = build(expr.list[2]);
            return set_node;
        } else if (head == "bitset:empty") {
            auto b = std::make_shared<ScmBitsetInit>();
            b->type = ScmBitsetInit::InitType::EMPTY;
            return b;
        } else if (head == "bitset:all") {
            auto b = std::make_shared<ScmBitsetInit>();
            b->type = ScmBitsetInit::InitType::ALL;
            return b;
        } else if (head == "bitset:from" && expr.list.size() == 2) {
            auto b = std::make_shared<ScmBitsetInit>();
            b->type = ScmBitsetInit::InitType::FROM_NODE;
            b->param = expr.list[1].atom;
            return b;
        } else if (head == "bitset:cardinality" && expr.list.size() == 2) {
            auto c = std::make_shared<ScmCardinality>();
            c->var = expr.list[1].atom;
            return c;
        } else if (head == ">" && expr.list.size() == 3) {
            // For (> (bitset:cardinality frontier) 0)
            // Just return the LHS cardinality node since our compiler currently handles this specifically
            return build(expr.list[1]);
        } else if (head == "csr-walk" || head == "g:walk-csr" || head == "mxv") {
            std::string rel = "edge";
            for (size_t i = 1; i < expr.list.size(); ++i) {
                if (!expr.list[i].is_list && expr.list[i].atom != "g" && expr.list[i].atom != "frontier" && expr.list[i].atom != "updateEdge") {
                    rel = expr.list[i].atom;
                }
            }
            return ScmWalk::forward(rel);
        } else if (head == "collect-bitset") {
            return ScmCollect::bitset();
        } else if (head == "set:difference" || head == "set:union" || head == "set:intersect") {
            if (expr.list.size() == 3) {
                auto op = std::make_shared<ScmSetOp>();
                if (head == "set:difference") op->op = ScmSetOp::DIFFERENCE;
                else if (head == "set:union") op->op = ScmSetOp::UNION;
                else op->op = ScmSetOp::INTERSECT;
                op->lhs = build(expr.list[1]);
                op->rhs = build(expr.list[2]);
                return op;
            }
        } else if (head == "return" && expr.list.size() == 2) {
            auto r = std::make_shared<ScmReturn>();
            r->expr = build(expr.list[1]);
            return r;
        }

        return nullptr;
    }

    static std::shared_ptr<ScmProgram> parse(const std::string& script) {
        SExprParser parser(script);
        SExpr root = parser.parse();
        
        auto ast = build(root);
        if (ast) {
            if (auto prog = std::dynamic_pointer_cast<ScmProgram>(ast)) {
                return prog;
            }
            std::vector<AstPtr> steps;
            steps.push_back(ast);
            return std::make_shared<ScmProgram>(std::move(steps));
        }

        throw std::runtime_error("Failed to parse S-Expression AST.");
    }
};

} // namespace impulse::impscm

#endif // IMPULSE_SEXPR_HPP
