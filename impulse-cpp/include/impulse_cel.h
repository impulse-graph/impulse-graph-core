#include <iostream>
/**
 * @file impulse_cel.h
 * @brief Google CEL (Common Expression Language) Zero-Dependency Pratt Parser & Compiler for Impulse Graph.
 *
 * Implements standard CEL grammar, temporal extensions (timestamp, duration),
 * 42-function analytical vector math catalog, and lowering to ImpScheme IR & impOps bytecode.
 */

#ifndef IMPULSE_CEL_H
#define IMPULSE_CEL_H

#include "impulse_vm.h"
#include "impulse_math_ops.h"
#include "impulse_assert.h"
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <sstream>
#include <cstdint>
#include <cmath>
#include <cstring>
#include <bit>

namespace impulse {
namespace cel {

enum class TokenType {
    END_OF_FILE,
    IDENTIFIER,
    INT_LITERAL,
    FLOAT_LITERAL,
    STRING_LITERAL,
    BOOL_LITERAL,
    
    // Operators
    PLUS, MINUS, STAR, SLASH, PERCENT,
    EQ_EQ, BANG_EQ, LT, LT_EQ, GT, GT_EQ,
    AMP_AMP, PIPE_PIPE, BANG,
    AMP, PIPE, CARET, TILDE, LT_LT, GT_GT,
    QUESTION, COLON,
    DOT, COMMA, EQ,
    
    // Delimiters
    LPAREN, RPAREN,
    LBRACKET, RBRACKET,
    LBRACE, RBRACE,
    
    // Keywords
    KW_IN, KW_AS
};

struct Token {
    TokenType type;
    std::string text;
    int64_t int_val = 0;
    double float_val = 0.0;
    bool bool_val = false;
    size_t pos = 0;
};

class Lexer {
public:
    explicit Lexer(std::string source) : src_(std::move(source)), cursor_(0) {}

    Token next_token() {
        skip_whitespace_and_comments();
        if (cursor_ >= src_.size()) {
            return Token{TokenType::END_OF_FILE, "", 0, 0.0, false, cursor_};
        }

        size_t start = cursor_;
        char c = src_[cursor_++];

        switch (c) {
            case '+': return Token{TokenType::PLUS, "+", 0, 0.0, false, start};
            case '-': return Token{TokenType::MINUS, "-", 0, 0.0, false, start};
            case '*': return Token{TokenType::STAR, "*", 0, 0.0, false, start};
            case '/': return Token{TokenType::SLASH, "/", 0, 0.0, false, start};
            case '%': return Token{TokenType::PERCENT, "%", 0, 0.0, false, start};
            case '?': return Token{TokenType::QUESTION, "?", 0, 0.0, false, start};
            case ':': return Token{TokenType::COLON, ":", 0, 0.0, false, start};
            case '.': return Token{TokenType::DOT, ".", 0, 0.0, false, start};
            case ',': return Token{TokenType::COMMA, ",", 0, 0.0, false, start};
            case '(': return Token{TokenType::LPAREN, "(", 0, 0.0, false, start};
            case ')': return Token{TokenType::RPAREN, ")", 0, 0.0, false, start};
            case '[': return Token{TokenType::LBRACKET, "[", 0, 0.0, false, start};
            case ']': return Token{TokenType::RBRACKET, "]", 0, 0.0, false, start};
            case '{': return Token{TokenType::LBRACE, "{", 0, 0.0, false, start};
            case '}': return Token{TokenType::RBRACE, "}", 0, 0.0, false, start};

            case '=':
                if (peek() == '=') { cursor_++; return Token{TokenType::EQ_EQ, "==", 0, 0.0, false, start}; }
                return Token{TokenType::EQ, "=", 0, 0.0, false, start};
            case '!':
                if (peek() == '=') { cursor_++; return Token{TokenType::BANG_EQ, "!=", 0, 0.0, false, start}; }
                return Token{TokenType::BANG, "!", 0, 0.0, false, start};
            case '<':
                if (peek() == '=') { cursor_++; return Token{TokenType::LT_EQ, "<=", 0, 0.0, false, start}; }
                if (peek() == '<') { cursor_++; return Token{TokenType::LT_LT, "<<", 0, 0.0, false, start}; }
                return Token{TokenType::LT, "<", 0, 0.0, false, start};
            case '>':
                if (peek() == '=') { cursor_++; return Token{TokenType::GT_EQ, ">=", 0, 0.0, false, start}; }
                if (peek() == '>') {
                    cursor_++;
                    if (peek() == '>') { cursor_++; return Token{TokenType::GT_GT, ">>>", 0, 0.0, false, start}; }
                    return Token{TokenType::GT_GT, ">>", 0, 0.0, false, start};
                }
                return Token{TokenType::GT, ">", 0, 0.0, false, start};
            case '&':
                if (peek() == '&') { cursor_++; return Token{TokenType::AMP_AMP, "&&", 0, 0.0, false, start}; }
                return Token{TokenType::AMP, "&", 0, 0.0, false, start};
            case '|':
                if (peek() == '|') { cursor_++; return Token{TokenType::PIPE_PIPE, "||", 0, 0.0, false, start}; }
                return Token{TokenType::PIPE, "|", 0, 0.0, false, start};
            case '^':
                return Token{TokenType::CARET, "^", 0, 0.0, false, start};
            case '~':
                return Token{TokenType::TILDE, "~", 0, 0.0, false, start};

            case '$':
            case '@':
                if (std::isalpha(static_cast<unsigned char>(peek())) || peek() == '_') {
                    return parse_ident(start);
                }
                break;

            case '"':
            case '\'':
                return parse_string(c, start);

            default:
                if (std::isdigit(static_cast<unsigned char>(c))) {
                    return parse_number(c, start);
                }
                if (std::isalpha(static_cast<unsigned char>(c)) || c == '_') {
                    return parse_ident(start);
                }
                break;
        }

        return Token{TokenType::END_OF_FILE, std::string(1, c), 0, 0.0, false, start};
    }

private:
    std::string src_;
    size_t cursor_;

    char peek() const {
        return cursor_ < src_.size() ? src_[cursor_] : '\0';
    }

    void skip_whitespace_and_comments() {
        while (cursor_ < src_.size()) {
            char c = src_[cursor_];
            if (std::isspace(static_cast<unsigned char>(c))) {
                cursor_++;
            } else if (c == '/' && cursor_ + 1 < src_.size() && src_[cursor_ + 1] == '/') {
                cursor_ += 2;
                while (cursor_ < src_.size() && src_[cursor_] != '\n') cursor_++;
            } else {
                break;
            }
        }
    }

    Token parse_string(char quote, size_t start) {
        std::string s;
        while (cursor_ < src_.size()) {
            char c = src_[cursor_++];
            if (c == quote) break;
            if (c == '\\' && cursor_ < src_.size()) {
                c = src_[cursor_++];
                if (c == 'n') s += '\n';
                else if (c == 't') s += '\t';
                else if (c == 'r') s += '\r';
                else s += c;
            } else {
                s += c;
            }
        }
        Token t{TokenType::STRING_LITERAL, s, 0, 0.0, false, start};
        return t;
    }

    Token parse_number(char first_digit, size_t start) {
        if (first_digit == '0' && cursor_ < src_.size() && (src_[cursor_] == 'x' || src_[cursor_] == 'X')) {
            std::string raw = std::string("0") + src_[cursor_++];
            std::string digits;
            while (cursor_ < src_.size()) {
                char c = src_[cursor_];
                if (std::isxdigit(static_cast<unsigned char>(c))) {
                    raw += c;
                    digits += c;
                    cursor_++;
                } else if (c == '_') {
                    raw += c;
                    cursor_++;
                } else {
                    break;
                }
            }
            Token t;
            t.pos = start;
            t.text = raw;
            t.type = TokenType::INT_LITERAL;
            t.int_val = digits.empty() ? 0 : static_cast<int64_t>(std::stoull(digits, nullptr, 16));
            return t;
        }
        if (first_digit == '0' && cursor_ < src_.size() && (src_[cursor_] == 'b' || src_[cursor_] == 'B')) {
            std::string raw = std::string("0") + src_[cursor_++];
            std::string digits;
            while (cursor_ < src_.size()) {
                char c = src_[cursor_];
                if (c == '0' || c == '1') {
                    raw += c;
                    digits += c;
                    cursor_++;
                } else if (c == '_') {
                    raw += c;
                    cursor_++;
                } else {
                    break;
                }
            }
            Token t;
            t.pos = start;
            t.text = raw;
            t.type = TokenType::INT_LITERAL;
            t.int_val = digits.empty() ? 0 : static_cast<int64_t>(std::stoull(digits, nullptr, 2));
            return t;
        }

        std::string num_str(1, first_digit);
        bool is_float = false;
        while (cursor_ < src_.size()) {
            char c = src_[cursor_];
            if (std::isdigit(static_cast<unsigned char>(c))) {
                num_str += c;
                cursor_++;
            } else if (c == '_') {
                cursor_++;
            } else if (c == '.' && !is_float && cursor_ + 1 < src_.size() && std::isdigit(static_cast<unsigned char>(src_[cursor_ + 1]))) {
                is_float = true;
                num_str += c;
                cursor_++;
            } else if (c == 'e' || c == 'E') {
                is_float = true;
                num_str += c;
                cursor_++;
                if (cursor_ < src_.size() && (src_[cursor_] == '+' || src_[cursor_] == '-')) {
                    num_str += src_[cursor_++];
                }
            } else {
                break;
            }
        }
        Token t;
        t.pos = start;
        t.text = num_str;
        if (is_float) {
            t.type = TokenType::FLOAT_LITERAL;
            t.float_val = std::stod(num_str);
        } else {
            t.type = TokenType::INT_LITERAL;
            t.int_val = std::stoll(num_str);
        }
        return t;
    }

    Token parse_ident(size_t start) {
        std::string id;
        char first = src_[start];
        if (first != '$' && first != '@') {
            id += first;
        }
        while (cursor_ < src_.size()) {
            char c = src_[cursor_];
            if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
                id += c;
                cursor_++;
            } else {
                break;
            }
        }
        Token t;
        t.pos = start;
        t.text = id;
        if (id == "true") {
            t.type = TokenType::BOOL_LITERAL;
            t.bool_val = true;
        } else if (id == "false") {
            t.type = TokenType::BOOL_LITERAL;
            t.bool_val = false;
        } else if (id == "in") {
            t.type = TokenType::KW_IN;
        } else if (id == "as") {
            t.type = TokenType::KW_AS;
        } else {
            t.type = TokenType::IDENTIFIER;
        }
        return t;
    }
};

// Abstract Syntax Tree (AST)
enum class AstKind {
    LITERAL_INT,
    LITERAL_FLOAT,
    LITERAL_BOOL,
    LITERAL_STRING,
    IDENTIFIER,
    MEMBER_ACCESS,
    UNARY_OP,
    BINARY_OP,
    TERNARY_OP,
    FUNCTION_CALL,
    LIST_LITERAL
};

struct AstNode {
    AstKind kind;
    std::string text;
    int64_t int_val = 0;
    double float_val = 0.0;
    bool bool_val = false;
    std::string str_val;
    std::vector<std::shared_ptr<AstNode>> children;

    static std::shared_ptr<AstNode> make_int(int64_t v) {
        auto n = std::make_shared<AstNode>();
        n->kind = AstKind::LITERAL_INT;
        n->int_val = v;
        return n;
    }

    static std::shared_ptr<AstNode> make_float(double v) {
        auto n = std::make_shared<AstNode>();
        n->kind = AstKind::LITERAL_FLOAT;
        n->float_val = v;
        return n;
    }

    static std::shared_ptr<AstNode> make_bool(bool v) {
        auto n = std::make_shared<AstNode>();
        n->kind = AstKind::LITERAL_BOOL;
        n->bool_val = v;
        return n;
    }

    static std::shared_ptr<AstNode> make_string(std::string v) {
        auto n = std::make_shared<AstNode>();
        n->kind = AstKind::LITERAL_STRING;
        n->str_val = std::move(v);
        return n;
    }

    static std::shared_ptr<AstNode> make_ident(std::string name) {
        auto n = std::make_shared<AstNode>();
        n->kind = AstKind::IDENTIFIER;
        n->text = std::move(name);
        return n;
    }

    static std::shared_ptr<AstNode> make_member(std::shared_ptr<AstNode> target, std::string field) {
        auto n = std::make_shared<AstNode>();
        n->kind = AstKind::MEMBER_ACCESS;
        n->text = std::move(field);
        n->children.push_back(std::move(target));
        return n;
    }

    static std::shared_ptr<AstNode> make_unary(std::string op, std::shared_ptr<AstNode> operand) {
        auto n = std::make_shared<AstNode>();
        n->kind = AstKind::UNARY_OP;
        n->text = std::move(op);
        n->children.push_back(std::move(operand));
        return n;
    }

