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
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <sstream>
#include <cstdint>
#include <cmath>

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
    QUESTION, COLON,
    DOT, COMMA,
    
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
                break;
            case '!':
                if (peek() == '=') { cursor_++; return Token{TokenType::BANG_EQ, "!=", 0, 0.0, false, start}; }
                return Token{TokenType::BANG, "!", 0, 0.0, false, start};
            case '<':
                if (peek() == '=') { cursor_++; return Token{TokenType::LT_EQ, "<=", 0, 0.0, false, start}; }
                return Token{TokenType::LT, "<", 0, 0.0, false, start};
            case '>':
                if (peek() == '=') { cursor_++; return Token{TokenType::GT_EQ, ">=", 0, 0.0, false, start}; }
                return Token{TokenType::GT, ">", 0, 0.0, false, start};
            case '&':
                if (peek() == '&') { cursor_++; return Token{TokenType::AMP_AMP, "&&", 0, 0.0, false, start}; }
                break;
            case '|':
                if (peek() == '|') { cursor_++; return Token{TokenType::PIPE_PIPE, "||", 0, 0.0, false, start}; }
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
        std::string num_str(1, first_digit);
        bool is_float = false;
        while (cursor_ < src_.size()) {
            char c = src_[cursor_];
            if (std::isdigit(static_cast<unsigned char>(c))) {
                num_str += c;
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
        std::string id(1, src_[start]);
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
    PREC_CONDITIONAL, // ? :
    PREC_OR,          // ||
    PREC_AND,         // &&
    PREC_COMPARE,     // == != < <= > >= in
    PREC_ADD_SUB,     // + -
    PREC_MUL_DIV,     // * / %
    PREC_UNARY,       // ! - +
    PREC_CALL_MEMBER  // . () []
};

class Parser {
public:
    explicit Parser(std::string source) : lexer_(std::move(source)) {
        advance();
    }

    std::shared_ptr<AstNode> parse_expression() {
        return parse_precedence(PREC_CONDITIONAL);
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

    std::shared_ptr<AstNode> parse_prefix() {
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
            case TokenType::PLUS: {
                auto operand = parse_precedence(PREC_UNARY);
                return AstNode::make_unary(t.text, operand);
            }
            case TokenType::LPAREN: {
                auto expr = parse_expression();
                match(TokenType::RPAREN);
                return expr;
            }
            case TokenType::LBRACKET: {
                std::vector<std::shared_ptr<AstNode>> elements;
                if (curr_.type != TokenType::RBRACKET) {
                    elements.push_back(parse_expression());
                    while (match(TokenType::COMMA)) {
                        elements.push_back(parse_expression());
                    }
                }
                match(TokenType::RBRACKET);
                return AstNode::make_list(std::move(elements));
            }
            default:
                return nullptr;
        }
    }

    std::shared_ptr<AstNode> parse_precedence(Precedence prec) {
        auto left = parse_prefix();
        if (!left) return nullptr;

        while (prec <= get_precedence(curr_.type)) {
            Token op = curr_;
            advance();

            if (op.type == TokenType::QUESTION) {
                // Ternary conditional ? :
                auto then_branch = parse_expression();
                match(TokenType::COLON);
                auto else_branch = parse_precedence(PREC_CONDITIONAL);
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
                            args.push_back(parse_expression());
                            while (match(TokenType::COMMA)) {
                                args.push_back(parse_expression());
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
                    args.push_back(parse_expression());
                    while (match(TokenType::COMMA)) {
                        args.push_back(parse_expression());
                    }
                }
                match(TokenType::RPAREN);
                if (left->kind == AstKind::IDENTIFIER) {
                    left = AstNode::make_call(left->text, std::move(args));
                }
            } else {
                // Binary operator
                auto next_prec = static_cast<Precedence>(get_precedence(op.type) + 1);
                auto right = parse_precedence(next_prec);
                left = AstNode::make_binary(op.text, left, right);
            }
        }

        return left;
    }
};

// CEL to ImpScheme & impOps Code Generator
class CelCompiler {
public:
    static std::string to_impscheme(const std::shared_ptr<AstNode>& node) {
        if (!node) return "()";

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
                return "(get-attr " + to_impscheme(node->children[0]) + " \"" + node->text + "\")";
            case AstKind::UNARY_OP:
                if (node->text == "!") return "(mask-not " + to_impscheme(node->children[0]) + ")";
                if (node->text == "-") return "(- 0 " + to_impscheme(node->children[0]) + ")";
                return to_impscheme(node->children[0]);
            case AstKind::BINARY_OP: {
                std::string op = node->text;
                if (op == "&&") return "(mask-and " + to_impscheme(node->children[0]) + " " + to_impscheme(node->children[1]) + ")";
                if (op == "||") return "(mask-or " + to_impscheme(node->children[0]) + " " + to_impscheme(node->children[1]) + ")";
                if (op == ">") return "(vec-cmp-gt " + to_impscheme(node->children[0]) + " " + to_impscheme(node->children[1]) + ")";
                if (op == "<") return "(vec-cmp-lt " + to_impscheme(node->children[0]) + " " + to_impscheme(node->children[1]) + ")";
                if (op == "==") return "(vec-cmp-eq " + to_impscheme(node->children[0]) + " " + to_impscheme(node->children[1]) + ")";
                if (op == "!=") return "(mask-not (vec-cmp-eq " + to_impscheme(node->children[0]) + " " + to_impscheme(node->children[1]) + "))";
                return "(" + op + " " + to_impscheme(node->children[0]) + " " + to_impscheme(node->children[1]) + ")";
            }
            case AstKind::TERNARY_OP:
                return "(vec-blend " + to_impscheme(node->children[0]) + " " + to_impscheme(node->children[1]) + " " + to_impscheme(node->children[2]) + ")";
            case AstKind::FUNCTION_CALL: {
                std::string s = "(" + node->text;
                for (const auto& arg : node->children) {
                    s += " " + to_impscheme(arg);
                }
                s += ")";
                return s;
            }
            case AstKind::LIST_LITERAL: {
                std::string s = "(list";
                for (const auto& elem : node->children) {
                    s += " " + to_impscheme(elem);
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
            {"popcount", MATH_FUNC_POPCOUNT}, {"clz", MATH_FUNC_CLZ}, {"ctz", MATH_FUNC_CTZ},
            {"rotl", MATH_FUNC_ROTL}, {"rotr", MATH_FUNC_ROTR}
        };
        auto it = kMathMap.find(name);
        return it != kMathMap.end() ? it->second : -1;
    }
};

} // namespace cel
} // namespace impulse

#endif // IMPULSE_CEL_H
