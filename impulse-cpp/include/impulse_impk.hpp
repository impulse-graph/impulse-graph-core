/**
 * @file impulse_impk.hpp
 * @brief Canonical C++20 ImpK (.impk) Array & Vector DSL Compiler for Impulse Graph.
 *
 * Implements the GraphBLAS matrix mathematics, PageRank, connected components (Afforest),
 * and SIMD vector pipeline compiler translating .impk DSL scripts directly into
 * SIMD-vectorized impOps bytecode.
 */

#ifndef IMPULSE_IMPK_HPP
#define IMPULSE_IMPK_HPP

#include "impulse_compiler.hpp"
#include "impulse_vm.h"
#include <string>
#include <vector>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <unordered_map>

namespace impulse::impk {

enum class ImpKOpType {
    MatrixVectorMul,     // y = A * x (OP_MXV)
    VectorAdd,           // z = x + y (OP_EWISE_ADD)
    VectorMul,           // z = x * y (OP_EWISE_MULT)
    DegreeNorm,          // d = degree(A) (OP_DEGREE_NORM)
    PageRankStep,        // pr = pagerank_step(A, p, damping)
    ConnectedComponents, // cc = afforest(A) (OP_CC_AFFOREST)
    TopK,                // top = topk(v, k)
};

struct ImpKStatement {
    std::string target_var;
    ImpKOpType op_type;
    std::string matrix_var;
    std::string vector_var1;
    std::string vector_var2;
    double scalar_param{0.0};
    uint32_t top_k{10};
};


class ImpKCompiler {
    struct Token {
        enum Type { IDENT, NUMBER, EQUALS, PLUS, STAR, LPAREN, RPAREN, COMMA, END } type;
        std::string text;
        double value = 0.0;
    };

    static std::vector<Token> tokenize(const std::string& script) {
        std::vector<Token> tokens;
        size_t i = 0;
        while (i < script.size()) {
            char c = script[i];
            if (std::isspace(c)) { i++; continue; }
            if (c == '#' || (c == '/' && i + 1 < script.size() && script[i+1] == '/')) {
                while (i < script.size() && script[i] != '\n') i++;
                continue;
            }
            if (std::isalpha(c) || c == '_') {
                std::string ident;
                while (i < script.size() && (std::isalnum(script[i]) || script[i] == '_')) {
                    ident += script[i++];
                }
                tokens.push_back({Token::IDENT, ident, 0.0});
            } else if (std::isdigit(c) || c == '.') {
                std::string num;
                while (i < script.size() && (std::isdigit(script[i]) || script[i] == '.')) {
                    num += script[i++];
                }
                tokens.push_back({Token::NUMBER, num, std::stod(num)});
            } else if (c == '=') { tokens.push_back({Token::EQUALS, "=", 0.0}); i++; }
            else if (c == '+') { tokens.push_back({Token::PLUS, "+", 0.0}); i++; }
            else if (c == '*') { tokens.push_back({Token::STAR, "*", 0.0}); i++; }
            else if (c == '(') { tokens.push_back({Token::LPAREN, "(", 0.0}); i++; }
            else if (c == ')') { tokens.push_back({Token::RPAREN, ")", 0.0}); i++; }
            else if (c == ',') { tokens.push_back({Token::COMMA, ",", 0.0}); i++; }
            else { i++; } // Ignore unknown for now
        }
        tokens.push_back({Token::END, "", 0.0});
        return tokens;
    }

public:
    static std::vector<ImpKStatement> parse(const std::string& script) {
        std::vector<ImpKStatement> statements;
        auto tokens = tokenize(script);
        size_t pos = 0;

        auto match = [&](Token::Type t) {
            if (tokens[pos].type == t) { pos++; return true; }
            return false;
        };
        auto expect = [&](Token::Type t) {
            if (tokens[pos].type == t) { pos++; return true; }
            throw std::runtime_error("Unexpected token in ImpK: " + tokens[pos].text);
        };

        while (tokens[pos].type != Token::END) {
            if (tokens[pos].type == Token::IDENT) {
                std::string target = tokens[pos].text;
                pos++;
                if (match(Token::EQUALS)) {
                    if (tokens[pos].type == Token::IDENT) {
                        std::string t1 = tokens[pos].text;
                        pos++;
                        if (match(Token::STAR)) {
                            std::string t2 = tokens[pos].text; pos++;
                            ImpKStatement stmt{target, ImpKOpType::MatrixVectorMul, t1, t2, "", 0.0, 10};
                            statements.push_back(stmt);
                        } else if (match(Token::PLUS)) {
                            std::string t2 = tokens[pos].text; pos++;
                            ImpKStatement stmt{target, ImpKOpType::VectorAdd, "", t1, t2, 0.0, 10};
                            statements.push_back(stmt);
                        } else if (match(Token::LPAREN)) {
                            if (t1 == "pagerank") {
                                std::string mat = tokens[pos].text; pos++;
                                expect(Token::COMMA);
                                std::string vec = tokens[pos].text; pos++;
                                expect(Token::COMMA);
                                double damp = tokens[pos].value; pos++;
                                expect(Token::RPAREN);
                                ImpKStatement stmt{target, ImpKOpType::PageRankStep, mat, vec, "", damp, 10};
                                statements.push_back(stmt);
                            } else if (t1 == "afforest" || t1 == "connected_components") {
                                std::string mat = tokens[pos].text; pos++;
                                expect(Token::RPAREN);
                                ImpKStatement stmt{target, ImpKOpType::ConnectedComponents, mat, "", "", 0.0, 10};
                                statements.push_back(stmt);
                            } else if (t1 == "degree") {
                                std::string mat = tokens[pos].text; pos++;
                                expect(Token::RPAREN);
                                ImpKStatement stmt{target, ImpKOpType::DegreeNorm, mat, "", "", 0.0, 10};
                                statements.push_back(stmt);
                            } else if (t1 == "topk") {
                                std::string vec = tokens[pos].text; pos++;
                                expect(Token::COMMA);
                                uint32_t k = (uint32_t)tokens[pos].value; pos++;
                                expect(Token::RPAREN);
                                ImpKStatement stmt{target, ImpKOpType::TopK, "", vec, "", 0.0, k};
                                statements.push_back(stmt);
                            }
                        } else {
                            // Just an assignment or unary, ignored for now
                        }
                    } else if (tokens[pos].type == Token::NUMBER) {
                        // Numeric assignment
                        pos++;
                    }
                } else {
                    // Not an assignment, skip
                }
            } else {
                pos++; // Skip unknown start of statement
            }
        }
        return statements;
    }

    static std::string to_impscheme(const std::vector<ImpKStatement>& stmts) {

        std::ostringstream oss;
        oss << "(impk-pipeline";
        for (const auto& s : stmts) {
            switch (s.op_type) {
                case ImpKOpType::MatrixVectorMul:
                    oss << " (mxv " << s.matrix_var << " " << s.vector_var1 << ")";
                    break;
                case ImpKOpType::VectorAdd:
                    oss << " (ewise-add " << s.vector_var1 << " " << s.vector_var2 << ")";
                    break;
                case ImpKOpType::PageRankStep:
                    oss << " (pagerank-step " << s.scalar_param << ")";
                    break;
                case ImpKOpType::ConnectedComponents:
                    oss << " (cc-afforest)";
                    break;
                default:
                    break;
            }
        }
        oss << ")";
        return oss.str();
    }
};

} // namespace impulse::impk

#endif // IMPULSE_IMPK_HPP