    static std::shared_ptr<AstNode> make_binary(std::string op, std::shared_ptr<AstNode> lhs, std::shared_ptr<AstNode> rhs) {
        auto n = std::make_shared<AstNode>();
        n->kind = AstKind::BINARY_OP;
        n->text = std::move(op);
        n->children.push_back(std::move(lhs));
        n->children.push_back(std::move(rhs));
        return n;
    }

    static std::shared_ptr<AstNode> make_ternary(std::shared_ptr<AstNode> cond, std::shared_ptr<AstNode> then_branch, std::shared_ptr<AstNode> else_branch) {
        auto n = std::make_shared<AstNode>();
        n->kind = AstKind::TERNARY_OP;
        n->children.push_back(std::move(cond));
        n->children.push_back(std::move(then_branch));
        n->children.push_back(std::move(else_branch));
        return n;
    }

    static std::shared_ptr<AstNode> make_call(std::string func, std::vector<std::shared_ptr<AstNode>> args) {
        auto n = std::make_shared<AstNode>();
        n->kind = AstKind::FUNCTION_CALL;
        n->text = std::move(func);
        n->children = std::move(args);
        return n;
    }

    static std::shared_ptr<AstNode> make_list(std::vector<std::shared_ptr<AstNode>> elements) {
        auto n = std::make_shared<AstNode>();
        n->kind = AstKind::LIST_LITERAL;
        n->children = std::move(elements);
        return n;
    }
};

// Operator Precedence levels for Pratt parser
enum Precedence {
    PREC_NONE = 0,
    PREC_ASSIGN,
    PREC_CONDITIONAL, // ? :
    PREC_OR,          // ||
    PREC_AND,         // &&
    PREC_COMPARE,     // == != < <= > >= in
    PREC_BIT_OR,      // |
    PREC_BIT_XOR,     // ^
    PREC_BIT_AND,     // &
    PREC_SHIFT,       // << >>
    PREC_ADD_SUB,     // + -
    PREC_MUL_DIV,     // * / %
    PREC_UNARY,       // ! - + ~
    PREC_CALL_MEMBER  // . () []
};

class Parser {
public:
    static constexpr size_t MAX_PARSE_DEPTH = 256;

    explicit Parser(std::string source) : lexer_(std::move(source)) {
        advance();
    }

    std::shared_ptr<AstNode> parse_expression(size_t depth = 0) {
        if (depth > MAX_PARSE_DEPTH) {
            throw std::runtime_error("CEL parse recursion depth exceeded limit 256");
        }
        return parse_precedence(PREC_ASSIGN, depth);
    }

    std::shared_ptr<AstNode> parse() {
        auto expr = parse_expression(0);
        if (!expr || curr_.type != TokenType::END_OF_FILE) {
            return nullptr;
        }
        return expr;
    }

private:
    Lexer lexer_;
    Token curr_;

    void advance() {
        curr_ = lexer_.next_token();
    }

    bool match(TokenType type) {
        if (curr_.type == type) {
            advance();
            return true;
        }
        return false;
    }

    Precedence get_precedence(TokenType type) const {
        switch (type) {
            case TokenType::EQ: return PREC_ASSIGN;
            case TokenType::QUESTION: return PREC_CONDITIONAL;
            case TokenType::PIPE_PIPE: return PREC_OR;
            case TokenType::AMP_AMP: return PREC_AND;
            case TokenType::EQ_EQ:
            case TokenType::BANG_EQ:
            case TokenType::LT:
            case TokenType::LT_EQ:
            case TokenType::GT:
            case TokenType::GT_EQ:
            case TokenType::KW_IN:
                return PREC_COMPARE;
            case TokenType::PIPE:
                return PREC_BIT_OR;
            case TokenType::CARET:
                return PREC_BIT_XOR;
            case TokenType::AMP:
                return PREC_BIT_AND;
            case TokenType::LT_LT:
            case TokenType::GT_GT:
                return PREC_SHIFT;
            case TokenType::PLUS:
            case TokenType::MINUS:
                return PREC_ADD_SUB;
            case TokenType::STAR:
            case TokenType::SLASH:
            case TokenType::PERCENT:
                return PREC_MUL_DIV;
            case TokenType::DOT:
            case TokenType::LPAREN:
            case TokenType::LBRACKET:
                return PREC_CALL_MEMBER;
            default:
                return PREC_NONE;
        }
    }

    std::shared_ptr<AstNode> parse_prefix(size_t depth = 0) {
        if (depth > MAX_PARSE_DEPTH) {
            throw std::runtime_error("CEL parse recursion depth exceeded limit 256");
        }
        Token t = curr_;
        advance();

        switch (t.type) {
            case TokenType::INT_LITERAL:
                return AstNode::make_int(t.int_val);
            case TokenType::FLOAT_LITERAL:
                return AstNode::make_float(t.float_val);
            case TokenType::BOOL_LITERAL:
                return AstNode::make_bool(t.bool_val);
            case TokenType::STRING_LITERAL:
                return AstNode::make_string(t.text);
            case TokenType::IDENTIFIER:
                return AstNode::make_ident(t.text);
            case TokenType::BANG:
            case TokenType::MINUS:
            case TokenType::PLUS:
            case TokenType::TILDE: {
                auto operand = parse_precedence(PREC_UNARY, depth + 1);
                return AstNode::make_unary(t.text, operand);
            }
            case TokenType::LPAREN: {
                auto expr = parse_expression(depth + 1);
                match(TokenType::RPAREN);
                return expr;
            }
            case TokenType::LBRACKET: {
                std::vector<std::shared_ptr<AstNode>> elements;
                if (curr_.type != TokenType::RBRACKET) {
                    elements.push_back(parse_expression(depth + 1));
                    while (match(TokenType::COMMA)) {
                        elements.push_back(parse_expression(depth + 1));
                    }
                }
                match(TokenType::RBRACKET);
                return AstNode::make_list(std::move(elements));
            }
            default:
                return nullptr;
        }
    }

    std::shared_ptr<AstNode> parse_precedence(Precedence prec, size_t depth = 0) {
        if (depth > MAX_PARSE_DEPTH) {
            throw std::runtime_error("CEL parse recursion depth exceeded limit 256");
        }
        auto left = parse_prefix(depth + 1);
        if (!left) return nullptr;

        while (prec <= get_precedence(curr_.type)) {
            Token op = curr_;
            advance();

            if (op.type == TokenType::QUESTION) {
                // Ternary conditional ? :
                auto then_branch = parse_expression(depth + 1);
                match(TokenType::COLON);
                auto else_branch = parse_precedence(PREC_CONDITIONAL, depth + 1);
                left = AstNode::make_ternary(left, then_branch, else_branch);
            } else if (op.type == TokenType::DOT) {
                // Field access target.field
                if (curr_.type == TokenType::IDENTIFIER) {
                    std::string field = curr_.text;
                    advance();
                    // Could be member call: target.func(args)
                    if (match(TokenType::LPAREN)) {
                        std::vector<std::shared_ptr<AstNode>> args;
                        args.push_back(left); // receiver is first arg
                        if (curr_.type != TokenType::RPAREN) {
                            args.push_back(parse_expression(depth + 1));
                            while (match(TokenType::COMMA)) {
                                args.push_back(parse_expression(depth + 1));
                            }
                        }
                        match(TokenType::RPAREN);
                        left = AstNode::make_call(field, std::move(args));
                    } else {
                        left = AstNode::make_member(left, field);
                    }
                }
            } else if (op.type == TokenType::LPAREN) {
                // Function call ident(args)
                std::vector<std::shared_ptr<AstNode>> args;
                if (curr_.type != TokenType::RPAREN) {
                    args.push_back(parse_expression(depth + 1));
                    while (match(TokenType::COMMA)) {
                        args.push_back(parse_expression(depth + 1));
                    }
                }
                match(TokenType::RPAREN);
                if (left->kind == AstKind::IDENTIFIER) {
                    left = AstNode::make_call(left->text, std::move(args));
                }
            } else {
                // Binary operator
                auto next_prec = static_cast<Precedence>(get_precedence(op.type) + (op.type == TokenType::EQ ? 0 : 1));
                auto right = parse_precedence(next_prec, depth + 1);
                if (!right) return nullptr;
                left = AstNode::make_binary(op.text, left, right);
            }
        }

        return left;
    }
};

// CEL to ImpScheme & impOps Code Generator
class CelCompiler {
public:
    static constexpr size_t MAX_PARSE_DEPTH = 256;

    static std::string to_impscheme(const std::shared_ptr<AstNode>& node, size_t depth = 0) {
        if (!node) return "()";
        if (depth > MAX_PARSE_DEPTH) {
            throw std::runtime_error("CelCompiler recursion depth exceeded limit 256");
        }
        IMPULSE_ASSERT(node != nullptr);

        switch (node->kind) {
            case AstKind::LITERAL_INT:
                return std::to_string(node->int_val);
            case AstKind::LITERAL_FLOAT: {
                std::ostringstream ss;
                ss << node->float_val;
                std::string s = ss.str();
                if (s.find('.') == std::string::npos && s.find('e') == std::string::npos) s += ".0";
                return s;
            }
            case AstKind::LITERAL_BOOL:
                return node->bool_val ? "#t" : "#f";
            case AstKind::LITERAL_STRING:
                return "\"" + node->str_val + "\"";
            case AstKind::IDENTIFIER:
                return node->text;
            case AstKind::MEMBER_ACCESS:
                return "(get-attr " + to_impscheme(node->children[0], depth + 1) + " \"" + node->text + "\")";
            case AstKind::UNARY_OP:
                if (node->text == "!") return "(mask-not " + to_impscheme(node->children[0], depth + 1) + ")";
                if (node->text == "-") return "(- 0 " + to_impscheme(node->children[0], depth + 1) + ")";
                if (node->text == "~") return "(~ " + to_impscheme(node->children[0], depth + 1) + ")";
                return to_impscheme(node->children[0], depth + 1);
            case AstKind::BINARY_OP: {
                std::string op = node->text;
                if (op == "&&") return "(mask-and " + to_impscheme(node->children[0], depth + 1) + " " + to_impscheme(node->children[1], depth + 1) + ")";
                if (op == "||") return "(mask-or " + to_impscheme(node->children[0], depth + 1) + " " + to_impscheme(node->children[1], depth + 1) + ")";
                if (op == ">") return "(vec-cmp-gt " + to_impscheme(node->children[0], depth + 1) + " " + to_impscheme(node->children[1], depth + 1) + ")";
                if (op == "<") return "(vec-cmp-lt " + to_impscheme(node->children[0], depth + 1) + " " + to_impscheme(node->children[1], depth + 1) + ")";
                if (op == "==") return "(vec-cmp-eq " + to_impscheme(node->children[0], depth + 1) + " " + to_impscheme(node->children[1], depth + 1) + ")";
                if (op == "!=") return "(mask-not (vec-cmp-eq " + to_impscheme(node->children[0], depth + 1) + " " + to_impscheme(node->children[1], depth + 1) + "))";
                return "(" + op + " " + to_impscheme(node->children[0], depth + 1) + " " + to_impscheme(node->children[1], depth + 1) + ")";
            }
            case AstKind::TERNARY_OP:
                return "(vec-blend " + to_impscheme(node->children[0], depth + 1) + " " + to_impscheme(node->children[1], depth + 1) + " " + to_impscheme(node->children[2], depth + 1) + ")";
            case AstKind::FUNCTION_CALL: {
                std::string s = "(" + node->text;
                for (const auto& arg : node->children) {
                    s += " " + to_impscheme(arg, depth + 1);
                }
                s += ")";
                return s;
            }
            case AstKind::LIST_LITERAL: {
                std::string s = "(list";
                for (const auto& elem : node->children) {
                    s += " " + to_impscheme(elem, depth + 1);
                }
                s += ")";
                return s;
            }
        }
        return "()";
    }

