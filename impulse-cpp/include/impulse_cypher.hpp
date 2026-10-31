#ifndef IMPULSE_CYPHER_HPP
#define IMPULSE_CYPHER_HPP

#include "impulse_compiler.hpp"
#include <string>
#include <vector>
#include <memory>
#include <stdexcept>
#include <cctype>
#include <algorithm>

namespace impulse::compiler {

enum class CypherTokenType {
    KW_MATCH,
    KW_WHERE,
    KW_RETURN,
    KW_AND,
    KW_OR,
    KW_NOT,
    KW_COUNT,
    KW_IN,
    KW_WITH,
    KW_AS,
    KW_ORDER,
    KW_BY,
    KW_DESC,
    KW_ASC,
    KW_LIMIT,

    LPAREN,       // (
    RPAREN,       // )
    LBRACKET,     // [
    RBRACKET,     // ]
    COLON,        // :
    COMMA,        // ,
    DOT,          // .
    PIPE,         // |
    DASH,         // -
    ARROW_RIGHT,  // ->
    ARROW_LEFT,   // <-
    STAR,         // *

    EQ,           // =
    EQ_EQ,        // ==
    NEQ,          // != or <>
    LT,           // <
    LTE,          // <=
    GT,           // >
    GTE,          // >=

    IDENTIFIER,
    PARAM,        // $paramName
    STRING_LITERAL,
    NUMBER_LITERAL,

    TOK_EOF,
    TOK_ERROR
};

struct CypherToken {
    CypherTokenType type;
    std::string lexeme;
    size_t start_pos;
    int line;
    int col;
};

class CypherLexer {
public:
    explicit CypherLexer(std::string source) : src_(std::move(source)) {}

    CypherToken next_token() {
        skip_whitespace_and_comments();

        if (is_at_end()) {
            return {CypherTokenType::TOK_EOF, "", cursor_, line_, col_};
        }

        size_t start_pos = cursor_;
        int start_line = line_;
        int start_col = col_;
        char c = advance();

        switch (c) {
            case '(': return {CypherTokenType::LPAREN, "(", start_pos, start_line, start_col};
            case ')': return {CypherTokenType::RPAREN, ")", start_pos, start_line, start_col};
            case '[': return {CypherTokenType::LBRACKET, "[", start_pos, start_line, start_col};
            case ']': return {CypherTokenType::RBRACKET, "]", start_pos, start_line, start_col};
            case ':': return {CypherTokenType::COLON, ":", start_pos, start_line, start_col};
            case ',': return {CypherTokenType::COMMA, ",", start_pos, start_line, start_col};
            case '.': return {CypherTokenType::DOT, ".", start_pos, start_line, start_col};
            case '|': return {CypherTokenType::PIPE, "|", start_pos, start_line, start_col};
            case '*': return {CypherTokenType::STAR, "*", start_pos, start_line, start_col};

            case '-':
                if (match('>')) {
                    return {CypherTokenType::ARROW_RIGHT, "->", start_pos, start_line, start_col};
                }
                return {CypherTokenType::DASH, "-", start_pos, start_line, start_col};

            case '<':
                if (match('-')) {
                    return {CypherTokenType::ARROW_LEFT, "<-", start_pos, start_line, start_col};
                }
                if (match('>')) {
                    return {CypherTokenType::NEQ, "<>", start_pos, start_line, start_col};
                }
                if (match('=')) {
                    return {CypherTokenType::LTE, "<=", start_pos, start_line, start_col};
                }
                return {CypherTokenType::LT, "<", start_pos, start_line, start_col};

            case '>':
                if (match('=')) {
                    return {CypherTokenType::GTE, ">=", start_pos, start_line, start_col};
                }
                return {CypherTokenType::GT, ">", start_pos, start_line, start_col};

            case '=':
                if (match('=')) {
                    return {CypherTokenType::EQ_EQ, "==", start_pos, start_line, start_col};
                }
                return {CypherTokenType::EQ, "=", start_pos, start_line, start_col};

            case '!':
                if (match('=')) {
                    return {CypherTokenType::NEQ, "!=", start_pos, start_line, start_col};
                }
                return {CypherTokenType::TOK_ERROR, "Unexpected '!'", start_pos, start_line, start_col};

            case '$':
                return scan_parameter(start_pos, start_line, start_col);

            case '`':
                return scan_backticked_identifier(start_pos, start_line, start_col);

            case '\'':
            case '"':
                return scan_string(c, start_pos, start_line, start_col);

            default:
                if (std::isdigit(static_cast<unsigned char>(c))) {
                    return scan_number(start_pos, start_line, start_col);
                }
                if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
                    return scan_identifier_or_keyword(start_pos, start_line, start_col);
                }
                return {CypherTokenType::TOK_ERROR, std::string("Unexpected char: ") + c, start_pos, start_line, start_col};
        }
    }

private:
    std::string src_;
    size_t cursor_ = 0;
    int line_ = 1;
    int col_ = 1;

