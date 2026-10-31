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
#include <algorithm>
#include <cctype>

namespace impulse::impk {

enum class ImpKOpType {
    MatrixVectorMul,     // y = A * x (OP_MXV)
    VectorAdd,           // z = x + y (OP_EWISE_ADD)
    VectorMul,           // z = x .* y (OP_EWISE_MULT)
    DegreeNorm,          // d = degree(A) (OP_DEGREE_NORM)
    PageRankStep,        // pr = pagerank_step(A, p, damping)
    ConnectedComponents, // cc = afforest(A) (OP_CC_AFFOREST)
    TopK,                // top = topk(v, k)
    VectorAnnSearch,     // hits = ann_search(rel, q, k, ef_or_fused) (0xCA)
    VectorRerankDot,     // scores = rerank_dot(frontier, q, attr_id, metric) (0xCB)
    LaneInitSeeds,       // L = lane_seeds(s0, s1, ...) (0xC0)
    LaneInitGroups,      // L = lane_groups([u0, g0, ...], num_groups) (0xC1)
    LaneSetEdgeMask,     // L2 = lane_edge_mask(L, rel, ...) (0xC2)
    LaneSpmmCsr,         // Y = lane_spmm(A, L, flags) (0xC3)
    LaneSpmmCsc,         // Y = lane_spmm_csc(A, L, flags) (0xC4)
    LanePruneMask,       // L2 = lane_prune(L, mode, arg0, arg1) (0xC5)
    LaneFilterQuorum,    // bs = lane_quorum(L, min_k) (0xC6)
    LaneReduceAnd,       // mask = lane_reduce_and(L, mask) (0xC7)
    LaneReduceAttr,      // vals = lane_reduce_attr(L, attr_id, op) (0xC8)
    LaneSetCover,        // cover = lane_set_cover(L, req_mask) (0xC9)
};

struct ImpKStatement {
    std::string target_var;
    ImpKOpType op_type{ImpKOpType::MatrixVectorMul};
    std::string matrix_var;
    std::string vector_var1;
    std::string vector_var2;
    double scalar_param{0.0};
    uint32_t top_k{10};
    uint32_t int_param1{0};
    uint32_t int_param2{0};
    bool bool_param{false};
    std::vector<uint64_t> int_list{};
    std::vector<float> float_list{};

    ImpKStatement() = default;
    ImpKStatement(
        std::string target,
        ImpKOpType op,
        std::string mat,
        std::string v1,
        std::string v2,
        double sp = 0.0,
        uint32_t k = 10,
        uint32_t ip1 = 0,
        uint32_t ip2 = 0,
        bool bp = false,
        std::vector<uint64_t> il = {},
        std::vector<float> fl = {})
        : target_var(std::move(target)),
          op_type(op),
          matrix_var(std::move(mat)),
          vector_var1(std::move(v1)),
          vector_var2(std::move(v2)),
          scalar_param(sp),
          top_k(k),
          int_param1(ip1),
          int_param2(ip2),
          bool_param(bp),
          int_list(std::move(il)),
          float_list(std::move(fl)) {}
};


class ImpKCompiler {
    struct Token {
        enum Type { IDENT, NUMBER, STRING, EQUALS, PLUS, STAR, LPAREN, RPAREN, LBRACKET, RBRACKET, COMMA, END } type;
        std::string text;
        double value = 0.0;
    };