    // Resolves CEL built-in function names to impulse_math_ops func_id
    static int resolve_math_func(const std::string& name) {
        static const std::unordered_map<std::string, int> kMathMap = {
            {"abs", MATH_FUNC_ABS}, {"sqrt", MATH_FUNC_SQRT}, {"rsqrt", MATH_FUNC_RSQRT},
            {"cbrt", MATH_FUNC_CBRT}, {"pow", MATH_FUNC_POW}, {"hypot", MATH_FUNC_HYPOT},
            {"lerp", MATH_FUNC_LERP}, {"exp", MATH_FUNC_EXP}, {"exp2", MATH_FUNC_EXP2},
            {"exp10", MATH_FUNC_EXP10}, {"expm1", MATH_FUNC_EXPM1}, {"log", MATH_FUNC_LOG},
            {"log2", MATH_FUNC_LOG2}, {"log10", MATH_FUNC_LOG10}, {"log1p", MATH_FUNC_LOG1P},
            {"sin", MATH_FUNC_SIN}, {"cos", MATH_FUNC_COS}, {"tan", MATH_FUNC_TAN},
            {"asin", MATH_FUNC_ASIN}, {"acos", MATH_FUNC_ACOS}, {"atan", MATH_FUNC_ATAN},
            {"atan2", MATH_FUNC_ATAN2}, {"sinc", MATH_FUNC_SINC}, {"sinh", MATH_FUNC_SINH},
            {"cosh", MATH_FUNC_COSH}, {"tanh", MATH_FUNC_TANH}, {"asinh", MATH_FUNC_ASINH},
            {"acosh", MATH_FUNC_ACOSH}, {"atanh", MATH_FUNC_ATANH}, {"floor", MATH_FUNC_FLOOR},
            {"ceil", MATH_FUNC_CEIL}, {"trunc", MATH_FUNC_TRUNC}, {"round", MATH_FUNC_ROUND},
            {"clamp", MATH_FUNC_CLAMP}, {"copysign", MATH_FUNC_COPYSIGN}, {"fmod", MATH_FUNC_FMOD},
            {"relu", MATH_FUNC_RELU}, {"leaky_relu", MATH_FUNC_LEAKY_RELU}, {"sigmoid", MATH_FUNC_SIGMOID},
            {"gelu", MATH_FUNC_GELU}, {"silu", MATH_FUNC_SILU}, {"softplus", MATH_FUNC_SOFTPLUS},
            {"erf", MATH_FUNC_ERF}, {"erfc", MATH_FUNC_ERFC}, {"lgamma", MATH_FUNC_LGAMMA},
            {"popcount", MATH_FUNC_POPCOUNT}, {"bit_count", MATH_FUNC_POPCOUNT},
            {"clz", MATH_FUNC_CLZ}, {"ctz", MATH_FUNC_CTZ}, {"first_lane", MATH_FUNC_CTZ},
            {"rotl", MATH_FUNC_ROTL}, {"rotr", MATH_FUNC_ROTR},
            {"safeDiv", MATH_FUNC_SAFE_DIV}, {"safe_div", MATH_FUNC_SAFE_DIV},
            {"isNan", MATH_FUNC_ISNAN}, {"isnan", MATH_FUNC_ISNAN},
            {"isInf", MATH_FUNC_ISINF}, {"isinf", MATH_FUNC_ISINF},
            {"isFinite", MATH_FUNC_ISFINITE}, {"isfinite", MATH_FUNC_ISFINITE}
        };
        auto it = kMathMap.find(name);
        return it != kMathMap.end() ? it->second : -1;
    }

    /**
     * @brief Compiles a CEL AST into an Edge Stream Shader instruction sequence.
     * Encloses instructions with OP_STREAM_FUNC_BEGIN (0xA1) and OP_STREAM_FUNC_END (0xA2).
     */
    static bool compile_stream_shader(
        const std::shared_ptr<AstNode>& expr,
        std::vector<impulse_instruction_t>& out_shader,
        uint16_t out_stream_reg = 0,
        const std::unordered_map<std::string, uint16_t>* attr_name_to_id = nullptr,
        bool is_filter = false
    ) {
        if (!expr) return false;

        out_shader.push_back(impulse_instruction_t{OP_STREAM_FUNC_BEGIN, 0, 0, 0});

        auto emit_stream_impl = [&](auto& self, const std::shared_ptr<AstNode>& node, uint16_t target_reg) -> bool {
            if (!node || target_reg >= 16) return false;

            switch (node->kind) {
                case AstKind::LITERAL_FLOAT:
                case AstKind::LITERAL_INT: {
                    float f = (node->kind == AstKind::LITERAL_FLOAT)
                        ? static_cast<float>(node->float_val)
                        : static_cast<float>(node->int_val);
                    uint32_t bits = 0;
                    std::memcpy(&bits, &f, sizeof(float));
                    out_shader.push_back(impulse_instruction_t{OP_STREAM_LOAD_CONST, 0, target_reg, bits});
                    return true;
                }
                case AstKind::LITERAL_BOOL: {
                    float f = node->bool_val ? 1.0f : 0.0f;
                    uint32_t bits = 0;
                    std::memcpy(&bits, &f, sizeof(float));
                    out_shader.push_back(impulse_instruction_t{OP_STREAM_LOAD_CONST, 0, target_reg, bits});
                    return true;
                }
                case AstKind::IDENTIFIER: {
                    if (node->text == "lane") {
                        out_shader.push_back(impulse_instruction_t{OP_STREAM_LOAD_SRC_ID, 2, target_reg, 0});
                        return true;
                    }
                    if (node->text == "id" || node->text == "src_id") {
                        out_shader.push_back(impulse_instruction_t{OP_STREAM_LOAD_SRC_ID, 0, target_reg, 0});
                        return true;
                    }
                    if (node->text == "tgt_id") {
                        out_shader.push_back(impulse_instruction_t{OP_STREAM_LOAD_TGT_ID, 0, target_reg, 0});
                        return true;
                    }
                    if (node->text == "edge_id") {
                        out_shader.push_back(impulse_instruction_t{OP_STREAM_LOAD_EDGE_ID, 0, target_reg, 0});
                        return true;
                    }
                    uint16_t attr_id = 0;
                    if (attr_name_to_id) {
                        auto it = attr_name_to_id->find(node->text);
                        if (it != attr_name_to_id->end()) attr_id = it->second;
                    }
                    out_shader.push_back(impulse_instruction_t{OP_STREAM_LOAD_EDGE, 0, target_reg, attr_id});
                    return true;
                }
                case AstKind::MEMBER_ACCESS: {
                    const std::string& field = node->text;
                    const auto& target = node->children[0];
                    std::string target_name = (target && target->kind == AstKind::IDENTIFIER) ? target->text : "";

                    if (target_name == "src") {
                        if (field == "id") {
                            out_shader.push_back(impulse_instruction_t{OP_STREAM_LOAD_SRC_ID, 0, target_reg, 0});
                            return true;
                        }
                        if (field == "lane") {
                            out_shader.push_back(impulse_instruction_t{OP_STREAM_LOAD_SRC_ID, 2, target_reg, 0});
                            return true;
                        }
                        if (field == "popcount") {
                            out_shader.push_back(impulse_instruction_t{OP_STREAM_LOAD_SRC_ID, 1, target_reg, 0});
                            return true;
                        }
                        uint16_t attr_id = 0;
                        if (attr_name_to_id) {
                            auto it = attr_name_to_id->find(field);
                            if (it != attr_name_to_id->end()) attr_id = it->second;
                        }
                        out_shader.push_back(impulse_instruction_t{OP_STREAM_LOAD_SRC, 0, target_reg, attr_id});
                        return true;
                    }
                    if (target_name == "tgt") {
                        if (field == "id") {
                            out_shader.push_back(impulse_instruction_t{OP_STREAM_LOAD_TGT_ID, 0, target_reg, 0});
                            return true;
                        }
                        uint16_t attr_id = 0;
                        if (attr_name_to_id) {
                            auto it = attr_name_to_id->find(field);
                            if (it != attr_name_to_id->end()) attr_id = it->second;
                        }
                        out_shader.push_back(impulse_instruction_t{OP_STREAM_LOAD_TGT, 0, target_reg, attr_id});
                        return true;
                    }
                    if (target_name == "edge") {
                        if (field == "id") {
                            out_shader.push_back(impulse_instruction_t{OP_STREAM_LOAD_EDGE_ID, 0, target_reg, 0});
                            return true;
                        }
                        uint16_t attr_id = 0;
                        if (attr_name_to_id) {
                            auto it = attr_name_to_id->find(field);
                            if (it != attr_name_to_id->end()) attr_id = it->second;
                        }
                        out_shader.push_back(impulse_instruction_t{OP_STREAM_LOAD_EDGE, 0, target_reg, attr_id});
                        return true;
                    }
                    uint16_t attr_id = 0;
                    if (attr_name_to_id) {
                        auto it = attr_name_to_id->find(field);
                        if (it != attr_name_to_id->end()) attr_id = it->second;
                    }
                    out_shader.push_back(impulse_instruction_t{OP_STREAM_LOAD_EDGE, 0, target_reg, attr_id});
                    return true;
                }
                case AstKind::UNARY_OP: {
                    if (node->text == "!") {
                        uint16_t r0 = (target_reg + 1) % 16;
                        if (!self(self, node->children[0], r0)) return false;
                        out_shader.push_back(impulse_instruction_t{OP_STREAM_LOGIC_NOT, 0, target_reg, r0});
                        return true;
                    }
                    if (node->text == "-") {
                        uint16_t r0 = (target_reg + 1) % 16;
                        uint16_t rz = (target_reg + 2) % 16;
                        if (!self(self, node->children[0], r0)) return false;
                        uint32_t zero_bits = 0;
                        out_shader.push_back(impulse_instruction_t{OP_STREAM_LOAD_CONST, 0, rz, zero_bits});
                        out_shader.push_back(impulse_instruction_t{OP_STREAM_MATH_SUB, 0, target_reg, static_cast<uint32_t>(rz | (r0 << 16))});
                        return true;
                    }
                    return self(self, node->children[0], target_reg);
                }
                case AstKind::BINARY_OP: {
                    const std::string& op = node->text;
                    uint16_t r0 = (target_reg + 1) % 16;
                    uint16_t r1 = (target_reg + 2) % 16;
                    if (!self(self, node->children[0], r0)) return false;
                    if (!self(self, node->children[1], r1)) return false;
                    uint32_t payload = static_cast<uint32_t>(r0 | (r1 << 16));

                    if (op == "+") out_shader.push_back(impulse_instruction_t{OP_STREAM_MATH_ADD, 0, target_reg, payload});
                    else if (op == "-") out_shader.push_back(impulse_instruction_t{OP_STREAM_MATH_SUB, 0, target_reg, payload});
                    else if (op == "*") out_shader.push_back(impulse_instruction_t{OP_STREAM_MATH_MUL, 0, target_reg, payload});
                    else if (op == "/") out_shader.push_back(impulse_instruction_t{OP_STREAM_MATH_DIV, 0, target_reg, payload});
                    else if (op == "%") out_shader.push_back(impulse_instruction_t{OP_STREAM_MATH_MOD, 0, target_reg, payload});
                    else if (op == "==") out_shader.push_back(impulse_instruction_t{OP_STREAM_CMP_EQ, 0, target_reg, payload});
                    else if (op == "!=") out_shader.push_back(impulse_instruction_t{OP_STREAM_CMP_NEQ, 0, target_reg, payload});
                    else if (op == ">") out_shader.push_back(impulse_instruction_t{OP_STREAM_CMP_GT, 0, target_reg, payload});
                    else if (op == "<") out_shader.push_back(impulse_instruction_t{OP_STREAM_CMP_LT, 0, target_reg, payload});
                    else if (op == ">=") {
                        out_shader.push_back(impulse_instruction_t{OP_STREAM_CMP_LT, 0, target_reg, payload});
                        out_shader.push_back(impulse_instruction_t{OP_STREAM_LOGIC_NOT, 0, target_reg, target_reg});
                    } else if (op == "<=") {
                        out_shader.push_back(impulse_instruction_t{OP_STREAM_CMP_GT, 0, target_reg, payload});
                        out_shader.push_back(impulse_instruction_t{OP_STREAM_LOGIC_NOT, 0, target_reg, target_reg});
                    } else if (op == "&&") out_shader.push_back(impulse_instruction_t{OP_STREAM_LOGIC_AND, 0, target_reg, payload});
                    else if (op == "||") out_shader.push_back(impulse_instruction_t{OP_STREAM_LOGIC_OR, 0, target_reg, payload});
                    else return false;
                    return true;
                }
                case AstKind::TERNARY_OP: {
                    uint16_t r_cond = (target_reg + 1) % 16;
                    uint16_t r_true = (target_reg + 2) % 16;
                    if (!self(self, node->children[0], r_cond)) return false;
                    if (!self(self, node->children[2], target_reg)) return false;
                    if (!self(self, node->children[1], r_true)) return false;
                    out_shader.push_back(impulse_instruction_t{OP_STREAM_SELECT, 0, target_reg, static_cast<uint32_t>(r_cond | (r_true << 16))});
                    return true;
                }
                case AstKind::FUNCTION_CALL: {
                    const std::string& fn = node->text;
                    if ((fn == "popcount" || fn == "bit_count") && !node->children.empty()) {
                        const auto& arg = node->children[0];
                        if (arg->text == "lane" || (arg->kind == AstKind::MEMBER_ACCESS && arg->text == "lane")) {
                            out_shader.push_back(impulse_instruction_t{OP_STREAM_LOAD_SRC_ID, 1, target_reg, 0});
                            return true;
                        }
                    }
                    if (fn == "sum" || fn == "min" || fn == "max") {
                        if (!node->children.empty()) {
                            uint16_t r_val = (target_reg + 1) % 16;
                            if (!self(self, node->children[0], r_val)) return false;
                            uint16_t monoid = (fn == "sum") ? 0 : ((fn == "max") ? 1 : 2);
                            out_shader.push_back(impulse_instruction_t{OP_STREAM_REDUCE, 0, r_val, static_cast<uint32_t>(target_reg | (monoid << 16))});
                            return true;
                        }
                    }
                    if (fn == "argmin" || fn == "argmax") {
                        if (!node->children.empty()) {
                            uint16_t r_val = (target_reg + 1) % 16;
                            if (!self(self, node->children[0], r_val)) return false;
                            uint8_t opc = (fn == "argmin") ? OP_STREAM_REDUCE_ARGMIN : OP_STREAM_REDUCE_ARGMAX;
                            out_shader.push_back(impulse_instruction_t{opc, 0, r_val, static_cast<uint32_t>(target_reg | ((target_reg + 1) << 16))});
                            return true;
                        }
                    }
                    int func_id = resolve_math_func(fn);
                    if (func_id > 0 && !node->children.empty()) {
                        uint16_t r0 = (target_reg + 1) % 16;
                        if (!self(self, node->children[0], r0)) return false;
                        out_shader.push_back(impulse_instruction_t{OP_STREAM_MATH_UNARY, 0, target_reg, static_cast<uint32_t>(r0 | (static_cast<uint32_t>(func_id) << 16))});
                        return true;
                    }
                    return false;
                }
                default:
                    return false;
            }
        };

        if (!emit_stream_impl(emit_stream_impl, expr, out_stream_reg)) {
            return false;
        }

        if (is_filter) {
            out_shader.push_back(impulse_instruction_t{OP_STREAM_FILTER, 0, out_stream_reg, 0});
        }

        out_shader.push_back(impulse_instruction_t{OP_STREAM_FUNC_END, 0, 0, 0});
        return true;
    }