    CypherToken scan_parameter(size_t start_pos, int start_line, int start_col) {
        while (!is_at_end() && (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_')) {
            advance();
        }
        return {CypherTokenType::PARAM, src_.substr(start_pos, cursor_ - start_pos), start_pos, start_line, start_col};
    }

    CypherToken scan_backticked_identifier(size_t start_pos, int start_line, int start_col) {
        size_t content_start = cursor_;
        while (!is_at_end() && peek() != '`') {
            if (peek() == '\n') { line_++; col_ = 1; }
            advance();
        }
        if (is_at_end()) {
            return {CypherTokenType::TOK_ERROR, "Unterminated backtick identifier", start_pos, start_line, start_col};
        }
        std::string lexeme = src_.substr(content_start, cursor_ - content_start);
        advance(); // consume closing `
        return {CypherTokenType::IDENTIFIER, lexeme, start_pos, start_line, start_col};
    }

    CypherToken scan_string(char quote, size_t start_pos, int start_line, int start_col) {
        size_t content_start = cursor_;
        while (!is_at_end() && peek() != quote) {
            if (peek() == '\n') { line_++; col_ = 1; }
            advance();
        }
        if (is_at_end()) {
            return {CypherTokenType::TOK_ERROR, "Unterminated string literal", start_pos, start_line, start_col};
        }
        std::string content = src_.substr(content_start, cursor_ - content_start);
        advance(); // consume closing quote
        return {CypherTokenType::STRING_LITERAL, content, start_pos, start_line, start_col};
    }

    CypherToken scan_number(size_t start_pos, int start_line, int start_col) {
        while (!is_at_end() && std::isdigit(static_cast<unsigned char>(peek()))) {
            advance();
        }
        if (!is_at_end() && peek() == '.' && std::isdigit(static_cast<unsigned char>(peek_next()))) {
            advance();
            while (!is_at_end() && std::isdigit(static_cast<unsigned char>(peek()))) {
                advance();
            }
        }
        return {CypherTokenType::NUMBER_LITERAL, src_.substr(start_pos, cursor_ - start_pos), start_pos, start_line, start_col};
    }

    CypherToken scan_identifier_or_keyword(size_t start_pos, int start_line, int start_col) {
        while (!is_at_end() && (std::isalnum(static_cast<unsigned char>(peek())) || peek() == '_' || peek() == '>')) {
            advance();
        }
        std::string lexeme = src_.substr(start_pos, cursor_ - start_pos);
        std::string upper = lexeme;
        std::transform(upper.begin(), upper.end(), upper.begin(), ::toupper);

        CypherTokenType type = CypherTokenType::IDENTIFIER;
        if (upper == "MATCH") type = CypherTokenType::KW_MATCH;
        else if (upper == "WHERE") type = CypherTokenType::KW_WHERE;
        else if (upper == "RETURN") type = CypherTokenType::KW_RETURN;
        else if (upper == "AND") type = CypherTokenType::KW_AND;
        else if (upper == "OR") type = CypherTokenType::KW_OR;
        else if (upper == "NOT") type = CypherTokenType::KW_NOT;
        else if (upper == "COUNT") type = CypherTokenType::KW_COUNT;
        else if (upper == "IN") type = CypherTokenType::KW_IN;
        else if (upper == "WITH") type = CypherTokenType::KW_WITH;
        else if (upper == "AS") type = CypherTokenType::KW_AS;
        else if (upper == "ORDER") type = CypherTokenType::KW_ORDER;
        else if (upper == "BY") type = CypherTokenType::KW_BY;
        else if (upper == "DESC") type = CypherTokenType::KW_DESC;
        else if (upper == "ASC") type = CypherTokenType::KW_ASC;
        else if (upper == "LIMIT") type = CypherTokenType::KW_LIMIT;

        return {type, lexeme, start_pos, start_line, start_col};
    }

    void skip_whitespace_and_comments() {
        while (!is_at_end()) {
            char c = peek();
            switch (c) {
                case ' ':
                case '\r':
                case '\t':
                    advance();
                    break;
                case '\n':
                    line_++;
                    col_ = 1;
                    advance();
                    break;
                case '/':
                    if (peek_next() == '/') {
                        while (!is_at_end() && peek() != '\n') {
                            advance();
                        }
                    } else {
                        return;
                    }
                    break;
                default:
                    return;
            }
        }
    }

    bool is_at_end() const { return cursor_ >= src_.size(); }
    char advance() { col_++; return src_[cursor_++]; }
    bool match(char expected) {
        if (is_at_end() || src_[cursor_] != expected) return false;
        cursor_++;
        col_++;
        return true;
    }
    char peek() const { return is_at_end() ? '\0' : src_[cursor_]; }
    char peek_next() const { return (cursor_ + 1 >= src_.size()) ? '\0' : src_[cursor_ + 1]; }
};

struct CypherNodePattern {
    std::string variable;
    std::string label;
};

struct CypherEdgePattern {
    std::string variable;
    std::string relation_name;
    bool is_forward{true};
    int min_hops{1};
    int max_hops{1};
    std::vector<std::string> relation_names;
    bool is_undirected{false};
    bool is_unbounded{false};
};

struct CypherPathStep {
    CypherEdgePattern edge;
    CypherNodePattern target_node;
};

struct CypherPathPattern {
    CypherNodePattern start_node;
    std::vector<CypherPathStep> steps;
};

struct CypherWherePredicate {
    std::string target_var;
    std::string field;
    std::string op;
    std::string value_or_param;
    std::vector<uint64_t> in_values{};
    std::vector<float> vector_arg{};
    uint16_t top_k{0};
};

struct CypherReturnProjection {
    std::string variable;
    bool is_count{false};
};

struct CypherOrderByClause {
    std::string target_var;
    std::string attr_name;
    std::vector<float> query_vector{};
    std::string query_param;
    uint8_t metric{0};
    int limit{0};
    bool active{false};
};

struct CypherQuery {
    CypherPathPattern path;
    std::vector<CypherWherePredicate> where_predicates;
    CypherReturnProjection projection;
    CypherOrderByClause order_by{};
};

class CypherParser {
public:
    explicit CypherParser(std::string query) : lexer_(std::move(query)) {
        advance();
    }