    static std::string to_lower_copy(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    static uint8_t parse_metric_name(const std::string& name) {
        std::string low = to_lower_copy(name);
        if (low == "l2" || low == "neg_l2" || low == "euclid") return 1;
        if (low == "cosine" || low == "cos") return 2;
        if (low == "hamming" || low == "bq1") return 3;
        return 0;
    }

    static uint8_t parse_reduce_op_name(const std::string& name) {
        std::string low = to_lower_copy(name);
        if (low == "sum" || low == "add") return 0;
        if (low == "min") return 2;
        if (low == "max") return 3;
        if (low == "argmin") return 4;
        if (low == "argmax") return 5;
        return 0;
    }

    static std::vector<Token> tokenize(const std::string& script) {
        std::vector<Token> tokens;
        size_t i = 0;
        while (i < script.size()) {
            char c = script[i];
            if (std::isspace(static_cast<unsigned char>(c)) || c == ';') { i++; continue; }
            if (c == '#' || c == '/') {
                while (i < script.size() && script[i] != '\n') i++;
                continue;
            }
            if (c == '"' || c == '\'') {
                char quote = c;
                i++;
                std::string str_lit;
                while (i < script.size() && script[i] != quote) {
                    str_lit += script[i++];
                }
                if (i < script.size()) i++;
                tokens.push_back({Token::STRING, str_lit, 0.0});
            } else if (c == '0' && i + 1 < script.size() && (script[i + 1] == 'x' || script[i + 1] == 'X')) {
                std::string hex_str = "0x";
                i += 2;
                while (i < script.size() && std::isxdigit(static_cast<unsigned char>(script[i]))) {
                    hex_str += script[i++];
                }
                double val = static_cast<double>(std::stoull(hex_str, nullptr, 16));
                tokens.push_back({Token::NUMBER, hex_str, val});
            } else if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
                std::string ident;
                while (i < script.size() && (std::isalnum(static_cast<unsigned char>(script[i])) || script[i] == '_' || script[i] == '.')) {
                    ident += script[i++];
                }
                tokens.push_back({Token::IDENT, ident, 0.0});
            } else if (std::isdigit(static_cast<unsigned char>(c)) ||
                       ((c == '-' || c == '.') && i + 1 < script.size() && std::isdigit(static_cast<unsigned char>(script[i + 1])))) {
                std::string num;
                if (c == '-') num += script[i++];
                while (i < script.size() && (std::isdigit(static_cast<unsigned char>(script[i])) || script[i] == '.')) {
                    num += script[i++];
                }
                tokens.push_back({Token::NUMBER, num, std::stod(num)});
            } else if (c == '=' || c == ':') { tokens.push_back({Token::EQUALS, "=", 0.0}); i++; }
            else if (c == '+') { tokens.push_back({Token::PLUS, "+", 0.0}); i++; }
            else if (c == '*' || c == '@') { tokens.push_back({Token::STAR, "*", 0.0}); i++; }
            else if (c == '(') { tokens.push_back({Token::LPAREN, "(", 0.0}); i++; }
            else if (c == ')') { tokens.push_back({Token::RPAREN, ")", 0.0}); i++; }
            else if (c == '[') { tokens.push_back({Token::LBRACKET, "[", 0.0}); i++; }
            else if (c == ']') { tokens.push_back({Token::RBRACKET, "]", 0.0}); i++; }
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

        auto peek_type = [&]() {
            return (pos < tokens.size()) ? tokens[pos].type : Token::END;
        };
        auto match = [&](Token::Type t) {
            if (pos < tokens.size() && tokens[pos].type == t) { pos++; return true; }
            return false;
        };
        auto expect = [&](Token::Type t) {
            if (pos < tokens.size() && tokens[pos].type == t) { pos++; return true; }
            throw std::runtime_error("ImpK parse error: unexpected token");
        };
        auto consume_ident_or_str = [&](const std::string& fallback) -> std::string {
            if (pos < tokens.size() && (tokens[pos].type == Token::IDENT || tokens[pos].type == Token::STRING)) {
                return tokens[pos++].text;
            }
            return fallback;
        };
        auto consume_number = [&](double fallback) -> double {
            if (pos < tokens.size() && tokens[pos].type == Token::NUMBER) {
                return tokens[pos++].value;
            }
            return fallback;
        };
        auto consume_bool_or_flags = [&]() -> bool {
            if (pos < tokens.size()) {
                if (tokens[pos].type == Token::IDENT) {
                    std::string low = to_lower_copy(tokens[pos++].text);
                    return (low == "true" || low == "fixpoint" || low == "fp");
                }
                if (tokens[pos].type == Token::NUMBER) {
                    double v = tokens[pos++].value;
                    return v != 0.0;
                }
            }
            return false;
        };
        auto consume_float_bracket_list = [&]() -> std::vector<float> {
            expect(Token::LBRACKET);
            std::vector<float> out;
            while (pos < tokens.size() && tokens[pos].type != Token::RBRACKET && tokens[pos].type != Token::END) {
                if (tokens[pos].type == Token::NUMBER) {
                    out.push_back(static_cast<float>(tokens[pos].value));
                }
                pos++;
                match(Token::COMMA);
            }
            expect(Token::RBRACKET);
            return out;
        };
        auto consume_u64_bracket_list = [&]() -> std::vector<uint64_t> {
            expect(Token::LBRACKET);
            std::vector<uint64_t> out;
            while (pos < tokens.size() && tokens[pos].type != Token::RBRACKET && tokens[pos].type != Token::END) {
                if (tokens[pos].type == Token::NUMBER) {
                    out.push_back(static_cast<uint64_t>(tokens[pos].value));
                }
                pos++;
                match(Token::COMMA);
            }
            expect(Token::RBRACKET);
            return out;
        };
        auto consume_remaining_u64_args = [&]() -> std::vector<uint64_t> {
            std::vector<uint64_t> out;
            while (pos < tokens.size() && tokens[pos].type != Token::RPAREN && tokens[pos].type != Token::END) {
                if (tokens[pos].type == Token::NUMBER) {
                    out.push_back(static_cast<uint64_t>(tokens[pos].value));
                }
                pos++;
                match(Token::COMMA);
            }
            return out;
        };

        while (pos < tokens.size() && tokens[pos].type != Token::END) {
            if (tokens[pos].type == Token::IDENT) {
                std::string target = tokens[pos].text;
                pos++;
                if (match(Token::EQUALS)) {
                    if (pos < tokens.size() && tokens[pos].type == Token::IDENT) {
                        std::string t1 = tokens[pos].text;
                        pos++;
                        if (match(Token::STAR)) {
                            if (pos < tokens.size() && tokens[pos].type == Token::IDENT) {
                                std::string t2 = tokens[pos].text; pos++;
                                ImpKStatement stmt{target, ImpKOpType::MatrixVectorMul, t1, t2, "", 0.0, 10};
                                statements.push_back(stmt);
                            }
                        } else if (match(Token::PLUS)) {
                            if (pos < tokens.size() && tokens[pos].type == Token::IDENT) {
                                std::string t2 = tokens[pos].text; pos++;
                                ImpKStatement stmt{target, ImpKOpType::VectorAdd, "", t1, t2, 0.0, 10};
                                statements.push_back(stmt);
                            }
                        } else if (match(Token::LPAREN)) {
                            std::string fn = to_lower_copy(t1);
                            if (fn == "pagerank") {
                                std::string mat = (pos < tokens.size()) ? tokens[pos].text : "edge"; if (pos < tokens.size()) pos++;
                                expect(Token::COMMA);
                                std::string vec = (pos < tokens.size()) ? tokens[pos].text : "x"; if (pos < tokens.size()) pos++;
                                expect(Token::COMMA);
                                double damp = (pos < tokens.size()) ? tokens[pos].value : 0.85; if (pos < tokens.size()) pos++;
                                expect(Token::RPAREN);
                                ImpKStatement stmt{target, ImpKOpType::PageRankStep, mat, vec, "", damp, 10};
                                statements.push_back(stmt);
                            } else if (fn == "afforest" || fn == "connected_components") {
                                std::string mat = (pos < tokens.size()) ? tokens[pos].text : "edge"; if (pos < tokens.size()) pos++;
                                expect(Token::RPAREN);
                                ImpKStatement stmt{target, ImpKOpType::ConnectedComponents, mat, "", "", 0.0, 10};
                                statements.push_back(stmt);
                            } else if (fn == "degree") {
                                std::string mat = (pos < tokens.size()) ? tokens[pos].text : "edge"; if (pos < tokens.size()) pos++;
                                expect(Token::RPAREN);
                                ImpKStatement stmt{target, ImpKOpType::DegreeNorm, mat, "", "", 0.0, 10};
                                statements.push_back(stmt);
                            } else if (fn == "ewise_mul" || fn == "vmul") {
                                std::string v1 = consume_ident_or_str("x");
                                expect(Token::COMMA);
                                std::string v2 = consume_ident_or_str("y");
                                expect(Token::RPAREN);
                                ImpKStatement stmt{target, ImpKOpType::VectorMul, "", v1, v2, 0.0, 10};
                                statements.push_back(stmt);
                            } else if (fn == "topk") {
                                std::string vec = (pos < tokens.size()) ? tokens[pos].text : "v"; if (pos < tokens.size()) pos++;
                                expect(Token::COMMA);
                                uint32_t k = (pos < tokens.size()) ? static_cast<uint32_t>(tokens[pos].value) : 10; if (pos < tokens.size()) pos++;
                                expect(Token::RPAREN);
                                ImpKStatement stmt{target, ImpKOpType::TopK, "", vec, "", 0.0, k};
                                statements.push_back(stmt);
                            } else if (fn == "ann_search" || fn == "vector_ann_search") {
                                std::vector<float> q_vec;
                                std::string rel_or_q;
                                std::string q_var;
                                uint32_t q_reg = 2;
                                if (peek_type() == Token::LBRACKET) {
                                    q_vec = consume_float_bracket_list();
                                } else if (peek_type() == Token::NUMBER) {
                                    q_reg = static_cast<uint32_t>(consume_number(2));
                                } else {
                                    rel_or_q = consume_ident_or_str("q");
                                }
                                expect(Token::COMMA);
                                uint32_t k = 10;
                                uint32_t attr_id = 0;
                                bool fused = false;
                                if (peek_type() == Token::LBRACKET) {
                                    q_vec = consume_float_bracket_list();
                                    expect(Token::COMMA);
                                    k = static_cast<uint32_t>(consume_number(10));
                                } else if (peek_type() == Token::IDENT || peek_type() == Token::STRING) {
                                    q_var = consume_ident_or_str("q");
                                    expect(Token::COMMA);
                                    k = static_cast<uint32_t>(consume_number(10));
                                } else {
                                    k = static_cast<uint32_t>(consume_number(10));
                                    q_var = rel_or_q;
                                }
                                if (match(Token::COMMA)) {
                                    if (peek_type() == Token::IDENT) {
                                        fused = consume_bool_or_flags();
                                    } else {
                                        uint32_t val = static_cast<uint32_t>(consume_number(0));
                                        if (!q_var.empty() && q_var != rel_or_q) {
                                            // 4-arg form ann_search(rel, query_vec, k, ef_or_fused)
                                            if (val == 1) fused = true;
                                            else attr_id = 0;
                                        } else {
                                            attr_id = val;
                                        }
                                    }
                                }
                                if (match(Token::COMMA)) {
                                    fused = consume_bool_or_flags();
                                }
                                expect(Token::RPAREN);
                                ImpKStatement stmt{target, ImpKOpType::VectorAnnSearch, rel_or_q, q_var, "", 0.0, k, attr_id, q_reg, fused, {}, q_vec};
                                statements.push_back(stmt);
                            } else if (fn == "rerank_dot" || fn == "vector_rerank_dot" || fn == "rerank") {
                                std::string frontier_var = consume_ident_or_str("cands");
                                expect(Token::COMMA);
                                std::vector<float> q_vec;
                                std::string q_var;
                                uint32_t q_reg = 2;
                                if (peek_type() == Token::LBRACKET) {
                                    q_vec = consume_float_bracket_list();
                                } else if (peek_type() == Token::NUMBER) {
                                    q_reg = static_cast<uint32_t>(consume_number(2));
                                } else {
                                    q_var = consume_ident_or_str("q");
                                }
                                uint32_t attr_id = 0;
                                uint32_t metric = 0;
                                if (match(Token::COMMA)) {
                                    if (peek_type() == Token::IDENT || peek_type() == Token::STRING) {
                                        metric = parse_metric_name(consume_ident_or_str("dot"));
                                    } else {
                                        attr_id = static_cast<uint32_t>(consume_number(0));
                                    }
                                }
                                if (match(Token::COMMA)) {
                                    if (peek_type() == Token::IDENT || peek_type() == Token::STRING) {
                                        metric = parse_metric_name(consume_ident_or_str("dot"));
                                    } else {
                                        metric = static_cast<uint32_t>(consume_number(0));
                                    }
                                }
                                expect(Token::RPAREN);
                                ImpKStatement stmt{target, ImpKOpType::VectorRerankDot, "", frontier_var, q_var, 0.0, q_reg, attr_id, metric, false, {}, q_vec};
                                statements.push_back(stmt);
                            } else if (fn == "lane_seeds" || fn == "lane_init_seeds") {
                                std::vector<uint64_t> seeds;
                                if (peek_type() == Token::LBRACKET) {
                                    seeds = consume_u64_bracket_list();
                                } else {
                                    seeds = consume_remaining_u64_args();
                                }
                                expect(Token::RPAREN);
                                ImpKStatement stmt{target, ImpKOpType::LaneInitSeeds, "", "", "", 0.0, static_cast<uint32_t>(seeds.size()), 0, 0, false, seeds, {}};
                                statements.push_back(stmt);
                            } else if (fn == "lane_groups" || fn == "lane_init_groups") {
                                std::vector<uint64_t> pairs;
                                uint32_t num_groups = 1;
                                if (peek_type() == Token::LBRACKET) {
                                    pairs = consume_u64_bracket_list();
                                    if (match(Token::COMMA)) {
                                        num_groups = static_cast<uint32_t>(consume_number(1));
                                    }
                                } else {
                                    uint32_t preg = static_cast<uint32_t>(consume_number(0));
                                    if (match(Token::COMMA)) {
                                        num_groups = static_cast<uint32_t>(consume_number(1));
                                    }
                                    pairs.push_back(preg);
                                }
                                expect(Token::RPAREN);
                                ImpKStatement stmt{target, ImpKOpType::LaneInitGroups, "", "", "", 0.0, 0, num_groups, 0, false, pairs, {}};
                                statements.push_back(stmt);
                            } else if (fn == "lane_edge_mask" || fn == "lane_set_edge_mask") {
                                std::string arg0 = consume_ident_or_str("edge");
                                std::string rel = arg0;
                                std::vector<uint64_t> pairs;
                                if (match(Token::COMMA)) {
                                    if (peek_type() == Token::LBRACKET) {
                                        pairs = consume_u64_bracket_list();
                                    } else if (peek_type() == Token::IDENT || peek_type() == Token::STRING) {
                                        rel = consume_ident_or_str("edge");
                                        if (match(Token::COMMA)) {
                                            if (peek_type() == Token::LBRACKET) pairs = consume_u64_bracket_list();
                                            else pairs = consume_remaining_u64_args();
                                        }
                                    } else {
                                        pairs = consume_remaining_u64_args();
                                    }
                                }
                                expect(Token::RPAREN);
                                ImpKStatement stmt{target, ImpKOpType::LaneSetEdgeMask, rel, arg0, "", 0.0, 0, 0, 0, false, pairs, {}};
                                statements.push_back(stmt);
                            } else if (fn == "lane_spmm" || fn == "lane_walk_csr" || fn == "lane_spmm_csc" || fn == "lane_walk_csc") {
                                bool is_csc = (fn == "lane_spmm_csc" || fn == "lane_walk_csc");
                                std::string rel = consume_ident_or_str("edge");
                                std::string l_var;
                                bool fp = false;
                                if (match(Token::COMMA)) {
                                    if (peek_type() == Token::IDENT || peek_type() == Token::STRING) {
                                        std::string tok = consume_ident_or_str("");
                                        std::string low = to_lower_copy(tok);
                                        if (low == "true" || low == "fixpoint" || low == "fp") {
                                            fp = true;
                                        } else {
                                            l_var = tok;
                                        }
                                    } else {
                                        fp = consume_bool_or_flags();
                                    }
                                }
                                if (match(Token::COMMA)) {
                                    fp = consume_bool_or_flags();
                                }
                                expect(Token::RPAREN);
                                ImpKStatement stmt{target, is_csc ? ImpKOpType::LaneSpmmCsc : ImpKOpType::LaneSpmmCsr, rel, l_var, "", 0.0, 0, 0, 0, fp, {}, {}};
                                statements.push_back(stmt);
                            } else if (fn == "lane_prune" || fn == "lane_prune_mask") {
                                std::string l_var = consume_ident_or_str("L");
                                expect(Token::COMMA);
                                uint32_t mode = static_cast<uint32_t>(consume_number(0));
                                uint32_t arg0 = 0;
                                uint32_t arg1 = 0;
                                if (match(Token::COMMA)) arg0 = static_cast<uint32_t>(consume_number(0));
                                if (match(Token::COMMA)) arg1 = static_cast<uint32_t>(consume_number(0));
                                expect(Token::RPAREN);
                                ImpKStatement stmt{target, ImpKOpType::LanePruneMask, "", l_var, "", 0.0, arg1, mode, arg0, false, {}, {}};
                                statements.push_back(stmt);
                            } else if (fn == "lane_quorum" || fn == "lane_filter_quorum") {
                                std::string l_var = consume_ident_or_str("L");
                                expect(Token::COMMA);
                                uint32_t min_k = static_cast<uint32_t>(consume_number(1));
                                uint32_t mask_reg = 0xFF;
                                if (match(Token::COMMA)) mask_reg = static_cast<uint32_t>(consume_number(0xFF));
                                expect(Token::RPAREN);
                                ImpKStatement stmt{target, ImpKOpType::LaneFilterQuorum, "", l_var, "", 0.0, min_k, mask_reg, 0, false, {}, {}};
                                statements.push_back(stmt);
                            } else if (fn == "lane_reduce_and") {
                                std::string l_var = consume_ident_or_str("L");
                                uint32_t mask = 0;
                                if (match(Token::COMMA)) mask = static_cast<uint32_t>(consume_number(0));
                                expect(Token::RPAREN);
                                ImpKStatement stmt{target, ImpKOpType::LaneReduceAnd, "", l_var, "", 0.0, 0, mask, 0, false, {}, {}};
                                statements.push_back(stmt);
                            } else if (fn == "lane_reduce_attr") {
                                std::string l_var = consume_ident_or_str("L");
                                expect(Token::COMMA);
                                uint32_t attr_id = static_cast<uint32_t>(consume_number(0));
                                uint32_t red_op = 0;
                                if (match(Token::COMMA)) {
                                    if (peek_type() == Token::IDENT || peek_type() == Token::STRING) {
                                        red_op = parse_reduce_op_name(consume_ident_or_str("sum"));
                                    } else {
                                        red_op = static_cast<uint32_t>(consume_number(0));
                                    }
                                }
                                expect(Token::RPAREN);
                                ImpKStatement stmt{target, ImpKOpType::LaneReduceAttr, "", l_var, "", 0.0, 0, attr_id, red_op, false, {}, {}};
                                statements.push_back(stmt);
                            } else if (fn == "lane_set_cover") {
                                std::string l_var = consume_ident_or_str("L");
                                uint32_t mask_reg = 0xFF;
                                uint32_t mode = 0;
                                if (match(Token::COMMA)) mask_reg = static_cast<uint32_t>(consume_number(0xFF));
                                if (match(Token::COMMA)) mode = static_cast<uint32_t>(consume_number(0));
                                expect(Token::RPAREN);
                                ImpKStatement stmt{target, ImpKOpType::LaneSetCover, "", l_var, "", 0.0, 0, mask_reg, mode, false, {}, {}};
                                statements.push_back(stmt);
                            } else {
                                while (pos < tokens.size() && tokens[pos].type != Token::RPAREN && tokens[pos].type != Token::END) {
                                    pos++;
                                }
                                match(Token::RPAREN);
                            }
                        } else {
                            // Just an assignment or unary
                        }
                    } else if (pos < tokens.size() && tokens[pos].type == Token::NUMBER) {
                        pos++;
                    }
                }
            } else {
                pos++;
            }
        }
        IMPULSE_AUDIT_ASSERT(!tokens.empty());
        return statements;
    }

    static std::string to_impscheme(const std::vector<ImpKStatement>& stmts) {
        if (stmts.empty()) {
            return "(impk-pipeline (csr-walk \"edge\") (collect-bitset))";
        }
        std::ostringstream oss;
        oss << "(impk-pipeline";
        for (const auto& s : stmts) {
            IMPULSE_ASSERT(!s.target_var.empty() || s.op_type == ImpKOpType::ConnectedComponents);
            switch (s.op_type) {
                case ImpKOpType::MatrixVectorMul:
                    oss << " (mxv " << s.matrix_var << " " << s.vector_var1 << ")";
                    break;
                case ImpKOpType::VectorAdd:
                    oss << " (ewise-add " << s.vector_var1 << " " << s.vector_var2 << ")";
                    break;
                case ImpKOpType::VectorMul:
                    oss << " (ewise-mul " << s.vector_var1 << " " << s.vector_var2 << ")";
                    break;
                case ImpKOpType::DegreeNorm:
                    oss << " (degree-norm " << s.matrix_var << ")";
                    break;
                case ImpKOpType::PageRankStep:
                    oss << " (pagerank-step " << s.scalar_param << ")";
                    break;
                case ImpKOpType::ConnectedComponents:
                    oss << " (cc-afforest)";
                    break;
                case ImpKOpType::TopK:
                    oss << " (topk " << s.vector_var1 << " " << s.top_k << ")";
                    break;
                case ImpKOpType::VectorAnnSearch:
                    oss << " (vector-ann-search " << s.int_param2 << " " << s.top_k << " "
                        << s.int_param1 << " " << (s.bool_param ? 1 : 0) << ")";
                    break;
                case ImpKOpType::VectorRerankDot:
                    oss << " (vector-rerank-dot " << s.top_k << " " << s.int_param1 << " " << s.int_param2 << ")";
                    break;
                case ImpKOpType::LaneInitSeeds:
                    oss << " (lane-init-seeds 0 0)";
                    break;
                case ImpKOpType::LaneInitGroups:
                    oss << " (lane-init-groups 0 " << std::max<uint32_t>(1, s.int_param1) << ")";
                    break;
                case ImpKOpType::LaneSetEdgeMask:
                    oss << " (lane-set-edge-mask 0 " << (s.matrix_var.empty() ? "edge" : s.matrix_var) << ")";
                    break;
                case ImpKOpType::LaneSpmmCsr:
                    oss << " (lane-walk-csr " << (s.matrix_var.empty() ? "edge" : s.matrix_var)
                        << (s.bool_param ? " 1" : "") << ")";
                    break;
                case ImpKOpType::LaneSpmmCsc:
                    oss << " (lane-walk-csc " << (s.matrix_var.empty() ? "edge" : s.matrix_var)
                        << (s.bool_param ? " 1" : "") << ")";
                    break;
                case ImpKOpType::LanePruneMask:
                    oss << " (lane-prune-mask " << s.int_param1 << " " << s.int_param2 << " " << s.top_k << ")";
                    break;
                case ImpKOpType::LaneFilterQuorum:
                    oss << " (lane-filter-quorum " << s.top_k << " " << s.int_param1 << ")";
                    break;
                case ImpKOpType::LaneReduceAnd:
                    oss << " (lane-reduce-and)";
                    break;
                case ImpKOpType::LaneReduceAttr:
                    oss << " (lane-reduce-attr " << s.int_param1 << " " << s.int_param2 << ")";
                    break;
                case ImpKOpType::LaneSetCover:
                    oss << " (lane-set-cover " << s.int_param1 << " " << s.int_param2 << ")";
                    break;
            }
        }
        oss << " (collect-bitset))";
        return oss.str();
    }

    static std::shared_ptr<impulse::compiler::ScmProgram> lower_to_program(const std::vector<ImpKStatement>& stmts) {
        using namespace impulse::compiler;
        std::vector<AstPtr> steps;
        for (const auto& s : stmts) {
            switch (s.op_type) {
                case ImpKOpType::MatrixVectorMul:
                case ImpKOpType::DegreeNorm:
                case ImpKOpType::PageRankStep:
                case ImpKOpType::ConnectedComponents:
                    steps.push_back(ScmWalk::forward(s.matrix_var.empty() ? "edge" : s.matrix_var));
                    break;
                case ImpKOpType::VectorAdd:
                case ImpKOpType::VectorMul:
                    steps.push_back(ScmWalk::forward("edge"));
                    break;
                case ImpKOpType::TopK:
                    steps.push_back(std::make_shared<ScmReduce>(ScmReduce::Op::ARGMAX));
                    break;
                case ImpKOpType::VectorAnnSearch:
                    steps.push_back(std::make_shared<ScmVectorAnnSearch>(
                        static_cast<uint16_t>(s.int_param2),
                        static_cast<uint16_t>(s.top_k),
                        static_cast<uint16_t>(s.int_param1),
                        s.bool_param));
                    break;
                case ImpKOpType::VectorRerankDot:
                    steps.push_back(std::make_shared<ScmVectorRerankDot>(
                        static_cast<uint16_t>(s.top_k),
                        static_cast<uint16_t>(s.int_param1),
                        static_cast<uint8_t>(s.int_param2)));
                    break;
                case ImpKOpType::LaneInitSeeds:
                    steps.push_back(ScmLaneInitSeeds::from_inline_seeds(s.int_list, 0, 0));
                    break;
                case ImpKOpType::LaneInitGroups:
                    steps.push_back(ScmLaneInitGroups::from_inline_pairs(
                        s.int_list, static_cast<uint16_t>(std::max<uint32_t>(1, s.int_param1)), 0));
                    break;
                case ImpKOpType::LaneSetEdgeMask:
                    steps.push_back(std::make_shared<ScmLaneSetEdgeMask>(
                        0, s.matrix_var.empty() ? "edge" : s.matrix_var));
                    break;
                case ImpKOpType::LaneSpmmCsr:
                    steps.push_back(ScmLaneWalk::forward(
                        s.matrix_var.empty() ? "edge" : s.matrix_var, s.bool_param, s.bool_param ? 0x04 : 0x00));
                    break;
                case ImpKOpType::LaneSpmmCsc:
                    steps.push_back(ScmLaneWalk::reverse(
                        s.matrix_var.empty() ? "edge" : s.matrix_var, s.bool_param, s.bool_param ? 0x04 : 0x00));
                    break;
                case ImpKOpType::LanePruneMask:
                    steps.push_back(std::make_shared<ScmLanePruneMask>(
                        static_cast<uint8_t>(s.int_param1),
                        static_cast<uint16_t>(s.int_param2),
                        static_cast<uint16_t>(s.top_k)));
                    break;
                case ImpKOpType::LaneFilterQuorum:
                    steps.push_back(std::make_shared<ScmLaneFilterQuorum>(
                        static_cast<uint16_t>(s.top_k),
                        static_cast<uint16_t>(s.int_param1)));
                    break;
                case ImpKOpType::LaneReduceAnd:
                    steps.push_back(std::make_shared<ScmLaneReduceAnd>());
                    break;
                case ImpKOpType::LaneReduceAttr:
                    steps.push_back(std::make_shared<ScmLaneReduceAttr>(
                        static_cast<uint16_t>(s.int_param1),
                        static_cast<uint8_t>(s.int_param2)));
                    break;
                case ImpKOpType::LaneSetCover:
                    steps.push_back(std::make_shared<ScmLaneSetCover>(
                        static_cast<uint16_t>(s.int_param1),
                        static_cast<uint8_t>(s.int_param2)));
                    break;
            }
        }
        if (steps.empty()) {
            steps.push_back(ScmWalk::forward("edge"));
        }
        steps.push_back(ScmCollect::bitset());
        return std::make_shared<ScmProgram>(std::move(steps));
    }

    static std::shared_ptr<impulse::compiler::ScmProgram> compile(const std::string& script) {
        return lower_to_program(parse(script));
    }
};

} // namespace impulse::impk

#endif // IMPULSE_IMPK_HPP