    /**
     * @brief Compiles an in-domain node CEL projection into VM instructions operating on vector registers.
     */
    static bool compile_node_vector_expr(
        const std::shared_ptr<AstNode>& expr,
        std::vector<impulse_instruction_t>& out_instructions,
        uint16_t out_reg,
        uint16_t& next_temp_reg,
        const std::unordered_map<std::string, uint16_t>* attr_name_to_id = nullptr,
        uint16_t domain_id = 0
    ) {
        if (!expr) return false;

        auto emit_vec_impl = [&](auto& self, const std::shared_ptr<AstNode>& node, uint16_t target_reg) -> bool {
            if (!node || target_reg >= 64) return false;

            switch (node->kind) {
                case AstKind::LITERAL_FLOAT:
                case AstKind::LITERAL_INT: {
                    float f = (node->kind == AstKind::LITERAL_FLOAT)
                        ? static_cast<float>(node->float_val)
                        : static_cast<float>(node->int_val);
                    uint32_t bits = 0;
                    std::memcpy(&bits, &f, sizeof(float));
                    out_instructions.push_back(impulse_instruction_t{OP_LOAD_CONST_FLOAT, 0, target_reg, bits});
                    return true;
                }
                case AstKind::IDENTIFIER:
                case AstKind::MEMBER_ACCESS: {
                    std::string attr_name = node->text;
                    uint16_t attr_idx = 0;
                    if (attr_name_to_id) {
                        auto it = attr_name_to_id->find(attr_name);
                        if (it != attr_name_to_id->end()) attr_idx = it->second;
                    }
                    uint32_t payload = static_cast<uint32_t>(domain_id) | (static_cast<uint32_t>(attr_idx) << 16);
                    out_instructions.push_back(impulse_instruction_t{OP_LOAD_COLUMN_VECTOR, 0, target_reg, payload});
                    return true;
                }
                case AstKind::UNARY_OP: {
                    if (node->text == "-") {
                        if (next_temp_reg >= 64) return false;
                        uint16_t r_child = next_temp_reg++;
                        if (!self(self, node->children[0], r_child)) return false;
                        if (next_temp_reg >= 64) return false;
                        uint16_t r_zero = next_temp_reg++;
                        uint32_t zbits = 0;
                        out_instructions.push_back(impulse_instruction_t{OP_LOAD_CONST_FLOAT, 0, r_zero, zbits});
                        uint32_t payload = static_cast<uint32_t>(r_zero) | (static_cast<uint32_t>(r_child) << 8) | (1u << 16);
                        out_instructions.push_back(impulse_instruction_t{OP_VEC_MATH_BINARY, 0, target_reg, payload});
                        return true;
                    }
                    return self(self, node->children[0], target_reg);
                }
                case AstKind::BINARY_OP: {
                    const std::string& op = node->text;
                    if (next_temp_reg >= 64) return false;
                    uint16_t r0 = next_temp_reg++;
                    if (!self(self, node->children[0], r0)) return false;
                    if (next_temp_reg >= 64) return false;
                    uint16_t r1 = next_temp_reg++;
                    if (!self(self, node->children[1], r1)) return false;

                    uint8_t func_id = 0;
                    if (op == "+") func_id = 0;
                    else if (op == "-") func_id = 1;
                    else if (op == "*") func_id = 2;
                    else if (op == "/") func_id = 3;
                    else if (op == "%") func_id = MATH_FUNC_FMOD;
                    else func_id = 0;

                    uint32_t payload = static_cast<uint32_t>(r0) | (static_cast<uint32_t>(r1) << 8) | (static_cast<uint32_t>(func_id) << 16);
                    out_instructions.push_back(impulse_instruction_t{OP_VEC_MATH_BINARY, 0, target_reg, payload});
                    return true;
                }
                case AstKind::FUNCTION_CALL: {
                    int func_id = resolve_math_func(node->text);
                    if (func_id <= 0) return false;
                    if (node->children.size() == 1) {
                        if (next_temp_reg >= 64) return false;
                        uint16_t r0 = next_temp_reg++;
                        if (!self(self, node->children[0], r0)) return false;
                        uint32_t payload = static_cast<uint32_t>(r0) | (static_cast<uint32_t>(func_id) << 8);
                        out_instructions.push_back(impulse_instruction_t{OP_VEC_MATH_UNARY, 0, target_reg, payload});
                        return true;
                    }
                    if (node->children.size() == 2) {
                        if (next_temp_reg >= 64) return false;
                        uint16_t r0 = next_temp_reg++;
                        if (!self(self, node->children[0], r0)) return false;
                        if (next_temp_reg >= 64) return false;
                        uint16_t r1 = next_temp_reg++;
                        if (!self(self, node->children[1], r1)) return false;
                        uint32_t payload = static_cast<uint32_t>(r0) | (static_cast<uint32_t>(r1) << 8) | (static_cast<uint32_t>(func_id) << 16);
                        out_instructions.push_back(impulse_instruction_t{OP_VEC_MATH_BINARY, 0, target_reg, payload});
                        return true;
                    }
                    if (node->children.size() == 3) {
                        if (next_temp_reg >= 64) return false;
                        uint16_t r0 = next_temp_reg++;
                        if (!self(self, node->children[0], r0)) return false;
                        if (next_temp_reg >= 64) return false;
                        uint16_t r1 = next_temp_reg++;
                        if (!self(self, node->children[1], r1)) return false;
                        if (next_temp_reg >= 64) return false;
                        uint16_t r2 = next_temp_reg++;
                        if (!self(self, node->children[2], r2)) return false;
                        uint32_t payload = static_cast<uint32_t>(r0) | (static_cast<uint32_t>(r1) << 8) | (static_cast<uint32_t>(r2) << 16) | (static_cast<uint32_t>(func_id) << 24);
                        out_instructions.push_back(impulse_instruction_t{OP_VEC_MATH_TERNARY, 0, target_reg, payload});
                        return true;
                    }
                    return false;
                }
                case AstKind::TERNARY_OP: {
                    if (next_temp_reg >= 64) return false;
                    uint16_t r_cond = next_temp_reg++;
                    if (!self(self, node->children[0], r_cond)) return false;
                    if (next_temp_reg >= 64) return false;
                    uint16_t r_true = next_temp_reg++;
                    if (!self(self, node->children[1], r_true)) return false;
                    if (next_temp_reg >= 64) return false;
                    uint16_t r_false = next_temp_reg++;
                    if (!self(self, node->children[2], r_false)) return false;

                    uint32_t payload = static_cast<uint32_t>(r_cond) | (static_cast<uint32_t>(r_true) << 8) | (static_cast<uint32_t>(r_false) << 16);
                    out_instructions.push_back(impulse_instruction_t{OP_VEC_BLEND, 0, target_reg, payload});
                    return true;
                }
                default:
                    return false;
            }
        };

        return emit_vec_impl(emit_vec_impl, expr, out_reg);
    }
};

/**
 * @brief Zero-Dependency AST Optimizer & Constant Folder for CEL ASTs.
 *
 * Implements:
 * 1. Constant folding across arithmetic, logical, comparison, and transcendental math functions.
 * 2. Algebraic simplification & identity reduction (x + 0, x * 1, x * 0, !(!x), etc.).
 * 3. Dead branch elimination for ternary expressions with constant conditions.
 */
class AstOptimizer {
public:
    static constexpr size_t MAX_PARSE_DEPTH = 256;

    static std::shared_ptr<AstNode> optimize(const std::shared_ptr<AstNode>& node, size_t depth = 0) {
        if (!node) return nullptr;
        if (depth > MAX_PARSE_DEPTH) {
            throw std::runtime_error("AstOptimizer recursion depth exceeded limit 256");
        }

        // 1. Bottom-up recursive optimization of children
        std::vector<std::shared_ptr<AstNode>> folded_children;
        for (const auto& child : node->children) {
            folded_children.push_back(optimize(child, depth + 1));
        }

        auto result = std::make_shared<AstNode>(*node);
        result->children = std::move(folded_children);

        // 2. Optimization passes
        switch (result->kind) {
            case AstKind::UNARY_OP:
                return fold_unary(result);
            case AstKind::BINARY_OP:
                return fold_binary(result);
            case AstKind::TERNARY_OP:
                return fold_ternary(result);
            case AstKind::FUNCTION_CALL:
                return fold_function_call(result);
            default:
                return result;
        }
    }

private:
    static std::shared_ptr<AstNode> fold_unary(const std::shared_ptr<AstNode>& node) {
        auto child = node->children[0];
        if (node->text == "!") {
            if (child->kind == AstKind::LITERAL_BOOL) {
                return AstNode::make_bool(!child->bool_val);
            }
            if (child->kind == AstKind::UNARY_OP && child->text == "!") {
                // Double negation: !(!x) -> x
                return child->children[0];
            }
        } else if (node->text == "-") {
            if (child->kind == AstKind::LITERAL_INT) {
                return AstNode::make_int(-child->int_val);
            }
            if (child->kind == AstKind::LITERAL_FLOAT) {
                return AstNode::make_float(-child->float_val);
            }
        } else if (node->text == "~") {
            if (child->kind == AstKind::LITERAL_INT) {
                return AstNode::make_int(~child->int_val);
            }
            if (child->kind == AstKind::UNARY_OP && child->text == "~") {
                return child->children[0];
            }
        }
        return node;
    }