    static CypherQuery parse(const std::string& query) {
        CypherParser parser(query);
        return parser.parse_query();
    }

    CypherQuery parse_query() {
        consume(CypherTokenType::KW_MATCH, "Expected 'MATCH' at query start");
        CypherPathPattern path = parse_path_pattern();

        std::vector<CypherWherePredicate> where_predicates;
        if (match(CypherTokenType::KW_WHERE)) {
            where_predicates = parse_where_clause();
        }

        // Optional WITH clause (e.g. WITH b, count(a) AS c WHERE c >= 2)
        if (match(CypherTokenType::KW_WITH)) {
            parse_with_clause(where_predicates);
        }

        consume(CypherTokenType::KW_RETURN, "Expected 'RETURN' clause");
        CypherReturnProjection projection = parse_return_clause();

        CypherOrderByClause order_by{};
        if (match(CypherTokenType::KW_ORDER)) {
            order_by = parse_order_by_clause();
        } else if (match(CypherTokenType::KW_LIMIT)) {
            if (curr_.type == CypherTokenType::NUMBER_LITERAL) {
                order_by.limit = std::stoi(curr_.lexeme);
                order_by.active = true;
                advance();
            }
        }

        return {path, where_predicates, projection, order_by};
    }

private:
    CypherLexer lexer_;
    CypherToken curr_;
    std::string count_alias_;

