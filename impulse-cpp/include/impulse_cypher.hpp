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
    bool is_forward;
    int min_hops;
    int max_hops;
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
};

struct CypherReturnProjection {
    std::string variable;
    bool is_count;
};

struct CypherQuery {
    CypherPathPattern path;
    std::vector<CypherWherePredicate> where_predicates;
    CypherReturnProjection projection;
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

        consume(CypherTokenType::KW_RETURN, "Expected 'RETURN' clause");
        CypherReturnProjection projection = parse_return_clause();

        return {path, where_predicates, projection};
    }

private:
    CypherLexer lexer_;
    CypherToken curr_;

    CypherPathPattern parse_path_pattern() {
        CypherNodePattern start_node = parse_node_pattern();
        std::vector<CypherPathStep> steps;

        while (curr_.type == CypherTokenType::DASH || curr_.type == CypherTokenType::ARROW_LEFT) {
            bool is_forward = true;
            if (match(CypherTokenType::ARROW_LEFT)) {
                is_forward = false;
                consume(CypherTokenType::LBRACKET, "Expected '[' after '<-'");
            } else {
                consume(CypherTokenType::DASH, "Expected '-'");
                consume(CypherTokenType::LBRACKET, "Expected '[' after '-'");
            }

            std::string edge_var;
            if (curr_.type == CypherTokenType::IDENTIFIER && false) {
                edge_var = curr_.lexeme;
                advance();
            }

            consume(CypherTokenType::COLON, "Typed Edge Walk Mandate: Relationship type is mandatory (e.g. [:RelName])");
            if (curr_.type != CypherTokenType::IDENTIFIER) {
                throw std::runtime_error("Expected relationship name after ':', got: " + curr_.lexeme);
            }
            std::string rel_name = curr_.lexeme;
            advance();

            int min_hops = 1;
            int max_hops = 1;
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
                    throw std::runtime_error("Unbounded Traversal Mandate: Unbounded wildcard '*' without upper bound is not permitted");
                }
            }

            consume(CypherTokenType::RBRACKET, "Expected ']' at end of relationship pattern");

            if (!is_forward) {
                consume(CypherTokenType::DASH, "Expected '-' after ']' in incoming relationship '<-[:Rel]-'");
            } else {
                consume(CypherTokenType::ARROW_RIGHT, "Expected '->' after ']' in outgoing relationship '-[:Rel]->'");
            }

            CypherNodePattern target_node = parse_node_pattern();
            steps.push_back({{edge_var, rel_name, is_forward, min_hops, max_hops}, target_node});
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

    std::vector<CypherWherePredicate> parse_where_clause() {
        std::vector<CypherWherePredicate> preds;
        do {
            if (curr_.type != CypherTokenType::IDENTIFIER) {
                throw std::runtime_error("Expected identifier in WHERE clause, got: " + curr_.lexeme);
            }
            std::string var = curr_.lexeme;
            advance();

            consume(CypherTokenType::DOT, "Expected '.' after variable in WHERE predicate");
            if (curr_.type != CypherTokenType::IDENTIFIER) {
                throw std::runtime_error("Expected property name after '.', got: " + curr_.lexeme);
            }
            std::string prop = curr_.lexeme;
            advance();

            std::string op = curr_.lexeme;
            if (!match(CypherTokenType::EQ) && !match(CypherTokenType::EQ_EQ) && !match(CypherTokenType::GT)
                && !match(CypherTokenType::GTE) && !match(CypherTokenType::LT) && !match(CypherTokenType::LTE)
                && !match(CypherTokenType::NEQ)) {
                throw std::runtime_error("Unsupported operator in WHERE clause: " + curr_.lexeme);
            }

            std::string val_or_param = curr_.lexeme;
            if (curr_.type == CypherTokenType::PARAM || curr_.type == CypherTokenType::NUMBER_LITERAL
                || curr_.type == CypherTokenType::STRING_LITERAL || curr_.type == CypherTokenType::IDENTIFIER) {
                advance();
            } else {
                throw std::runtime_error("Expected parameter ($param) or literal in WHERE predicate, got: " + curr_.lexeme);
            }

            preds.push_back({var, prop, op, val_or_param});
        } while (match(CypherTokenType::KW_AND));

        return preds;
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

        for (const auto& pred : query.where_predicates) {
            if (pred.target_var == seed_var) {
                seed_val_or_param = pred.value_or_param;
            }
        }

        for (const auto& step : query.path.steps) {
            std::string rel = step.edge.relation_name;
            bool is_fwd = step.edge.is_forward;
            int hops = step.edge.max_hops;

            for (int h = 0; h < hops; ++h) {
                if (is_fwd) {
                    steps.push_back(ScmWalk::forward(rel));
                } else {
                    steps.push_back(ScmWalk::reverse(rel));
                }
            }
        }

        if (query.projection.is_count) {
            steps.push_back(ScmCollect::count());
        } else {
            steps.push_back(ScmCollect::bitset());
        }

        return {std::make_shared<ScmProgram>(steps), seed_var, seed_val_or_param};
    }
};

} // namespace impulse::compiler

#endif // IMPULSE_CYPHER_HPP