    static std::shared_ptr<AstNode> fold_binary(const std::shared_ptr<AstNode>& node) {
        auto left = node->children[0];
        auto right = node->children[1];
        const std::string& op = node->text;

        // Monotonic Inverse Pushdown
        if (op == "<" || op == "<=" || op == ">" || op == ">=" || op == "==" || op == "!=") {
            auto check_pushdown = [&](const std::shared_ptr<AstNode>& lhs, const std::shared_ptr<AstNode>& rhs, bool reverse_op) -> std::shared_ptr<AstNode> {
                if (lhs->kind == AstKind::FUNCTION_CALL && lhs->children.size() == 1 &&
                    (rhs->kind == AstKind::LITERAL_FLOAT || rhs->kind == AstKind::LITERAL_INT)) {
                    double c = (rhs->kind == AstKind::LITERAL_FLOAT) ? rhs->float_val : static_cast<double>(rhs->int_val);
                    std::string new_op = op;
                    if (reverse_op) {
                        if (op == "<") new_op = ">";
                        else if (op == "<=") new_op = ">=";
                        else if (op == ">") new_op = "<";
                        else if (op == ">=") new_op = "<=";
                    }
                    if (lhs->text == "sqrt") {
                        if (c >= 0) return AstNode::make_binary(new_op, lhs->children[0], AstNode::make_float(c * c));
                    }
                }
                return nullptr;
            };
            if (auto res = check_pushdown(left, right, false)) return res;
            if (auto res = check_pushdown(right, left, true)) return res;
        }

        // 1. Integer Constant Folding
        if (left->kind == AstKind::LITERAL_INT && right->kind == AstKind::LITERAL_INT) {
            int64_t a = left->int_val;
            int64_t b = right->int_val;
            if (op == "+") return AstNode::make_int(a + b);
            if (op == "-") return AstNode::make_int(a - b);
            if (op == "*") return AstNode::make_int(a * b);
            if (op == "/" && b != 0) return AstNode::make_int(a / b);
            if (op == "%" && b != 0) return AstNode::make_int(a % b);
            if (op == "==") return AstNode::make_bool(a == b);
            if (op == "!=") return AstNode::make_bool(a != b);
            if (op == "<")  return AstNode::make_bool(a < b);
            if (op == "<=") return AstNode::make_bool(a <= b);
            if (op == ">")  return AstNode::make_bool(a > b);
            if (op == ">=") return AstNode::make_bool(a >= b);
            if (op == "&")  return AstNode::make_int(a & b);
            if (op == "|")  return AstNode::make_int(a | b);
            if (op == "^")  return AstNode::make_int(a ^ b);
            if (op == "<<") return AstNode::make_int((b < 0 || b >= 64) ? 0 : static_cast<int64_t>(static_cast<uint64_t>(a) << b));
            if (op == ">>") return AstNode::make_int((b < 0 || b >= 64) ? 0 : (a >> b));
            if (op == ">>>") return AstNode::make_int((b < 0 || b >= 64) ? 0 : static_cast<int64_t>(static_cast<uint64_t>(a) >> b));
        }

        // 2. Float Constant Folding
        if ((left->kind == AstKind::LITERAL_FLOAT || left->kind == AstKind::LITERAL_INT) &&
            (right->kind == AstKind::LITERAL_FLOAT || right->kind == AstKind::LITERAL_INT)) {
            double a = (left->kind == AstKind::LITERAL_FLOAT) ? left->float_val : static_cast<double>(left->int_val);
            double b = (right->kind == AstKind::LITERAL_FLOAT) ? right->float_val : static_cast<double>(right->int_val);
            if (op == "+") return AstNode::make_float(a + b);
            if (op == "-") return AstNode::make_float(a - b);
            if (op == "*") return AstNode::make_float(a * b);
            if (op == "/" && b != 0.0) return AstNode::make_float(a / b);
            if (op == "==") return AstNode::make_bool(a == b);
            if (op == "!=") return AstNode::make_bool(a != b);
            if (op == "<")  return AstNode::make_bool(a < b);
            if (op == "<=") return AstNode::make_bool(a <= b);
            if (op == ">")  return AstNode::make_bool(a > b);
            if (op == ">=") return AstNode::make_bool(a >= b);
        }

        // 3. Boolean Constant Folding
        if (left->kind == AstKind::LITERAL_BOOL && right->kind == AstKind::LITERAL_BOOL) {
            bool a = left->bool_val;
            bool b = right->bool_val;
            if (op == "&&") return AstNode::make_bool(a && b);
            if (op == "||") return AstNode::make_bool(a || b);
            if (op == "==") return AstNode::make_bool(a == b);
            if (op == "!=") return AstNode::make_bool(a != b);
        }

        // 4. Algebraic Identities
        if (op == "+") {
            if (is_zero(left)) return right;
            if (is_zero(right)) return left;
        } else if (op == "-") {
            if (is_zero(right)) return left;
        } else if (op == "*") {
            if (is_one(left)) return right;
            if (is_one(right)) return left;
        } else if (op == "/") {
            if (is_one(right)) return left;
        } else if (op == "&&") {
            if (left->kind == AstKind::LITERAL_BOOL) {
                return left->bool_val ? right : AstNode::make_bool(false);
            }
            if (right->kind == AstKind::LITERAL_BOOL) {
                return right->bool_val ? left : AstNode::make_bool(false);
            }
        } else if (op == "||") {
            if (left->kind == AstKind::LITERAL_BOOL) {
                return left->bool_val ? AstNode::make_bool(true) : right;
            }
            if (right->kind == AstKind::LITERAL_BOOL) {
                return right->bool_val ? AstNode::make_bool(true) : left;
            }
        }

        return node;
    }

    static std::shared_ptr<AstNode> fold_ternary(const std::shared_ptr<AstNode>& node) {
        auto cond = node->children[0];
        auto then_branch = node->children[1];
        auto else_branch = node->children[2];

        if (cond->kind == AstKind::LITERAL_BOOL) {
            return cond->bool_val ? then_branch : else_branch;
        }
        return node;
    }

    static std::shared_ptr<AstNode> fold_function_call(const std::shared_ptr<AstNode>& node) {
        int func_id = CelCompiler::resolve_math_func(node->text);
        if (func_id <= 0) return node;

        // Unary constant folding
        if (node->children.size() == 1) {
            auto arg = node->children[0];
            if (arg->kind == AstKind::LITERAL_FLOAT || arg->kind == AstKind::LITERAL_INT) {
                double val = (arg->kind == AstKind::LITERAL_FLOAT) ? arg->float_val : static_cast<double>(arg->int_val);
                double res = impulse_math_unary_f64(static_cast<uint8_t>(func_id), val);
                return AstNode::make_float(res);
            }
        }

        // Binary constant folding
        if (node->children.size() == 2) {
            auto arg1 = node->children[0];
            auto arg2 = node->children[1];
            if ((arg1->kind == AstKind::LITERAL_FLOAT || arg1->kind == AstKind::LITERAL_INT) &&
                (arg2->kind == AstKind::LITERAL_FLOAT || arg2->kind == AstKind::LITERAL_INT)) {
                double a = (arg1->kind == AstKind::LITERAL_FLOAT) ? arg1->float_val : static_cast<double>(arg1->int_val);
                double b = (arg2->kind == AstKind::LITERAL_FLOAT) ? arg2->float_val : static_cast<double>(arg2->int_val);
                double res = impulse_math_binary_f64(static_cast<uint8_t>(func_id), a, b);
                return AstNode::make_float(res);
            }
        }

        // Ternary constant folding
        if (node->children.size() == 3) {
            auto arg1 = node->children[0];
            auto arg2 = node->children[1];
            auto arg3 = node->children[2];
            if ((arg1->kind == AstKind::LITERAL_FLOAT || arg1->kind == AstKind::LITERAL_INT) &&
                (arg2->kind == AstKind::LITERAL_FLOAT || arg2->kind == AstKind::LITERAL_INT) &&
                (arg3->kind == AstKind::LITERAL_FLOAT || arg3->kind == AstKind::LITERAL_INT)) {
                double a = (arg1->kind == AstKind::LITERAL_FLOAT) ? arg1->float_val : static_cast<double>(arg1->int_val);
                double b = (arg2->kind == AstKind::LITERAL_FLOAT) ? arg2->float_val : static_cast<double>(arg2->int_val);
                double c = (arg3->kind == AstKind::LITERAL_FLOAT) ? arg3->float_val : static_cast<double>(arg3->int_val);
                double res = impulse_math_ternary_f64(static_cast<uint8_t>(func_id), a, b, c);
                return AstNode::make_float(res);
            }
        }

        return node;
    }

    static bool is_zero(const std::shared_ptr<AstNode>& node) {
        if (!node) return false;
        if (node->kind == AstKind::LITERAL_INT && node->int_val == 0) return true;
        if (node->kind == AstKind::LITERAL_FLOAT && node->float_val == 0.0) return true;
        return false;
    }

    static bool is_one(const std::shared_ptr<AstNode>& node) {
        if (!node) return false;
        if (node->kind == AstKind::LITERAL_INT && node->int_val == 1) return true;
        if (node->kind == AstKind::LITERAL_FLOAT && node->float_val == 1.0) return true;
        return false;
    }
};

/**
 * @brief Runtime evaluator for CEL Multi-Lane predicates (.filter / .pruneWhere)
 *        and selective per-lane mask mutations (.project("lane = ...")).
 */
class CelMultiLaneEvaluator {
public:
    using AttrMap = std::unordered_map<std::string, int64_t>;
    using FloatAttrMap = std::unordered_map<std::string, double>;

    // -----------------------------------------------------------------------
    // eval_double (Primary numeric & floating-point evaluator)
    // -----------------------------------------------------------------------
    static double eval_double(const std::shared_ptr<AstNode>& node,
                              const uint64_t* lane_words,
                              size_t num_words,
                              const FloatAttrMap* float_attrs = nullptr,
                              const AttrMap* int_attrs = nullptr) {
        if (!node) return 0.0;
        switch (node->kind) {
            case AstKind::LITERAL_FLOAT:
                return node->float_val;
            case AstKind::LITERAL_INT:
                return static_cast<double>(node->int_val);
            case AstKind::LITERAL_BOOL:
                return node->bool_val ? 1.0 : 0.0;
            case AstKind::IDENTIFIER:
            case AstKind::MEMBER_ACCESS: {
                if (node->text == "lane") {
                    return static_cast<double>((lane_words && num_words > 0) ? lane_words[0] : 0ULL);
                }
                if (float_attrs) {
                    auto it = float_attrs->find(node->text);
                    if (it != float_attrs->end()) return it->second;
                }
                if (int_attrs) {
                    auto it = int_attrs->find(node->text);
                    if (it != int_attrs->end()) return static_cast<double>(it->second);
                }
                return 0.0;
            }
            case AstKind::UNARY_OP: {
                if (node->text == "!") {
                    return eval_bool(node->children[0], lane_words, num_words, int_attrs, float_attrs) ? 0.0 : 1.0;
                }
                double v = eval_double(node->children[0], lane_words, num_words, float_attrs, int_attrs);
                if (node->text == "-") return -v;
                if (node->text == "~") return static_cast<double>(~static_cast<int64_t>(v));
                return v;
            }
            case AstKind::TERNARY_OP: {
                bool c = eval_bool(node->children[0], lane_words, num_words, int_attrs, float_attrs);
                return eval_double(c ? node->children[1] : node->children[2], lane_words, num_words, float_attrs, int_attrs);
            }
            case AstKind::BINARY_OP: {
                const std::string& op = node->text;
                if (op == "=") {
                    return eval_double(node->children[1], lane_words, num_words, float_attrs, int_attrs);
                }
                if (op == "in") {
                    return eval_bool(node, lane_words, num_words, int_attrs, float_attrs) ? 1.0 : 0.0;
                }
                if (op == "&&" || op == "||" || op == "==" || op == "!=" ||
                    op == "<" || op == "<=" || op == ">" || op == ">=") {
                    return eval_bool(node, lane_words, num_words, int_attrs, float_attrs) ? 1.0 : 0.0;
                }
                double a = eval_double(node->children[0], lane_words, num_words, float_attrs, int_attrs);
                double b = eval_double(node->children[1], lane_words, num_words, float_attrs, int_attrs);
                if (op == "+") return a + b;
                if (op == "-") return a - b;
                if (op == "*") return a * b;
                if (op == "/") return (b != 0.0 && !std::isnan(b)) ? (a / b) : 0.0;
                if (op == "%") return (b != 0.0) ? std::fmod(a, b) : 0.0;
                // Bitwise binary operators
                int64_t ia = static_cast<int64_t>(a);
                int64_t ib = static_cast<int64_t>(b);
                if (op == "&") return static_cast<double>(ia & ib);
                if (op == "|") return static_cast<double>(ia | ib);
                if (op == "^") return static_cast<double>(ia ^ ib);
                if (op == "<<") return static_cast<double>((ib < 0 || ib >= 64) ? 0 : (static_cast<uint64_t>(ia) << ib));
                if (op == ">>") return static_cast<double>((ib < 0 || ib >= 64) ? 0 : (ia >> ib));
                if (op == ">>>") return static_cast<double>((ib < 0 || ib >= 64) ? 0 : static_cast<int64_t>(static_cast<uint64_t>(ia) >> ib));
                return 0.0;
            }
            case AstKind::FUNCTION_CALL: {
                const std::string& fn = node->text;
                if ((fn == "popcount" || fn == "bit_count") && !node->children.empty()) {
                    if (node->children[0]->text == "lane" ||
                        (node->children[0]->kind == AstKind::MEMBER_ACCESS && node->children[0]->text == "lane")) {
                        size_t total = 0;
                        for (size_t w = 0; w < num_words; ++w) total += std::popcount(lane_words[w]);
                        return static_cast<double>(total);
                    }
                    uint64_t v = eval_bits(node->children[0], lane_words, num_words, int_attrs, float_attrs);
                    return static_cast<double>(std::popcount(v));
                }
                if ((fn == "first_lane" || fn == "ctz") && !node->children.empty()) {
                    if (node->children[0]->text == "lane" ||
                        (node->children[0]->kind == AstKind::MEMBER_ACCESS && node->children[0]->text == "lane")) {
                        for (size_t w = 0; w < num_words; ++w) {
                            if (lane_words[w] != 0ULL) {
                                return static_cast<double>(w * 64 + std::countr_zero(lane_words[w]));
                            }
                        }
                        return -1.0;
                    }
                    uint64_t v = eval_bits(node->children[0], lane_words, num_words, int_attrs, float_attrs);
                    return (v == 0ULL) ? -1.0 : static_cast<double>(std::countr_zero(v));
                }
                if (fn == "last_lane" && !node->children.empty()) {
                    if (node->children[0]->text == "lane" ||
                        (node->children[0]->kind == AstKind::MEMBER_ACCESS && node->children[0]->text == "lane")) {
                        for (size_t w = num_words; w > 0; --w) {
                            if (lane_words[w - 1] != 0ULL) {
                                return static_cast<double>((w - 1) * 64 + 63 - std::countl_zero(lane_words[w - 1]));
                            }
                        }
                        return -1.0;
                    }
                    uint64_t v = eval_bits(node->children[0], lane_words, num_words, int_attrs, float_attrs);
                    return (v == 0ULL) ? -1.0 : static_cast<double>(63 - std::countl_zero(v));
                }
                if (fn == "clz" && !node->children.empty()) {
                    uint64_t v = eval_bits(node->children[0], lane_words, num_words, int_attrs, float_attrs);
                    return (v == 0ULL) ? 64.0 : static_cast<double>(std::countl_zero(v));
                }
                if (fn == "has_any" || fn == "has_all" || fn == "has_none" || fn == "has_lane") {
                    return eval_bool(node, lane_words, num_words, int_attrs, float_attrs) ? 1.0 : 0.0;
                }

                // 48-Function Vector Math Catalog Dispatch
                int func_id = CelCompiler::resolve_math_func(fn);
                if (func_id > 0) {
                    if (node->children.size() == 1) {
                        double a = eval_double(node->children[0], lane_words, num_words, float_attrs, int_attrs);
                        return impulse_math_unary_f64(static_cast<uint8_t>(func_id), a);
                    }
                    if (node->children.size() == 2) {
                        double a = eval_double(node->children[0], lane_words, num_words, float_attrs, int_attrs);
                        double b = eval_double(node->children[1], lane_words, num_words, float_attrs, int_attrs);
                        return impulse_math_binary_f64(static_cast<uint8_t>(func_id), a, b);
                    }
                    if (node->children.size() == 3) {
                        double a = eval_double(node->children[0], lane_words, num_words, float_attrs, int_attrs);
                        double b = eval_double(node->children[1], lane_words, num_words, float_attrs, int_attrs);
                        double c = eval_double(node->children[2], lane_words, num_words, float_attrs, int_attrs);
                        return impulse_math_ternary_f64(static_cast<uint8_t>(func_id), a, b, c);
                    }
                }
                return 0.0;
            }
            default:
                return 0.0;
        }
    }