    static std::string to_lower_str(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
                       [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    void parse_with_clause(std::vector<CypherWherePredicate>& where_predicates) {
        while (curr_.type != CypherTokenType::KW_WHERE &&
               curr_.type != CypherTokenType::KW_RETURN &&
               curr_.type != CypherTokenType::TOK_EOF) {
            if (match(CypherTokenType::KW_COUNT)) {
                consume(CypherTokenType::LPAREN, "Expected '(' after count in WITH");
                if (curr_.type == CypherTokenType::IDENTIFIER) advance();
                consume(CypherTokenType::RPAREN, "Expected ')' after count(...) in WITH");
                if (match(CypherTokenType::KW_AS) && curr_.type == CypherTokenType::IDENTIFIER) {
                    count_alias_ = curr_.lexeme;
                    advance();
                }
            } else {
                advance();
            }
            match(CypherTokenType::COMMA);
        }
        if (match(CypherTokenType::KW_WHERE)) {
            auto extra = parse_where_clause();
            for (auto& p : extra) where_predicates.push_back(std::move(p));
        }
    }

    CypherPathPattern parse_path_pattern() {
        CypherNodePattern start_node = parse_node_pattern();
        std::vector<CypherPathStep> steps;

        while (curr_.type == CypherTokenType::DASH || curr_.type == CypherTokenType::ARROW_LEFT) {
            bool is_forward = true;
            bool is_undirected = false;
            if (match(CypherTokenType::ARROW_LEFT)) {
                is_forward = false;
                consume(CypherTokenType::LBRACKET, "Expected '[' after '<-'");
            } else {
                consume(CypherTokenType::DASH, "Expected '-'");
                consume(CypherTokenType::LBRACKET, "Expected '[' after '-'");
            }

            std::string edge_var;
            if (curr_.type == CypherTokenType::IDENTIFIER) {
                // Could be edge variable before ':'
                // Check if next is COLON by only consuming if curr_ is followed by ':'
            }

            consume(CypherTokenType::COLON, "Typed Edge Walk Mandate: Relationship type is mandatory (e.g. [:RelName])");
            if (curr_.type != CypherTokenType::IDENTIFIER) {
                throw std::runtime_error("Expected relationship name after ':', got: " + curr_.lexeme);
            }
            std::string rel_name = curr_.lexeme;
            std::vector<std::string> rel_names = {rel_name};
            advance();

            while (match(CypherTokenType::PIPE)) {
                match(CypherTokenType::COLON);
                if (curr_.type != CypherTokenType::IDENTIFIER) {
                    throw std::runtime_error("Expected relationship name after '|', got: " + curr_.lexeme);
                }
                rel_names.push_back(curr_.lexeme);
                advance();
            }

            int min_hops = 1;
            int max_hops = 1;
            bool is_unbounded = false;
            if (match(CypherTokenType::STAR)) {
                if (curr_.type == CypherTokenType::NUMBER_LITERAL) {
                    min_hops = std::stoi(curr_.lexeme);
                    advance();
                    if (match(CypherTokenType::DOT) && match(CypherTokenType::DOT)) {
                        if (curr_.type == CypherTokenType::NUMBER_LITERAL) {
                            max_hops = std::stoi(curr_.lexeme);
                            advance();
                        } else {
                            throw std::runtime_error("Unbounded Traversal Mandate: Upper bound required (e.g. *1..4)");
                        }
                    } else {
                        max_hops = min_hops;
                    }
                } else if (match(CypherTokenType::DOT) && match(CypherTokenType::DOT)) {
                    if (curr_.type == CypherTokenType::NUMBER_LITERAL) {
                        max_hops = std::stoi(curr_.lexeme);
                        advance();
                    } else {
                        throw std::runtime_error("Unbounded Traversal Mandate: Upper bound required (e.g. *..4)");
                    }
                } else {
                    is_unbounded = true;
                    min_hops = 1;
                    max_hops = -1;
                }
            }

            consume(CypherTokenType::RBRACKET, "Expected ']' at end of relationship pattern");

            if (!is_forward) {
                consume(CypherTokenType::DASH, "Expected '-' after ']' in incoming relationship '<-[:Rel]-'");
            } else if (match(CypherTokenType::ARROW_RIGHT)) {
                is_undirected = false;
            } else if (match(CypherTokenType::DASH)) {
                is_undirected = true;
            } else {
                consume(CypherTokenType::ARROW_RIGHT, "Expected '->' after ']' in outgoing relationship '-[:Rel]->'");
            }

            CypherNodePattern target_node = parse_node_pattern();
            steps.push_back({{edge_var, rel_name, is_forward, min_hops, max_hops, std::move(rel_names), is_undirected, is_unbounded}, target_node});
        }

        return {start_node, steps};
    }

    CypherNodePattern parse_node_pattern() {
        consume(CypherTokenType::LPAREN, "Expected '(' at start of node pattern");
        std::string var;
        std::string label;

        if (curr_.type == CypherTokenType::IDENTIFIER) {
            var = curr_.lexeme;
            advance();
        }

        if (match(CypherTokenType::COLON)) {
            if (curr_.type == CypherTokenType::IDENTIFIER) {
                label = curr_.lexeme;
                advance();
            }
        }

        consume(CypherTokenType::RPAREN, "Expected ')' at end of node pattern");
        return {var, label};
    }

    std::string consume_comparison_op() {
        std::string op = curr_.lexeme;
        if (!match(CypherTokenType::EQ) && !match(CypherTokenType::EQ_EQ) && !match(CypherTokenType::GT)
            && !match(CypherTokenType::GTE) && !match(CypherTokenType::LT) && !match(CypherTokenType::LTE)
            && !match(CypherTokenType::NEQ)) {
            throw std::runtime_error("Unsupported operator in WHERE clause: " + curr_.lexeme);
        }
        return op;
    }

    std::vector<uint64_t> parse_u64_list() {
        consume(CypherTokenType::LBRACKET, "Expected '[' at start of IN list");
        std::vector<uint64_t> vals;
        while (curr_.type != CypherTokenType::RBRACKET && curr_.type != CypherTokenType::TOK_EOF) {
            if (curr_.type == CypherTokenType::NUMBER_LITERAL) {
                vals.push_back(std::stoull(curr_.lexeme));
            }
            advance();
            match(CypherTokenType::COMMA);
        }
        consume(CypherTokenType::RBRACKET, "Expected ']' at end of IN list");
        return vals;
    }

    std::vector<float> parse_float_list_or_param(std::string& out_param) {
        if (curr_.type == CypherTokenType::PARAM || curr_.type == CypherTokenType::IDENTIFIER) {
            out_param = curr_.lexeme;
            advance();
            return {};
        }
        consume(CypherTokenType::LBRACKET, "Expected '[' or $param for vector argument");
        std::vector<float> vals;
        while (curr_.type != CypherTokenType::RBRACKET && curr_.type != CypherTokenType::TOK_EOF) {
            bool neg = match(CypherTokenType::DASH);
            if (curr_.type == CypherTokenType::NUMBER_LITERAL) {
                float v = std::stof(curr_.lexeme);
                vals.push_back(neg ? -v : v);
                advance();
            } else {
                advance();
            }
            match(CypherTokenType::COMMA);
        }
        consume(CypherTokenType::RBRACKET, "Expected ']' at end of vector literal");
        return vals;
    }

    std::vector<CypherWherePredicate> parse_where_clause() {
        std::vector<CypherWherePredicate> preds;
        do {
            if (match(CypherTokenType::KW_COUNT)) {
                consume(CypherTokenType::LPAREN, "Expected '(' after count");
                std::string var = curr_.lexeme;
                consume(CypherTokenType::IDENTIFIER, "Expected variable inside count(...)");
                consume(CypherTokenType::RPAREN, "Expected ')' after count(...)");
                std::string op = consume_comparison_op();
                uint16_t k = static_cast<uint16_t>(std::stoul(curr_.lexeme));
                consume(CypherTokenType::NUMBER_LITERAL, "Expected integer quorum threshold");
                preds.push_back({var, "quorum", op, std::to_string(k), {k}, {}, k});
                continue;
            }

            if (curr_.type != CypherTokenType::IDENTIFIER) {
                throw std::runtime_error("Expected identifier in WHERE clause, got: " + curr_.lexeme);
            }
            std::string var = curr_.lexeme;
            advance();
            std::string var_lower = to_lower_str(var);

            if (var_lower == "vector_ann" && match(CypherTokenType::LPAREN)) {
                std::string target_var = curr_.lexeme;
                consume(CypherTokenType::IDENTIFIER, "Expected node variable in vector_ann(...)");
                if (match(CypherTokenType::DOT)) {
                    consume(CypherTokenType::IDENTIFIER, "Expected attribute name after '.' in vector_ann");
                }
                consume(CypherTokenType::COMMA, "Expected ',' after target in vector_ann");
                std::string q_param;
                auto q_vec = parse_float_list_or_param(q_param);
                uint16_t k = 10;
                if (match(CypherTokenType::COMMA)) {
                    k = static_cast<uint16_t>(std::stoul(curr_.lexeme));
                    consume(CypherTokenType::NUMBER_LITERAL, "Expected top_k integer in vector_ann");
                }
                consume(CypherTokenType::RPAREN, "Expected ')' at end of vector_ann(...)");
                preds.push_back({target_var, "vector_ann", "ANN", q_param.empty() ? std::to_string(k) : q_param, {}, std::move(q_vec), k});
                continue;
            }

            if (var_lower == "quorum" || (!count_alias_.empty() && var == count_alias_)) {
                std::string target_var = var;
                if (match(CypherTokenType::LPAREN)) {
                    target_var = curr_.lexeme;
                    consume(CypherTokenType::IDENTIFIER, "Expected variable inside quorum(...)");
                    consume(CypherTokenType::RPAREN, "Expected ')' after quorum(...)");
                }
                std::string op = consume_comparison_op();
                uint16_t k = static_cast<uint16_t>(std::stoul(curr_.lexeme));
                consume(CypherTokenType::NUMBER_LITERAL, "Expected integer quorum threshold");
                preds.push_back({target_var, "quorum", op, std::to_string(k), {k}, {}, k});
                continue;
            }

            consume(CypherTokenType::DOT, "Expected '.' after variable in WHERE predicate");
            if (curr_.type != CypherTokenType::IDENTIFIER) {
                throw std::runtime_error("Expected property name after '.', got: " + curr_.lexeme);
            }
            std::string prop = curr_.lexeme;
            advance();

            if (match(CypherTokenType::KW_IN)) {
                auto in_vals = parse_u64_list();
                preds.push_back({var, prop, "IN", "IN_LIST", std::move(in_vals), {}, 0});
                continue;
            }

            std::string op = consume_comparison_op();

            std::string val_or_param = curr_.lexeme;
            if (curr_.type == CypherTokenType::PARAM || curr_.type == CypherTokenType::NUMBER_LITERAL
                || curr_.type == CypherTokenType::STRING_LITERAL || curr_.type == CypherTokenType::IDENTIFIER) {
                advance();
            } else {
                throw std::runtime_error("Expected parameter ($param) or literal in WHERE predicate, got: " + curr_.lexeme);
            }

            if (to_lower_str(prop) == "quorum") {
                uint16_t k = static_cast<uint16_t>(std::stoul(val_or_param));
                preds.push_back({var, "quorum", op, val_or_param, {k}, {}, k});
            } else {
                preds.push_back({var, prop, op, val_or_param, {}, {}, 0});
            }
        } while (match(CypherTokenType::KW_AND));

        return preds;
    }

    CypherOrderByClause parse_order_by_clause() {
        consume(CypherTokenType::KW_BY, "Expected 'BY' after 'ORDER'");
        std::string fn_or_var = curr_.lexeme;
        consume(CypherTokenType::IDENTIFIER, "Expected identifier in ORDER BY");
        CypherOrderByClause clause{};
        clause.active = true;
        clause.target_var = fn_or_var;

        if (match(CypherTokenType::LPAREN)) {
            std::string fn_low = to_lower_str(fn_or_var);
            if (fn_low.find("l2") != std::string::npos) clause.metric = 1;
            else if (fn_low.find("cos") != std::string::npos) clause.metric = 2;
            else if (fn_low.find("ham") != std::string::npos) clause.metric = 3;
            else clause.metric = 0;

            clause.target_var = curr_.lexeme;
            consume(CypherTokenType::IDENTIFIER, "Expected variable in ORDER BY function");
            if (match(CypherTokenType::DOT)) {
                clause.attr_name = curr_.lexeme;
                consume(CypherTokenType::IDENTIFIER, "Expected attribute name after '.' in ORDER BY");
            }
            if (match(CypherTokenType::COMMA)) {
                clause.query_vector = parse_float_list_or_param(clause.query_param);
            }
            consume(CypherTokenType::RPAREN, "Expected ')' after ORDER BY function");
        } else if (match(CypherTokenType::DOT)) {
            clause.attr_name = curr_.lexeme;
            consume(CypherTokenType::IDENTIFIER, "Expected attribute name after '.' in ORDER BY");
        }

        if (!match(CypherTokenType::KW_DESC)) {
            match(CypherTokenType::KW_ASC);
        }
        if (match(CypherTokenType::KW_LIMIT) && curr_.type == CypherTokenType::NUMBER_LITERAL) {
            clause.limit = std::stoi(curr_.lexeme);
            advance();
        }
        return clause;
    }

    CypherReturnProjection parse_return_clause() {
        if (match(CypherTokenType::KW_COUNT)) {
            consume(CypherTokenType::LPAREN, "Expected '(' after 'count'");
            std::string var = curr_.lexeme;
            consume(CypherTokenType::IDENTIFIER, "Expected variable name inside count(...)");
            consume(CypherTokenType::RPAREN, "Expected ')' after variable inside count(...)");
            return {var, true};
        }

        if (curr_.type == CypherTokenType::IDENTIFIER) {
            std::string var = curr_.lexeme;
            advance();
            return {var, false};
        }

        throw std::runtime_error("Expected return target variable or count(var), got: " + curr_.lexeme);
    }

    void advance() { curr_ = lexer_.next_token(); }
    bool match(CypherTokenType type) {
        if (curr_.type == type) {
            advance();
            return true;
        }
        return false;
    }
    void consume(CypherTokenType type, const std::string& error_msg) {
        if (curr_.type == type) {
            advance();
            return;
        }
        throw std::runtime_error(error_msg + " [Line " + std::to_string(curr_.line) + ":" + std::to_string(curr_.col) + " at '" + curr_.lexeme + "']");
    }
};

class CypherCompiler {
public:
    struct CompilationResult {
        std::shared_ptr<ScmProgram> ast;
        std::string seed_variable;
        std::string seed_param_or_value;
    };

    static CompilationResult compile(const std::string& cypher_query) {
        CypherQuery query = CypherParser::parse(cypher_query);
        return compile(query);
    }

    static CompilationResult compile(const CypherQuery& query) {
        std::vector<AstPtr> steps;
        std::string seed_var = query.path.start_node.variable;
        std::string seed_val_or_param;
        std::vector<uint64_t> multi_seeds;
        const CypherWherePredicate* vector_ann_pred = nullptr;
        uint16_t quorum_k = 0;

        for (const auto& pred : query.where_predicates) {
            if (pred.op == "IN" && !pred.in_values.empty() && (seed_var.empty() || pred.target_var == seed_var)) {
                multi_seeds = pred.in_values;
                seed_val_or_param = pred.value_or_param;
            } else if (pred.op == "ANN" && pred.field == "vector_ann") {
                vector_ann_pred = &pred;
            } else if (pred.field == "quorum" && pred.top_k > 0) {
                quorum_k = pred.top_k;
            } else if (pred.target_var == seed_var) {
                seed_val_or_param = pred.value_or_param;
            }
        }

        bool multi_lane_mode = !multi_seeds.empty() || (vector_ann_pred != nullptr && quorum_k > 0);

        if (!multi_seeds.empty()) {
            steps.push_back(ScmLaneInitSeeds::from_inline_seeds(multi_seeds, 0, 0));
        } else if (vector_ann_pred != nullptr) {
            steps.push_back(std::make_shared<ScmVectorAnnSearch>(
                2, vector_ann_pred->top_k, 0, multi_lane_mode));
        }

        for (const auto& step : query.path.steps) {
            const auto& edge = step.edge;
            std::vector<std::string> rels = edge.relation_names.empty()
                ? std::vector<std::string>{edge.relation_name}
                : edge.relation_names;

            if (multi_lane_mode) {
                bool fp = edge.is_unbounded;
                int hops = fp ? 1 : std::max(1, edge.max_hops);
                for (int h = 0; h < hops; ++h) {
                    for (const auto& r : rels) {
                        if (edge.is_undirected) {
                            steps.push_back(ScmLaneWalk::forward(r, fp, fp ? 0x04 : 0x00));
                            steps.push_back(ScmLaneWalk::reverse(r, fp, fp ? 0x04 : 0x00));
                        } else if (edge.is_forward) {
                            steps.push_back(ScmLaneWalk::forward(r, fp, fp ? 0x04 : 0x00));
                        } else {
                            steps.push_back(ScmLaneWalk::reverse(r, fp, fp ? 0x04 : 0x00));
                        }
                    }
                }
            } else {
                auto build_single_hop = [&]() -> AstPtr {
                    std::vector<AstPtr> branches;
                    for (const auto& r : rels) {
                        if (edge.is_undirected) {
                            branches.push_back(ScmWalk::forward(r));
                            branches.push_back(ScmWalk::reverse(r));
                        } else if (edge.is_forward) {
                            branches.push_back(ScmWalk::forward(r));
                        } else {
                            branches.push_back(ScmWalk::reverse(r));
                        }
                    }
                    AstPtr acc = branches[0];
                    for (size_t i = 1; i < branches.size(); ++i) {
                        acc = ScmSetOp::union_op(acc, branches[i]);
                    }
                    return acc;
                };

                if (edge.is_unbounded) {
                    steps.push_back(std::make_shared<ScmFixpoint>(ScmProgram::of(build_single_hop())));
                } else {
                    int hops = std::max(1, edge.max_hops);
                    for (int h = 0; h < hops; ++h) {
                        steps.push_back(build_single_hop());
                    }
                }
            }
        }

        if (multi_lane_mode && quorum_k > 0) {
            steps.push_back(std::make_shared<ScmLaneFilterQuorum>(quorum_k, 0xFF));
        }

        if (query.order_by.active && (!query.order_by.query_vector.empty() || !query.order_by.query_param.empty())) {
            steps.push_back(std::make_shared<ScmVectorRerankDot>(2, 0, query.order_by.metric));
        }

        if (query.projection.is_count) {
            steps.push_back(ScmCollect::count());
        } else {
            steps.push_back(ScmCollect::bitset());
        }

        IMPULSE_ASSERT(!steps.empty());
        IMPULSE_AUDIT_ASSERT(steps.back()->kind() == NodeKind::COLLECT);

        return {std::make_shared<ScmProgram>(steps), seed_var, seed_val_or_param};
    }
};

} // namespace impulse::compiler

#endif // IMPULSE_CYPHER_HPP