    static double eval_double(const std::shared_ptr<AstNode>& node,
                              uint64_t lane_mask,
                              const FloatAttrMap* float_attrs = nullptr,
                              const AttrMap* int_attrs = nullptr) {
        return eval_double(node, &lane_mask, 1, float_attrs, int_attrs);
    }

    // -----------------------------------------------------------------------
    // eval_bits / eval_int (Exact 64-bit integer & bitmask evaluator)
    // -----------------------------------------------------------------------
    static uint64_t eval_bits(const std::shared_ptr<AstNode>& node,
                              const uint64_t* lane_words,
                              size_t num_words,
                              const AttrMap* node_attrs = nullptr,
                              const FloatAttrMap* float_attrs = nullptr) {
        if (!node) return 0ULL;
        switch (node->kind) {
            case AstKind::LITERAL_INT:
                return static_cast<uint64_t>(node->int_val);
            case AstKind::LITERAL_BOOL:
                return node->bool_val ? 1ULL : 0ULL;
            case AstKind::LITERAL_FLOAT:
                return static_cast<uint64_t>(static_cast<int64_t>(node->float_val));
            case AstKind::IDENTIFIER:
            case AstKind::MEMBER_ACCESS: {
                if (node->text == "lane") {
                    return (lane_words && num_words > 0) ? lane_words[0] : 0ULL;
                }
                if (node_attrs) {
                    auto it = node_attrs->find(node->text);
                    if (it != node_attrs->end()) return static_cast<uint64_t>(it->second);
                }
                if (float_attrs) {
                    auto it = float_attrs->find(node->text);
                    if (it != float_attrs->end()) return static_cast<uint64_t>(static_cast<int64_t>(it->second));
                }
                return 0ULL;
            }
            case AstKind::UNARY_OP: {
                if (node->text == "!") {
                    return eval_bool(node->children[0], lane_words, num_words, node_attrs, float_attrs) ? 0ULL : 1ULL;
                }
                if (node->text == "~") {
                    return ~eval_bits(node->children[0], lane_words, num_words, node_attrs, float_attrs);
                }
                if (node->text == "-") {
                    int64_t sv = static_cast<int64_t>(eval_bits(node->children[0], lane_words, num_words, node_attrs, float_attrs));
                    return static_cast<uint64_t>(0ULL - static_cast<uint64_t>(sv));
                }
                return eval_bits(node->children[0], lane_words, num_words, node_attrs, float_attrs);
            }
            case AstKind::TERNARY_OP: {
                bool c = eval_bool(node->children[0], lane_words, num_words, node_attrs, float_attrs);
                return eval_bits(c ? node->children[1] : node->children[2], lane_words, num_words, node_attrs, float_attrs);
            }
            case AstKind::BINARY_OP: {
                const std::string& op = node->text;
                if (op == "=") {
                    return eval_bits(node->children[1], lane_words, num_words, node_attrs, float_attrs);
                }
                if (op == "&" || op == "|" || op == "^" || op == "<<" || op == ">>" || op == ">>>") {
                    uint64_t ua = eval_bits(node->children[0], lane_words, num_words, node_attrs, float_attrs);
                    uint64_t ub = eval_bits(node->children[1], lane_words, num_words, node_attrs, float_attrs);
                    int64_t ia = static_cast<int64_t>(ua);
                    int64_t ib = static_cast<int64_t>(ub);
                    if (op == "&") return ua & ub;
                    if (op == "|") return ua | ub;
                    if (op == "^") return ua ^ ub;
                    if (op == "<<") return (ib < 0 || ib >= 64) ? 0ULL : (ua << ib);
                    if (op == ">>") return (ib < 0 || ib >= 64) ? 0ULL : static_cast<uint64_t>(ia >> ib);
                    if (op == ">>>") return (ib < 0 || ib >= 64) ? 0ULL : (ua >> ib);
                }
                return static_cast<uint64_t>(static_cast<int64_t>(eval_double(node, lane_words, num_words, float_attrs, node_attrs)));
            }
            default:
                return static_cast<uint64_t>(static_cast<int64_t>(eval_double(node, lane_words, num_words, float_attrs, node_attrs)));
        }
    }

    static int64_t eval_int(const std::shared_ptr<AstNode>& node,
                            const uint64_t* lane_words,
                            size_t num_words,
                            const AttrMap* node_attrs = nullptr,
                            const FloatAttrMap* float_attrs = nullptr) {
        return static_cast<int64_t>(eval_bits(node, lane_words, num_words, node_attrs, float_attrs));
    }

    static int64_t eval_int(const std::shared_ptr<AstNode>& node,
                            uint64_t lane_mask,
                            const AttrMap* node_attrs = nullptr,
                            const FloatAttrMap* float_attrs = nullptr) {
        return static_cast<int64_t>(eval_bits(node, &lane_mask, 1, node_attrs, float_attrs));
    }

    // -----------------------------------------------------------------------
    // eval_bool
    // -----------------------------------------------------------------------
    static bool eval_bool(const std::shared_ptr<AstNode>& node,
                          const uint64_t* lane_words,
                          size_t num_words,
                          const AttrMap* node_attrs = nullptr,
                          const FloatAttrMap* float_attrs = nullptr) {
        if (!node) return false;
        switch (node->kind) {
            case AstKind::LITERAL_BOOL:
                return node->bool_val;
            case AstKind::LITERAL_INT:
                return node->int_val != 0;
            case AstKind::LITERAL_FLOAT:
                return node->float_val != 0.0;
            case AstKind::UNARY_OP:
                if (node->text == "!") return !eval_bool(node->children[0], lane_words, num_words, node_attrs, float_attrs);
                return eval_double(node, lane_words, num_words, float_attrs, node_attrs) != 0.0;
            case AstKind::TERNARY_OP:
                return eval_bool(eval_bool(node->children[0], lane_words, num_words, node_attrs, float_attrs)
                                     ? node->children[1]
                                     : node->children[2],
                                 lane_words, num_words, node_attrs, float_attrs);
            case AstKind::BINARY_OP: {
                const std::string& op = node->text;
                if (op == "&&") {
                    return eval_bool(node->children[0], lane_words, num_words, node_attrs, float_attrs) &&
                           eval_bool(node->children[1], lane_words, num_words, node_attrs, float_attrs);
                }
                if (op == "||") {
                    return eval_bool(node->children[0], lane_words, num_words, node_attrs, float_attrs) ||
                           eval_bool(node->children[1], lane_words, num_words, node_attrs, float_attrs);
                }
                if (op == "in") {
                    double val = eval_double(node->children[0], lane_words, num_words, float_attrs, node_attrs);
                    const auto& rhs = node->children[1];
                    if (rhs && rhs->kind == AstKind::LIST_LITERAL) {
                        for (const auto& elem : rhs->children) {
                            double elem_val = eval_double(elem, lane_words, num_words, float_attrs, node_attrs);
                            if (std::abs(val - elem_val) < 1e-9) return true;
                        }
                        return false;
                    }
                    return false;
                }
                double a = eval_double(node->children[0], lane_words, num_words, float_attrs, node_attrs);
                double b = eval_double(node->children[1], lane_words, num_words, float_attrs, node_attrs);
                if (op == "==") return std::abs(a - b) < 1e-9;
                if (op == "!=") return std::abs(a - b) >= 1e-9;
                if (op == "<")  return a < b;
                if (op == "<=") return a <= b;
                if (op == ">")  return a > b;
                if (op == ">=") return a >= b;
                return eval_double(node, lane_words, num_words, float_attrs, node_attrs) != 0.0;
            }
            case AstKind::FUNCTION_CALL: {
                const std::string& fn = node->text;
                if (fn == "has_any" && !node->children.empty()) {
                    if (node->children.size() == 1 ||
                        node->children[0]->text == "lane" ||
                        (node->children[0]->kind == AstKind::MEMBER_ACCESS && node->children[0]->text == "lane")) {
                        if (node->children.size() >= 2) {
                            uint64_t req = eval_bits(node->children[1], lane_words, num_words, node_attrs, float_attrs);
                            return ((lane_words && num_words > 0 ? lane_words[0] : 0ULL) & req) != 0ULL;
                        }
                        for (size_t w = 0; w < num_words; ++w) {
                            if (lane_words[w] != 0ULL) return true;
                        }
                        return false;
                    }
                    uint64_t m = eval_bits(node->children[0], lane_words, num_words, node_attrs, float_attrs);
                    uint64_t req = eval_bits(node->children[1], lane_words, num_words, node_attrs, float_attrs);
                    return (m & req) != 0ULL;
                }
                if (fn == "has_all" && node->children.size() >= 2) {
                    uint64_t m = (node->children[0]->text == "lane") ? (lane_words && num_words > 0 ? lane_words[0] : 0ULL)
                                                                     : eval_bits(node->children[0], lane_words, num_words, node_attrs, float_attrs);
                    uint64_t req = eval_bits(node->children[1], lane_words, num_words, node_attrs, float_attrs);
                    return (m & req) == req;
                }
                if (fn == "has_none" && node->children.size() >= 2) {
                    uint64_t m = (node->children[0]->text == "lane") ? (lane_words && num_words > 0 ? lane_words[0] : 0ULL)
                                                                     : eval_bits(node->children[0], lane_words, num_words, node_attrs, float_attrs);
                    uint64_t forbid = eval_bits(node->children[1], lane_words, num_words, node_attrs, float_attrs);
                    return (m & forbid) == 0ULL;
                }
                if (fn == "has_lane" && node->children.size() >= 2) {
                    int64_t idx = static_cast<int64_t>(eval_double(node->children[1], lane_words, num_words, float_attrs, node_attrs));
                    if (idx < 0) return false;
                    size_t w = static_cast<size_t>(idx) / 64;
                    size_t b = static_cast<size_t>(idx) % 64;
                    if (w < num_words) {
                        return ((lane_words[w] >> b) & 1ULL) != 0ULL;
                    }
                    return false;
                }
                return eval_double(node, lane_words, num_words, float_attrs, node_attrs) != 0.0;
            }
            default:
                return eval_double(node, lane_words, num_words, float_attrs, node_attrs) != 0.0;
        }
    }

    static bool eval_bool(const std::shared_ptr<AstNode>& node,
                          uint64_t lane_mask,
                          const AttrMap* node_attrs = nullptr,
                          const FloatAttrMap* float_attrs = nullptr) {
        return eval_bool(node, &lane_mask, 1, node_attrs, float_attrs);
    }

    // -----------------------------------------------------------------------
    // eval_project_lane
    // -----------------------------------------------------------------------
    static uint64_t eval_project_lane(const std::shared_ptr<AstNode>& node,
                                      uint64_t lane_mask,
                                      const AttrMap* node_attrs = nullptr,
                                      const FloatAttrMap* float_attrs = nullptr) {
        return static_cast<uint64_t>(eval_int(node, lane_mask, node_attrs, float_attrs));
    }

    static void eval_project_lanes(const std::shared_ptr<AstNode>& node,
                                   uint64_t* lane_words,
                                   size_t num_words,
                                   const AttrMap* node_attrs = nullptr,
                                   const FloatAttrMap* float_attrs = nullptr) {
        if (!lane_words || num_words == 0) return;
        lane_words[0] = static_cast<uint64_t>(eval_int(node, lane_words, num_words, node_attrs, float_attrs));
    }
};

/**
 * @brief Typed value with Kleene 3-valued NULL semantics (Spec v0.10.0).
 */
struct CelValue {
    enum class Kind : uint8_t { Null = 0, Bool, Int64, Uint64, Double, String };

    Kind kind = Kind::Null;
    bool bool_val = false;
    bool uses_owned_str = false;
    int64_t int_val = 0;
    uint64_t uint_val = 0;
    double dbl_val = 0.0;
    std::string_view str_view{};
    std::string str_owned{};

    static CelValue make_null() { return CelValue{}; }
    static CelValue make_bool(bool b) {
        CelValue v;
        v.kind = Kind::Bool;
        v.bool_val = b;
        return v;
    }
    static CelValue make_int(int64_t x) {
        CelValue v;
        v.kind = Kind::Int64;
        v.int_val = x;
        return v;
    }
    static CelValue make_uint(uint64_t x) {
        CelValue v;
        v.kind = Kind::Uint64;
        v.uint_val = x;
        return v;
    }
    static CelValue make_double(double x) {
        if (std::isnan(x)) return make_null();
        CelValue v;
        v.kind = Kind::Double;
        v.dbl_val = x;
        return v;
    }
    static CelValue make_string_view(std::string_view sv) {
        CelValue v;
        v.kind = Kind::String;
        v.uses_owned_str = false;
        v.str_view = sv;
        return v;
    }
    static CelValue make_string(std::string s) {
        CelValue v;
        v.kind = Kind::String;
        v.uses_owned_str = true;
        v.str_owned = std::move(s);
        return v;
    }

    bool is_null() const { return kind == Kind::Null; }

    std::string_view as_string_view() const {
        return uses_owned_str ? std::string_view(str_owned) : str_view;
    }

    bool to_double(double& out) const {
        switch (kind) {
            case Kind::Bool:   out = bool_val ? 1.0 : 0.0; return true;
            case Kind::Int64:  out = static_cast<double>(int_val); return true;
            case Kind::Uint64: out = static_cast<double>(uint_val); return true;
            case Kind::Double:
                if (std::isnan(dbl_val)) return false;
                out = dbl_val;
                return true;
            default:
                return false;
        }
    }

    bool to_int64(int64_t& out) const {
        switch (kind) {
            case Kind::Bool:   out = bool_val ? 1 : 0; return true;
            case Kind::Int64:  out = int_val; return true;
            case Kind::Uint64: out = static_cast<int64_t>(uint_val); return true;
            case Kind::Double:
                if (std::isnan(dbl_val)) return false;
                out = static_cast<int64_t>(dbl_val);
                return true;
            default:
                return false;
        }
    }

    // Returns 1 = true, 0 = false, -1 = Kleene Unknown (NULL)
    int to_tribool() const {
        switch (kind) {
            case Kind::Null:   return -1;
            case Kind::Bool:   return bool_val ? 1 : 0;
            case Kind::Int64:  return (int_val != 0) ? 1 : 0;
            case Kind::Uint64: return (uint_val != 0) ? 1 : 0;
            case Kind::Double: return std::isnan(dbl_val) ? -1 : ((dbl_val != 0.0) ? 1 : 0);
            case Kind::String: return as_string_view().empty() ? 0 : 1;
        }
        return -1;
    }
};

/**
 * @brief Spec v0.10.0 Kleene 3-valued CEL evaluator for node, edge, and multi-lane predicates.
 */
class CelKleeneEvaluator {
public:
    static CelValue compare_values(const std::string& op, const CelValue& a, const CelValue& b) {
        if (a.is_null() || b.is_null()) return CelValue::make_null();

        if (a.kind == CelValue::Kind::String || b.kind == CelValue::Kind::String) {
            if (a.kind != CelValue::Kind::String || b.kind != CelValue::Kind::String) {
                if (op == "==") return CelValue::make_bool(false);
                if (op == "!=") return CelValue::make_bool(true);
                return CelValue::make_null();
            }
            std::string_view sa = a.as_string_view();
            std::string_view sb = b.as_string_view();
            if (op == "==") return CelValue::make_bool(sa == sb);
            if (op == "!=") return CelValue::make_bool(sa != sb);
            if (op == "<")  return CelValue::make_bool(sa < sb);
            if (op == "<=") return CelValue::make_bool(sa <= sb);
            if (op == ">")  return CelValue::make_bool(sa > sb);
            if (op == ">=") return CelValue::make_bool(sa >= sb);
            return CelValue::make_null();
        }

        if (a.kind == CelValue::Kind::Bool && b.kind == CelValue::Kind::Bool) {
            if (op == "==") return CelValue::make_bool(a.bool_val == b.bool_val);
            if (op == "!=") return CelValue::make_bool(a.bool_val != b.bool_val);
            if (op == "<")  return CelValue::make_bool(!a.bool_val && b.bool_val);
            if (op == "<=") return CelValue::make_bool(!a.bool_val || b.bool_val);
            if (op == ">")  return CelValue::make_bool(a.bool_val && !b.bool_val);
            if (op == ">=") return CelValue::make_bool(a.bool_val || !b.bool_val);
            return CelValue::make_null();
        }

        // Exact integer / bool comparisons (avoids double precision loss on int64 PKs & timestamps)
        if (a.kind != CelValue::Kind::Double && b.kind != CelValue::Kind::Double) {
            auto as_signed_or_unsigned = [](const CelValue& v, bool& is_neg, uint64_t& mag) {
                if (v.kind == CelValue::Kind::Bool) {
                    is_neg = false;
                    mag = v.bool_val ? 1ULL : 0ULL;
                } else if (v.kind == CelValue::Kind::Uint64) {
                    is_neg = false;
                    mag = v.uint_val;
                } else {
                    if (v.int_val < 0) {
                        is_neg = true;
                        mag = static_cast<uint64_t>(-(v.int_val + 1)) + 1ULL;
                    } else {
                        is_neg = false;
                        mag = static_cast<uint64_t>(v.int_val);
                    }
                }
            };
            bool neg_a = false, neg_b = false;
            uint64_t mag_a = 0, mag_b = 0;
            as_signed_or_unsigned(a, neg_a, mag_a);
            as_signed_or_unsigned(b, neg_b, mag_b);
            int cmp = 0;
            if (neg_a != neg_b) {
                cmp = neg_a ? -1 : 1;
            } else if (neg_a) {
                cmp = (mag_a > mag_b) ? -1 : ((mag_a < mag_b) ? 1 : 0);
            } else {
                cmp = (mag_a < mag_b) ? -1 : ((mag_a > mag_b) ? 1 : 0);
            }
            if (op == "==") return CelValue::make_bool(cmp == 0);
            if (op == "!=") return CelValue::make_bool(cmp != 0);
            if (op == "<")  return CelValue::make_bool(cmp < 0);
            if (op == "<=") return CelValue::make_bool(cmp <= 0);
            if (op == ">")  return CelValue::make_bool(cmp > 0);
            if (op == ">=") return CelValue::make_bool(cmp >= 0);
            return CelValue::make_null();
        }

        double da = 0.0, db = 0.0;
        if (!a.to_double(da) || !b.to_double(db)) return CelValue::make_null();
        if (op == "==") return CelValue::make_bool(da == db);
        if (op == "!=") return CelValue::make_bool(da != db);
        if (op == "<")  return CelValue::make_bool(da < db);
        if (op == "<=") return CelValue::make_bool(da <= db);
        if (op == ">")  return CelValue::make_bool(da > db);
        if (op == ">=") return CelValue::make_bool(da >= db);
        return CelValue::make_null();
    }

    template <typename ResolveAttrFn>
    static CelValue eval(
        const std::shared_ptr<AstNode>& node,
        ResolveAttrFn&& resolve_attr,
        const uint64_t* lane_words = nullptr,
        size_t num_lane_words = 0
    ) {
        if (!node) return CelValue::make_null();
        switch (node->kind) {
            case AstKind::LITERAL_BOOL:
                return CelValue::make_bool(node->bool_val);
            case AstKind::LITERAL_INT:
                return CelValue::make_int(node->int_val);
            case AstKind::LITERAL_FLOAT:
                return CelValue::make_double(node->float_val);
            case AstKind::LITERAL_STRING:
                return CelValue::make_string_view(node->str_val);
            case AstKind::IDENTIFIER:
            case AstKind::MEMBER_ACCESS: {
                if (node->text == "null" || node->text == "NULL") {
                    return CelValue::make_null();
                }
                if (node->text == "lane") {
                    uint64_t lw = (lane_words && num_lane_words > 0) ? lane_words[0] : 0ULL;
                    return CelValue::make_uint(lw);
                }
                if (node->kind == AstKind::MEMBER_ACCESS && !node->children.empty() &&
                    node->children[0] && node->children[0]->kind == AstKind::IDENTIFIER &&
                    (node->children[0]->text == "edge" || node->children[0]->text == "node" || node->children[0]->text == "self")) {
                    return resolve_attr(node->text);
                }
                return resolve_attr(node->text);
            }
            case AstKind::UNARY_OP: {
                CelValue sub = eval(node->children[0], resolve_attr, lane_words, num_lane_words);
                if (node->text == "!") {
                    int tb = sub.to_tribool();
                    if (tb < 0) return CelValue::make_null();
                    return CelValue::make_bool(tb == 0);
                }
                if (sub.is_null()) return CelValue::make_null();
                if (node->text == "+") return sub;
                if (node->text == "-") {
                    if (sub.kind == CelValue::Kind::Int64) return CelValue::make_int(-sub.int_val);
                    if (sub.kind == CelValue::Kind::Uint64) return CelValue::make_double(-static_cast<double>(sub.uint_val));
                    double dv = 0.0;
                    if (sub.to_double(dv)) return CelValue::make_double(-dv);
                    return CelValue::make_null();
                }
                if (node->text == "~") {
                    int64_t iv = 0;
                    if (sub.to_int64(iv)) return CelValue::make_int(~iv);
                    return CelValue::make_null();
                }
                return CelValue::make_null();
            }
            case AstKind::TERNARY_OP: {
                CelValue c = eval(node->children[0], resolve_attr, lane_words, num_lane_words);
                int tb = c.to_tribool();
                if (tb < 0) return CelValue::make_null();
                return eval(tb ? node->children[1] : node->children[2], resolve_attr, lane_words, num_lane_words);
            }
            case AstKind::BINARY_OP: {
                const std::string& op = node->text;
                if (op == "=") {
                    return eval(node->children[1], resolve_attr, lane_words, num_lane_words);
                }
                if (op == "&&") {
                    int tl = eval(node->children[0], resolve_attr, lane_words, num_lane_words).to_tribool();
                    if (tl == 0) return CelValue::make_bool(false);
                    int tr = eval(node->children[1], resolve_attr, lane_words, num_lane_words).to_tribool();
                    if (tr == 0) return CelValue::make_bool(false);
                    if (tl == 1 && tr == 1) return CelValue::make_bool(true);
                    return CelValue::make_null();
                }
                if (op == "||") {
                    int tl = eval(node->children[0], resolve_attr, lane_words, num_lane_words).to_tribool();
                    if (tl == 1) return CelValue::make_bool(true);
                    int tr = eval(node->children[1], resolve_attr, lane_words, num_lane_words).to_tribool();
                    if (tr == 1) return CelValue::make_bool(true);
                    if (tl == 0 && tr == 0) return CelValue::make_bool(false);
                    return CelValue::make_null();
                }
                if (op == "in") {
                    const auto& rhs = node->children[1];
                    if (!rhs || rhs->kind != AstKind::LIST_LITERAL) return CelValue::make_bool(false);
                    if (rhs->children.empty()) return CelValue::make_bool(false);
                    CelValue lhs = eval(node->children[0], resolve_attr, lane_words, num_lane_words);
                    if (lhs.is_null()) return CelValue::make_null();
                    bool saw_null = false;
                    for (const auto& elem_node : rhs->children) {
                        CelValue elem = eval(elem_node, resolve_attr, lane_words, num_lane_words);
                        CelValue eq = compare_values("==", lhs, elem);
                        if (eq.is_null()) saw_null = true;
                        else if (eq.bool_val) return CelValue::make_bool(true);
                    }
                    return saw_null ? CelValue::make_null() : CelValue::make_bool(false);
                }
                if (op == "==" || op == "!=" || op == "<" || op == "<=" || op == ">" || op == ">=") {
                    CelValue lhs = eval(node->children[0], resolve_attr, lane_words, num_lane_words);
                    if (lhs.is_null()) return CelValue::make_null();
                    CelValue rhs = eval(node->children[1], resolve_attr, lane_words, num_lane_words);
                    if (rhs.is_null()) return CelValue::make_null();
                    return compare_values(op, lhs, rhs);
                }

                CelValue lhs = eval(node->children[0], resolve_attr, lane_words, num_lane_words);
                if (lhs.is_null()) return CelValue::make_null();
                CelValue rhs = eval(node->children[1], resolve_attr, lane_words, num_lane_words);
                if (rhs.is_null()) return CelValue::make_null();

                if (op == "+" && lhs.kind == CelValue::Kind::String && rhs.kind == CelValue::Kind::String) {
                    std::string s(lhs.as_string_view());
                    s.append(rhs.as_string_view());
                    return CelValue::make_string(std::move(s));
                }

                bool both_int = (lhs.kind == CelValue::Kind::Int64 || lhs.kind == CelValue::Kind::Bool) &&
                                (rhs.kind == CelValue::Kind::Int64 || rhs.kind == CelValue::Kind::Bool);
                if (both_int && (op == "+" || op == "-" || op == "*" || op == "%")) {
                    int64_t ia = 0, ib = 0;
                    lhs.to_int64(ia);
                    rhs.to_int64(ib);
                    if (op == "+") return CelValue::make_int(ia + ib);
                    if (op == "-") return CelValue::make_int(ia - ib);
                    if (op == "*") return CelValue::make_int(ia * ib);
                    if (op == "%") return (ib == 0) ? CelValue::make_null() : CelValue::make_int(ia % ib);
                }

                if (op == "+" || op == "-" || op == "*" || op == "/" || op == "%") {
                    double da = 0.0, db = 0.0;
                    if (!lhs.to_double(da) || !rhs.to_double(db)) return CelValue::make_null();
                    if (op == "+") return CelValue::make_double(da + db);
                    if (op == "-") return CelValue::make_double(da - db);
                    if (op == "*") return CelValue::make_double(da * db);
                    if (op == "/") return (db == 0.0) ? CelValue::make_null() : CelValue::make_double(da / db);
                    if (op == "%") return (db == 0.0) ? CelValue::make_null() : CelValue::make_double(std::fmod(da, db));
                }

                int64_t ia = 0, ib = 0;
                if (lhs.to_int64(ia) && rhs.to_int64(ib)) {
                    if (op == "&") return CelValue::make_int(ia & ib);
                    if (op == "|") return CelValue::make_int(ia | ib);
                    if (op == "^") return CelValue::make_int(ia ^ ib);
                    if (op == "<<") return CelValue::make_int((ib < 0 || ib >= 64) ? 0 : static_cast<int64_t>(static_cast<uint64_t>(ia) << ib));
                    if (op == ">>") return CelValue::make_int((ib < 0 || ib >= 64) ? 0 : (ia >> ib));
                    if (op == ">>>") return CelValue::make_int((ib < 0 || ib >= 64) ? 0 : static_cast<int64_t>(static_cast<uint64_t>(ia) >> ib));
                }
                return CelValue::make_null();
            }
            case AstKind::FUNCTION_CALL: {
                const std::string& fn = node->text;
                if ((fn == "is_null" || fn == "isNull") && node->children.size() == 1) {
                    CelValue v = eval(node->children[0], resolve_attr, lane_words, num_lane_words);
                    return CelValue::make_bool(v.is_null());
                }
                if ((fn == "is_not_null" || fn == "isNotNull") && node->children.size() == 1) {
                    CelValue v = eval(node->children[0], resolve_attr, lane_words, num_lane_words);
                    return CelValue::make_bool(!v.is_null());
                }
                if ((fn == "starts_with" || fn == "startsWith") && node->children.size() == 2) {
                    CelValue s = eval(node->children[0], resolve_attr, lane_words, num_lane_words);
                    if (s.is_null()) return CelValue::make_null();
                    CelValue p = eval(node->children[1], resolve_attr, lane_words, num_lane_words);
                    if (p.is_null()) return CelValue::make_null();
                    if (s.kind != CelValue::Kind::String || p.kind != CelValue::Kind::String) {
                        return CelValue::make_null();
                    }
                    std::string_view sv = s.as_string_view();
                    std::string_view pv = p.as_string_view();
                    return CelValue::make_bool(sv.size() >= pv.size() && sv.substr(0, pv.size()) == pv);
                }
                if ((fn == "ends_with" || fn == "endsWith") && node->children.size() == 2) {
                    CelValue s = eval(node->children[0], resolve_attr, lane_words, num_lane_words);
                    if (s.is_null()) return CelValue::make_null();
                    CelValue p = eval(node->children[1], resolve_attr, lane_words, num_lane_words);
                    if (p.is_null()) return CelValue::make_null();
                    if (s.kind != CelValue::Kind::String || p.kind != CelValue::Kind::String) {
                        return CelValue::make_null();
                    }
                    std::string_view sv = s.as_string_view();
                    std::string_view pv = p.as_string_view();
                    return CelValue::make_bool(sv.size() >= pv.size() && sv.substr(sv.size() - pv.size()) == pv);
                }
                if (fn == "contains" && node->children.size() == 2) {
                    CelValue s = eval(node->children[0], resolve_attr, lane_words, num_lane_words);
                    if (s.is_null()) return CelValue::make_null();
                    CelValue p = eval(node->children[1], resolve_attr, lane_words, num_lane_words);
                    if (p.is_null()) return CelValue::make_null();
                    if (s.kind != CelValue::Kind::String || p.kind != CelValue::Kind::String) {
                        return CelValue::make_null();
                    }
                    return CelValue::make_bool(s.as_string_view().find(p.as_string_view()) != std::string_view::npos);
                }

                // Multi-lane intrinsics
                if ((fn == "popcount" || fn == "bit_count") && !node->children.empty()) {
                    if (node->children[0]->text == "lane") {
                        int64_t total = 0;
                        for (size_t w = 0; w < num_lane_words; ++w) total += std::popcount(lane_words[w]);
                        return CelValue::make_int(total);
                    }
                    CelValue v = eval(node->children[0], resolve_attr, lane_words, num_lane_words);
                    int64_t iv = 0;
                    if (!v.to_int64(iv)) return CelValue::make_null();
                    return CelValue::make_int(std::popcount(static_cast<uint64_t>(iv)));
                }
                if ((fn == "first_lane" || fn == "ctz") && !node->children.empty()) {
                    if (node->children[0]->text == "lane") {
                        for (size_t w = 0; w < num_lane_words; ++w) {
                            if (lane_words[w] != 0ULL) {
                                return CelValue::make_int(static_cast<int64_t>(w * 64 + std::countr_zero(lane_words[w])));
                            }
                        }
                        return CelValue::make_int(-1);
                    }
                    CelValue v = eval(node->children[0], resolve_attr, lane_words, num_lane_words);
                    int64_t iv = 0;
                    if (!v.to_int64(iv)) return CelValue::make_null();
                    uint64_t uv = static_cast<uint64_t>(iv);
                    return CelValue::make_int(uv == 0ULL ? -1 : static_cast<int64_t>(std::countr_zero(uv)));
                }
                if (fn == "last_lane" && !node->children.empty()) {
                    if (node->children[0]->text == "lane") {
                        for (size_t w = num_lane_words; w > 0; --w) {
                            if (lane_words[w - 1] != 0ULL) {
                                return CelValue::make_int(static_cast<int64_t>((w - 1) * 64 + 63 - std::countl_zero(lane_words[w - 1])));
                            }
                        }
                        return CelValue::make_int(-1);
                    }
                    CelValue v = eval(node->children[0], resolve_attr, lane_words, num_lane_words);
                    int64_t iv = 0;
                    if (!v.to_int64(iv)) return CelValue::make_null();
                    uint64_t uv = static_cast<uint64_t>(iv);
                    return CelValue::make_int(uv == 0ULL ? -1 : static_cast<int64_t>(63 - std::countl_zero(uv)));
                }
                if (fn == "clz" && !node->children.empty()) {
                    CelValue v = eval(node->children[0], resolve_attr, lane_words, num_lane_words);
                    int64_t iv = 0;
                    if (!v.to_int64(iv)) return CelValue::make_null();
                    uint64_t uv = static_cast<uint64_t>(iv);
                    return CelValue::make_int(uv == 0ULL ? 64 : static_cast<int64_t>(std::countl_zero(uv)));
                }
                if (fn == "has_any" && !node->children.empty()) {
                    if (node->children.size() == 1 && node->children[0]->text == "lane") {
                        for (size_t w = 0; w < num_lane_words; ++w) {
                            if (lane_words[w] != 0ULL) return CelValue::make_bool(true);
                        }
                        return CelValue::make_bool(false);
                    }
                    int64_t m = 0, req = 0;
                    if (node->children[0]->text == "lane") {
                        m = static_cast<int64_t>((lane_words && num_lane_words > 0) ? lane_words[0] : 0ULL);
                    } else if (!eval(node->children[0], resolve_attr, lane_words, num_lane_words).to_int64(m)) {
                        return CelValue::make_null();
                    }
                    if (node->children.size() >= 2) {
                        if (!eval(node->children[1], resolve_attr, lane_words, num_lane_words).to_int64(req)) {
                            return CelValue::make_null();
                        }
                        return CelValue::make_bool((static_cast<uint64_t>(m) & static_cast<uint64_t>(req)) != 0ULL);
                    }
                    return CelValue::make_bool(m != 0);
                }
                if (fn == "has_all" && node->children.size() >= 2) {
                    int64_t m = 0, req = 0;
                    if (node->children[0]->text == "lane") {
                        m = static_cast<int64_t>((lane_words && num_lane_words > 0) ? lane_words[0] : 0ULL);
                    } else if (!eval(node->children[0], resolve_attr, lane_words, num_lane_words).to_int64(m)) {
                        return CelValue::make_null();
                    }
                    if (!eval(node->children[1], resolve_attr, lane_words, num_lane_words).to_int64(req)) {
                        return CelValue::make_null();
                    }
                    return CelValue::make_bool((static_cast<uint64_t>(m) & static_cast<uint64_t>(req)) == static_cast<uint64_t>(req));
                }
                if (fn == "has_none" && node->children.size() >= 2) {
                    int64_t m = 0, forbid = 0;
                    if (node->children[0]->text == "lane") {
                        m = static_cast<int64_t>((lane_words && num_lane_words > 0) ? lane_words[0] : 0ULL);
                    } else if (!eval(node->children[0], resolve_attr, lane_words, num_lane_words).to_int64(m)) {
                        return CelValue::make_null();
                    }
                    if (!eval(node->children[1], resolve_attr, lane_words, num_lane_words).to_int64(forbid)) {
                        return CelValue::make_null();
                    }
                    return CelValue::make_bool((static_cast<uint64_t>(m) & static_cast<uint64_t>(forbid)) == 0ULL);
                }
                if (fn == "has_lane" && node->children.size() >= 2) {
                    int64_t idx = 0;
                    if (!eval(node->children[1], resolve_attr, lane_words, num_lane_words).to_int64(idx)) {
                        return CelValue::make_null();
                    }
                    if (idx < 0) return CelValue::make_bool(false);
                    size_t w = static_cast<size_t>(idx) / 64;
                    size_t b = static_cast<size_t>(idx) % 64;
                    if (lane_words && w < num_lane_words) {
                        return CelValue::make_bool(((lane_words[w] >> b) & 1ULL) != 0ULL);
                    }
                    return CelValue::make_bool(false);
                }

                int func_id = CelCompiler::resolve_math_func(fn);
                if (func_id > 0) {
                    if (node->children.size() == 1) {
                        double a = 0.0;
                        if (!eval(node->children[0], resolve_attr, lane_words, num_lane_words).to_double(a)) {
                            return CelValue::make_null();
                        }
                        return CelValue::make_double(impulse_math_unary_f64(static_cast<uint8_t>(func_id), a));
                    }
                    if (node->children.size() == 2) {
                        double a = 0.0, b = 0.0;
                        if (!eval(node->children[0], resolve_attr, lane_words, num_lane_words).to_double(a) ||
                            !eval(node->children[1], resolve_attr, lane_words, num_lane_words).to_double(b)) {
                            return CelValue::make_null();
                        }
                        return CelValue::make_double(impulse_math_binary_f64(static_cast<uint8_t>(func_id), a, b));
                    }
                    if (node->children.size() == 3) {
                        double a = 0.0, b = 0.0, c = 0.0;
                        if (!eval(node->children[0], resolve_attr, lane_words, num_lane_words).to_double(a) ||
                            !eval(node->children[1], resolve_attr, lane_words, num_lane_words).to_double(b) ||
                            !eval(node->children[2], resolve_attr, lane_words, num_lane_words).to_double(c)) {
                            return CelValue::make_null();
                        }
                        return CelValue::make_double(impulse_math_ternary_f64(static_cast<uint8_t>(func_id), a, b, c));
                    }
                }
                return CelValue::make_null();
            }
            default:
                return CelValue::make_null();
        }
    }

    template <typename ResolveAttrFn>
    static bool eval_filter_bool(
        const std::shared_ptr<AstNode>& node,
        ResolveAttrFn&& resolve_attr,
        const uint64_t* lane_words = nullptr,
        size_t num_lane_words = 0
    ) {
        return eval(node, std::forward<ResolveAttrFn>(resolve_attr), lane_words, num_lane_words).to_tribool() == 1;
    }
};

} // namespace cel
} // namespace impulse

#endif // IMPULSE_CEL_H
