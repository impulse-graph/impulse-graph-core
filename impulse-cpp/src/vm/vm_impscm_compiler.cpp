#include "vm/impulse_impscm.hpp"
#include "../impulse_graph_internal.hpp"
#include "vm_internal.hpp"

#undef OP_HALT
#undef OP_NOP
#undef OP_MOV
#undef OP_JMP
#undef OP_SET_CARDINALITY
#undef OP_VEC_BLEND
#undef OP_NODE_FILTER
#undef OP_GATHER_NODE_ATTR
#undef OP_GATHER_EDGE_ATTR

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace impulse::compiler::v11 {

using namespace impulse::vm;

namespace {

constexpr uint8_t kTypeCodeDecimal64 = 0x15u;
constexpr uint8_t kTypeCodeDecimal64Nullable = 0x95u;

void validate_decimal_precision_scale(uint16_t precision, uint8_t scale) {
    if (precision < 1u || precision > 18u) {
        throw std::runtime_error("PARSE_ERROR: DECIMAL precision must be in [1, 18], got " +
                                 std::to_string(precision));
    }
    if (scale > precision) {
        throw std::runtime_error("PARSE_ERROR: DECIMAL scale must be in [0, " +
                                 std::to_string(precision) + "], got " + std::to_string(scale));
    }
}

std::string trim_ascii(const std::string& text) {
    size_t start = 0;
    while (start < text.size() && std::isspace(static_cast<unsigned char>(text[start]))) {
        ++start;
    }
    size_t end = text.size();
    while (end > start && std::isspace(static_cast<unsigned char>(text[end - 1]))) {
        --end;
    }
    return text.substr(start, end - start);
}

FoldedDecimalComparison fold_above_max(DecimalCmpOp op) noexcept {
    switch (op) {
        case DecimalCmpOp::EQ:
        case DecimalCmpOp::GT:
        case DecimalCmpOp::GE:
            return FoldedDecimalComparison::always_false();
        case DecimalCmpOp::NE:
        case DecimalCmpOp::LT:
        case DecimalCmpOp::LE:
            return FoldedDecimalComparison::always_true();
    }
    return FoldedDecimalComparison::always_false();
}

FoldedDecimalComparison fold_below_min(DecimalCmpOp op) noexcept {
    switch (op) {
        case DecimalCmpOp::EQ:
        case DecimalCmpOp::LT:
        case DecimalCmpOp::LE:
            return FoldedDecimalComparison::always_false();
        case DecimalCmpOp::NE:
        case DecimalCmpOp::GT:
        case DecimalCmpOp::GE:
            return FoldedDecimalComparison::always_true();
    }
    return FoldedDecimalComparison::always_false();
}

FoldedDecimalComparison fold_exact_at_col_scale(
    DecimalCmpOp op,
    int64_t t,
    int64_t col_min,
    int64_t col_max
) noexcept {
    if (t > col_max) return fold_above_max(op);
    if (t < col_min) return fold_below_min(op);
    switch (op) {
        case DecimalCmpOp::GT:
            return (t == col_max) ? FoldedDecimalComparison::always_false()
                                  : FoldedDecimalComparison::compare(DecimalCmpOp::GT, t);
        case DecimalCmpOp::LT:
            return (t == col_min) ? FoldedDecimalComparison::always_false()
                                  : FoldedDecimalComparison::compare(DecimalCmpOp::LT, t);
        case DecimalCmpOp::GE:
            return (t == col_min) ? FoldedDecimalComparison::always_true()
                                  : FoldedDecimalComparison::compare(DecimalCmpOp::GE, t);
        case DecimalCmpOp::LE:
            return (t == col_max) ? FoldedDecimalComparison::always_true()
                                  : FoldedDecimalComparison::compare(DecimalCmpOp::LE, t);
        case DecimalCmpOp::EQ:
        case DecimalCmpOp::NE:
            return FoldedDecimalComparison::compare(op, t);
    }
    return FoldedDecimalComparison::compare(op, t);
}

enum class SExprKind { Symbol, Keyword, Param, String, Int, Float, Bool, Null, List };

struct SExpr {
    SExprKind kind = SExprKind::Null;
    std::string text;
    int64_t i_val = 0;
    double f_val = 0.0;
    bool b_val = false;
    std::vector<std::shared_ptr<SExpr>> elems;
};

using SExprPtr = std::shared_ptr<SExpr>;

struct Parser {
    const std::string& src;
    size_t pos = 0;

    void skip_ws() {
        while (pos < src.size()) {
            char c = src[pos];
            if (std::isspace(static_cast<unsigned char>(c))) {
                ++pos;
            } else if (c == ';') {
                while (pos < src.size() && src[pos] != '\n') ++pos;
            } else {
                break;
            }
        }
    }

    SExprPtr parse_one() {
        skip_ws();
        if (pos >= src.size()) throw std::runtime_error("PARSE_ERROR: unexpected EOF");
        char c = src[pos];
        if (c == '(' || c == '[') {
            char close = (c == '(') ? ')' : ']';
            ++pos;
            auto node = std::make_shared<SExpr>();
            node->kind = SExprKind::List;
            while (true) {
                skip_ws();
                if (pos >= src.size()) throw std::runtime_error("PARSE_ERROR: unterminated list");
                if (src[pos] == close) { ++pos; break; }
                node->elems.push_back(parse_one());
            }
            return node;
        }
        if (c == '"') {
            ++pos;
            std::string s;
            while (pos < src.size() && src[pos] != '"') {
                if (src[pos] == '\\' && pos + 1 < src.size()) {
                    ++pos;
                    char esc = src[pos++];
                    if (esc == 'n') s.push_back('\n');
                    else if (esc == 't') s.push_back('\t');
                    else if (esc == 'r') s.push_back('\r');
                    else s.push_back(esc);
                } else {
                    s.push_back(src[pos++]);
                }
            }
            if (pos >= src.size() || src[pos] != '"') throw std::runtime_error("PARSE_ERROR: unterminated string");
            ++pos;
            auto node = std::make_shared<SExpr>();
            node->kind = SExprKind::String;
            node->text = std::move(s);
            return node;
        }
        size_t start = pos;
        while (pos < src.size()) {
            char ch = src[pos];
            if (std::isspace(static_cast<unsigned char>(ch)) || ch == '(' || ch == ')' || ch == '[' || ch == ']' || ch == ';') break;
            ++pos;
        }
        std::string tok = src.substr(start, pos - start);
        auto node = std::make_shared<SExpr>();
        node->text = tok;
        if (tok == "#t") { node->kind = SExprKind::Bool; node->b_val = true; return node; }
        if (tok == "#f") { node->kind = SExprKind::Bool; node->b_val = false; return node; }
        if (tok == "null") { node->kind = SExprKind::Null; return node; }
        if (!tok.empty() && tok[0] == ':') { node->kind = SExprKind::Keyword; return node; }
        if (!tok.empty() && tok[0] == '$') { node->kind = SExprKind::Param; return node; }
        if ((std::isdigit(static_cast<unsigned char>(tok[0])) ||
             (tok.size() > 1 && tok[0] == '-' && std::isdigit(static_cast<unsigned char>(tok[1]))))) {
            if (tok.find('.') != std::string::npos || tok.find('e') != std::string::npos || tok.find('E') != std::string::npos) {
                node->kind = SExprKind::Float;
                node->f_val = std::strtod(tok.c_str(), nullptr);
                return node;
            }
            node->kind = SExprKind::Int;
            node->i_val = std::strtoll(tok.c_str(), nullptr, 10);
            return node;
        }
        node->kind = SExprKind::Symbol;
        return node;
    }
};

enum class ValBank {
    ScalarI64,
    ScalarU64,
    ScalarF64,
    ScalarBool,
    ScalarStr,
    ScalarDecimal,
    Frontier,
    VecI64,
    VecU64,
    VecF64,
    VecBool,
    VecStr,
    VecDecimal,
};

bool is_scalar_bank(ValBank b) noexcept {
    switch (b) {
        case ValBank::ScalarI64:
        case ValBank::ScalarU64:
        case ValBank::ScalarF64:
        case ValBank::ScalarBool:
        case ValBank::ScalarStr:
        case ValBank::ScalarDecimal:
            return true;
        default:
            return false;
    }
}

bool is_vec_bank(ValBank b) noexcept {
    switch (b) {
        case ValBank::VecI64:
        case ValBank::VecU64:
        case ValBank::VecF64:
        case ValBank::VecBool:
        case ValBank::VecStr:
        case ValBank::VecDecimal:
            return true;
        default:
            return false;
    }
}

bool is_decimal_bank(ValBank b) noexcept {
    return b == ValBank::ScalarDecimal || b == ValBank::VecDecimal;
}

bool is_int_bank(ValBank b) noexcept {
    return b == ValBank::ScalarI64 || b == ValBank::ScalarU64 ||
           b == ValBank::VecI64 || b == ValBank::VecU64;
}

bool is_float_bank(ValBank b) noexcept {
    return b == ValBank::ScalarF64 || b == ValBank::VecF64;
}

bool is_str_bank(ValBank b) noexcept {
    return b == ValBank::ScalarStr || b == ValBank::VecStr;
}

bool is_bool_bank(ValBank b) noexcept {
    return b == ValBank::ScalarBool || b == ValBank::VecBool;
}

const char* val_bank_name(ValBank b) noexcept {
    switch (b) {
        case ValBank::ScalarI64: return "SCALAR_I64";
        case ValBank::ScalarU64: return "SCALAR_U64";
        case ValBank::ScalarF64: return "SCALAR_F64";
        case ValBank::ScalarBool: return "SCALAR_BOOL";
        case ValBank::ScalarStr: return "SCALAR_STR";
        case ValBank::ScalarDecimal: return "SCALAR_DECIMAL";
        case ValBank::Frontier: return "FRONTIER";
        case ValBank::VecI64: return "VEC_I64";
        case ValBank::VecU64: return "VEC_U64";
        case ValBank::VecF64: return "VEC_F64";
        case ValBank::VecBool: return "VEC_BOOL";
        case ValBank::VecStr: return "VEC_STR";
        case ValBank::VecDecimal: return "VEC_DECIMAL";
    }
    return "UNKNOWN";
}

struct TypedVal {
    ValBank bank = ValBank::ScalarI64;
    uint8_t reg = 0;
    uint16_t dom_id = 0xFFFFu;
    uint16_t precision = 0;
    uint8_t scale = 0;
    bool is_const = false;
    int64_t const_val = 0;

    static TypedVal scalar(ValBank bank, uint8_t reg) noexcept {
        return {bank, reg, 0xFFFFu, 0, 0, false, 0};
    }
    static TypedVal scalar_with_dom(ValBank bank, uint8_t reg, uint16_t dom_id) noexcept {
        return {bank, reg, dom_id, 0, 0, false, 0};
    }
    static TypedVal const_int(uint8_t reg, int64_t val) noexcept {
        return {ValBank::ScalarI64, reg, 0xFFFFu, 0, 0, true, val};
    }
    static TypedVal decimal(uint8_t reg, uint16_t precision, uint8_t scale) noexcept {
        return {ValBank::ScalarDecimal, reg, 0xFFFFu, precision, scale, false, 0};
    }
    static TypedVal const_decimal(uint8_t reg, uint16_t precision, uint8_t scale, int64_t unscaled) noexcept {
        return {ValBank::ScalarDecimal, reg, 0xFFFFu, precision, scale, true, unscaled};
    }
    static TypedVal frontier(uint8_t reg, uint16_t dom_id) noexcept {
        return {ValBank::Frontier, reg, dom_id, 0, 0, false, 0};
    }
    static TypedVal vec(ValBank bank, uint8_t reg, uint16_t dom_id) noexcept {
        return {bank, reg, dom_id, 0, 0, false, 0};
    }
    static TypedVal vec_decimal(uint8_t reg, uint16_t dom_id, uint16_t precision, uint8_t scale) noexcept {
        return {ValBank::VecDecimal, reg, dom_id, precision, scale, false, 0};
    }

    OutputColumnMeta to_output_meta(const std::string& name, const std::string& collect_kind) const {
        if (is_decimal_bank(bank)) {
            return {name, "DECIMAL", collect_kind, true, precision, scale};
        }
        std::string ty = "UNKNOWN";
        switch (bank) {
            case ValBank::ScalarI64:
            case ValBank::VecI64: ty = "I64"; break;
            case ValBank::ScalarU64:
            case ValBank::VecU64: ty = "U64"; break;
            case ValBank::ScalarF64:
            case ValBank::VecF64: ty = "F64"; break;
            case ValBank::ScalarBool:
            case ValBank::VecBool: ty = "BOOL"; break;
            case ValBank::ScalarStr:
            case ValBank::VecStr: ty = "STR"; break;
            case ValBank::Frontier: ty = "FRONTIER"; break;
            default: break;
        }
        return {name, ty, collect_kind, false, 0, 0};
    }
};

struct RelInfo {
    uint16_t rel_id = 0;
    size_t rel_idx = 0;
    uint16_t src_dom = 0;
    uint16_t tgt_dom = 0;
    std::string name;
};

struct AttrInfo {
    uint16_t attr_id = 0;
    uint8_t type_code = 0;
    uint16_t precision = 0;
    uint8_t scale = 0;

    bool is_decimal() const noexcept {
        return type_code == kTypeCodeDecimal64 || type_code == kTypeCodeDecimal64Nullable;
    }
};

struct TypeSpec {
    ValBank scalar_bank = ValBank::ScalarI64;
    uint16_t precision = 0;
    uint8_t scale = 0;
};

struct MorselScope {
    RelInfo rel;
    std::string src_var;
    std::string tgt_var;
    std::string edge_var;
    int pred_reg = -1;
    uint8_t next_morsel_v = 12; // V8=src, V9=tgt, V10=edge, V11=pred, V12..V15=temps
    V11ProgramBuilder* hoisted_b = nullptr; // Points to outer builder before OP_WALK_MORSEL

    uint8_t alloc_temp_v(uint32_t pinned_v_mask) {
        for (int attempt = 0; attempt < 4; ++attempt) {
            uint8_t r = next_morsel_v++;
            if (next_morsel_v > 15u) next_morsel_v = 12u;
            if ((pinned_v_mask & (1u << r)) != 0u) {
                continue;
            }
            return r;
        }
        throw std::runtime_error("REGISTER_EXHAUSTION: morsel V12..V15");
    }
};

struct PinGuard {
    uint32_t& r_ref;
    uint32_t& v_ref;
    uint32_t saved_r;
    uint32_t saved_v;
    PinGuard(uint32_t& r, uint32_t& v) noexcept : r_ref(r), v_ref(v), saved_r(r), saved_v(v) {}
    ~PinGuard() {
        r_ref = saved_r;
        v_ref = saved_v;
    }
};

bool parse_cmp_op(const std::string& op, DecimalCmpOp& out) noexcept {
    if (op == "=") { out = DecimalCmpOp::EQ; return true; }
    if (op == "!=") { out = DecimalCmpOp::NE; return true; }
    if (op == "<") { out = DecimalCmpOp::LT; return true; }
    if (op == "<=") { out = DecimalCmpOp::LE; return true; }
    if (op == ">") { out = DecimalCmpOp::GT; return true; }
    if (op == ">=") { out = DecimalCmpOp::GE; return true; }
    return false;
}

uint8_t cmp_op_to_node_filter_subop(DecimalCmpOp op) noexcept {
    switch (op) {
        case DecimalCmpOp::EQ: return subop::kNODE_FILTER_EQ;
        case DecimalCmpOp::NE: return subop::kNODE_FILTER_NE;
        case DecimalCmpOp::LT: return subop::kNODE_FILTER_LT;
        case DecimalCmpOp::LE: return subop::kNODE_FILTER_LE;
        case DecimalCmpOp::GT: return subop::kNODE_FILTER_GT;
        case DecimalCmpOp::GE: return subop::kNODE_FILTER_GE;
    }
    return subop::kNODE_FILTER_EQ;
}

uint8_t cmp_op_to_vec_cmp_subop(DecimalCmpOp op) noexcept {
    switch (op) {
        case DecimalCmpOp::EQ: return subop::kVEC_CMP_EQ;
        case DecimalCmpOp::NE: return subop::kVEC_CMP_NE;
        case DecimalCmpOp::LT: return subop::kVEC_CMP_LT;
        case DecimalCmpOp::LE: return subop::kVEC_CMP_LE;
        case DecimalCmpOp::GT: return subop::kVEC_CMP_GT;
        case DecimalCmpOp::GE: return subop::kVEC_CMP_GE;
    }
    return subop::kVEC_CMP_EQ;
}

DecimalCmpOp flip_cmp_op(DecimalCmpOp op) noexcept {
    switch (op) {
        case DecimalCmpOp::LT: return DecimalCmpOp::GT;
        case DecimalCmpOp::LE: return DecimalCmpOp::GE;
        case DecimalCmpOp::GT: return DecimalCmpOp::LT;
        case DecimalCmpOp::GE: return DecimalCmpOp::LE;
        case DecimalCmpOp::EQ:
        case DecimalCmpOp::NE: return op;
    }
    return op;
}

struct CompilerCtx {
    const impulse_snapshot_t* snap = nullptr;
    bool opt = true;
    V11ProgramBuilder b;
    uint8_t next_r = 0;
    uint8_t next_f = 0;
    uint8_t next_v = 0;
    uint32_t next_param_id = 0;
    uint16_t output_slot = 0;
    uint32_t pinned_r_mask = 0;
    uint32_t pinned_v_mask = 0;

    std::unordered_map<std::string, TypedVal> env;
    std::unordered_map<std::string, uint32_t> param_map;
    std::vector<ParamMeta> param_schema;
    std::vector<std::string> output_names;
    std::vector<OutputColumnMeta> output_schema;
    std::unique_ptr<MorselScope> morsel;

    V11ProgramBuilder& const_builder() noexcept {
        return (morsel && morsel->hoisted_b) ? *morsel->hoisted_b : b;
    }

    void emit_const_i64(uint8_t dst, int64_t val) {
        if (morsel && morsel->hoisted_b) {
            pinned_r_mask |= (1u << dst);
            morsel->hoisted_b->emit_load_const_i64(dst, val);
        } else {
            b.emit_load_const_i64(dst, val);
        }
    }

    void emit_const_f64(uint8_t dst, double val) {
        if (morsel && morsel->hoisted_b) {
            pinned_r_mask |= (1u << dst);
            morsel->hoisted_b->emit_load_const_f64(dst, val);
        } else {
            b.emit_load_const_f64(dst, val);
        }
    }

    void emit_const_null(uint8_t dst) {
        if (morsel && morsel->hoisted_b) {
            pinned_r_mask |= (1u << dst);
            morsel->hoisted_b->emit(Opcode::OP_LOAD_CONST, subop::kLOAD_CONST_NULL, 0, dst, 0, 0, 0, 0);
        } else {
            b.emit(Opcode::OP_LOAD_CONST, subop::kLOAD_CONST_NULL, 0, dst, 0, 0, 0, 0);
        }
    }

    uint32_t live_r_mask() const noexcept {
        uint32_t mask = pinned_r_mask;
        for (const auto& kv : env) {
            if (is_scalar_bank(kv.second.bank) && kv.second.reg < 16u) {
                mask |= (1u << kv.second.reg);
            }
        }
        return mask;
    }

    uint32_t live_v_mask() const noexcept {
        uint32_t mask = pinned_v_mask;
        for (const auto& kv : env) {
            if (is_vec_bank(kv.second.bank) && kv.second.reg < 8u) {
                mask |= (1u << kv.second.reg);
            }
        }
        return mask;
    }

    uint8_t alloc_r() {
        if (next_r < 16u) return next_r++;
        uint32_t live = live_r_mask();
        for (uint8_t r = 0; r < 16u; ++r) {
            if ((live & (1u << r)) == 0u) return r;
        }
        throw std::runtime_error("REGISTER_EXHAUSTION: R0..R15");
    }

    uint8_t alloc_f() {
        if (next_f >= 16u) throw std::runtime_error("REGISTER_EXHAUSTION: F0..F15");
        return next_f++;
    }

    uint8_t alloc_v() {
        if (morsel) return morsel->alloc_temp_v(pinned_v_mask);
        if (next_v < 8u) return next_v++;
        uint32_t live = live_v_mask();
        for (uint8_t v = 0; v < 8u; ++v) {
            if ((live & (1u << v)) == 0u) return v;
        }
        throw std::runtime_error("REGISTER_EXHAUSTION: V0..V7");
    }

    void pin_r(const TypedVal& v) noexcept {
        if (is_scalar_bank(v.bank) && v.reg < 16u) {
            pinned_r_mask |= (1u << v.reg);
        }
    }

    void pin_v(const TypedVal& v) noexcept {
        if (is_vec_bank(v.bank) && v.reg < 16u) {
            pinned_v_mask |= (1u << v.reg);
        }
    }

    uint8_t alloc_pinned_r() {
        uint8_t r = alloc_r();
        pinned_r_mask |= (1u << r);
        return r;
    }

    uint8_t alloc_pinned_v() {
        uint8_t v = alloc_v();
        pinned_v_mask |= (1u << v);
        return v;
    }

    bool is_vec_in_env(uint8_t reg) const noexcept {
        for (const auto& kv : env) {
            if (is_vec_bank(kv.second.bank) && kv.second.reg == reg) return true;
        }
        return false;
    }

    uint8_t alloc_dst_v(const TypedVal* t1, const TypedVal* t2) {
        if (morsel) {
            for (int attempt = 0; attempt < 4; ++attempt) {
                uint8_t r = morsel->next_morsel_v++;
                if (morsel->next_morsel_v > 15u) morsel->next_morsel_v = 12u;
                if ((pinned_v_mask & (1u << r)) != 0u) {
                    continue;
                }
                return r;
            }
            if (t1 && is_vec_bank(t1->bank) && t1->reg >= 12u && t1->reg <= 15u && !is_vec_in_env(t1->reg)) {
                return t1->reg;
            }
            if (t2 && is_vec_bank(t2->bank) && t2->reg >= 12u && t2->reg <= 15u && !is_vec_in_env(t2->reg)) {
                return t2->reg;
            }
            throw std::runtime_error("REGISTER_EXHAUSTION: morsel V12..V15");
        }
        if (next_v < 8u) return next_v++;
        uint32_t live = live_v_mask();
        for (uint8_t v = 0; v < 8u; ++v) {
            if ((live & (1u << v)) == 0u) return v;
        }
        if (t1 && is_vec_bank(t1->bank) && t1->reg < 8u && !is_vec_in_env(t1->reg)) return t1->reg;
        if (t2 && is_vec_bank(t2->bank) && t2->reg < 8u && !is_vec_in_env(t2->reg)) return t2->reg;
        throw std::runtime_error("REGISTER_EXHAUSTION: V0..V7");
    }

    uint16_t resolve_domain(const std::string& name) const {
        if (!snap) return 0u;
        for (size_t i = 0; i < snap->domain_names.size() && i < snap->domains.size(); ++i) {
            if (snap->domain_names[i] == name) return snap->domains[i].domain_id;
        }
        throw std::runtime_error("UNKNOWN_SCHEMA_ENTITY: unknown domain '" + name + "'");
    }

    RelInfo resolve_rel(const std::string& name) const {
        if (!snap) return {0u, 0u, 0u, 0u, name};
        for (size_t i = 0; i < snap->relation_names.size() && i < snap->relations.size(); ++i) {
            if (snap->relation_names[i] == name) {
                const auto& r = snap->relations[i];
                return {r.relation_id, i, r.src_domain_id, r.tgt_domain_id, name};
            }
        }
        for (size_t i = 0; i < snap->relations.size(); ++i) {
            auto it = snap->metadata.find("relation." + std::to_string(i) + ".name");
            if (it != snap->metadata.end() && it->second == name) {
                const auto& r = snap->relations[i];
                return {r.relation_id, i, r.src_domain_id, r.tgt_domain_id, name};
            }
        }
        throw std::runtime_error("UNKNOWN_SCHEMA_ENTITY: unknown relation '" + name + "'");
    }

    AttrInfo resolve_node_attr(uint16_t dom_id, const std::string& attr_name) const {
        if (snap) {
            for (size_t di = 0; di < snap->domains.size(); ++di) {
                if (snap->domains[di].domain_id == dom_id) {
                    if (di < snap->domain_attribute_names.size() && di < snap->domain_attributes.size()) {
                        const auto& names = snap->domain_attribute_names[di];
                        const auto& attrs = snap->domain_attributes[di];
                        for (size_t ai = 0; ai < names.size() && ai < attrs.size(); ++ai) {
                            if (names[ai] == attr_name) {
                                const auto& desc = attrs[ai];
                                return {
                                    static_cast<uint16_t>(ai),
                                    desc.type_code,
                                    desc.reserved2,
                                    desc.reserved1
                                };
                            }
                        }
                    }
                    break;
                }
            }
        }
        throw std::runtime_error("UNKNOWN_SCHEMA_ENTITY: unknown node attribute '" + attr_name +
                                 "' on domain " + std::to_string(dom_id));
    }

    AttrInfo resolve_edge_attr(const RelInfo& rel, const std::string& attr_name) const {
        if (snap && rel.rel_idx < snap->relation_attribute_names.size() &&
            rel.rel_idx < snap->relation_attributes.size()) {
            const auto& names = snap->relation_attribute_names[rel.rel_idx];
            const auto& attrs = snap->relation_attributes[rel.rel_idx];
            for (size_t ai = 0; ai < names.size() && ai < attrs.size(); ++ai) {
                if (names[ai] == attr_name) {
                    const auto& desc = attrs[ai];
                    return {
                        static_cast<uint16_t>(ai),
                        desc.type_code,
                        desc.reserved2,
                        desc.reserved1
                    };
                }
            }
        }
        throw std::runtime_error("UNKNOWN_SCHEMA_ENTITY: unknown edge attribute '" + attr_name +
                                 "' on relation '" + rel.name + "'");
    }

    static ValBank attr_type_to_vec_bank(const AttrInfo& ai) noexcept {
        if (ai.is_decimal()) return ValBank::VecDecimal;
        uint8_t base = static_cast<uint8_t>(ai.type_code & 0x7Fu);
        switch (base) {
            case 0x05u:
            case 0x06u: return ValBank::VecF64;
            case 0x09u:
            case 0x0Fu: return ValBank::VecBool;
            case 0x0Au: return ValBank::VecStr;
            case 0x07u:
            case 0x08u: return ValBank::VecU64;
            default: return ValBank::VecI64;
        }
    }

    static TypeSpec parse_scalar_type_spec(const SExprPtr& ty) {
        if (ty->kind == SExprKind::List && ty->elems.size() == 3 &&
            ty->elems[0]->kind == SExprKind::Symbol && ty->elems[0]->text == "decimal") {
            if (ty->elems[1]->kind != SExprKind::Int || ty->elems[2]->kind != SExprKind::Int) {
                throw std::runtime_error("PARSE_ERROR: (decimal P S) requires integer P and S");
            }
            int64_t p_raw = ty->elems[1]->i_val;
            int64_t s_raw = ty->elems[2]->i_val;
            if (p_raw < 1 || p_raw > 18 || s_raw < 0 || s_raw > p_raw) {
                throw std::runtime_error("PARSE_ERROR: invalid DECIMAL(" + std::to_string(p_raw) + ", " +
                                         std::to_string(s_raw) + ")");
            }
            uint16_t p = static_cast<uint16_t>(p_raw);
            uint8_t s = static_cast<uint8_t>(s_raw);
            validate_decimal_precision_scale(p, s);
            return {ValBank::ScalarDecimal, p, s};
        }
        if (ty->kind == SExprKind::Symbol) {
            if (ty->text == "i64" || ty->text == "i32") return {ValBank::ScalarI64, 0, 0};
            if (ty->text == "u64" || ty->text == "u32") return {ValBank::ScalarU64, 0, 0};
            if (ty->text == "f64" || ty->text == "f32") return {ValBank::ScalarF64, 0, 0};
            if (ty->text == "bool") return {ValBank::ScalarBool, 0, 0};
            if (ty->text == "str") return {ValBank::ScalarStr, 0, 0};
            throw std::runtime_error("PARSE_ERROR: unknown type '" + ty->text + "'");
        }
        throw std::runtime_error("PARSE_ERROR: unsupported type expression");
    }

    TypedVal ensure_decimal(const TypedVal& v, const std::string& op_name) const {
        if (is_decimal_bank(v.bank)) return v;
        if (is_int_bank(v.bank)) {
            // Exact implicit promotion: INT -> DECIMAL(18, 0)
            if (is_scalar_bank(v.bank)) {
                return v.is_const ? TypedVal::const_decimal(v.reg, 18, 0, v.const_val)
                                  : TypedVal::decimal(v.reg, 18, 0);
            }
            return TypedVal::vec_decimal(v.reg, v.dom_id, 18, 0);
        }
        throw std::runtime_error("TYPE_MISMATCH: decimal operator '" + op_name +
                                 "' does not allow operand of type " + val_bank_name(v.bank) +
                                 "; use (cast ...)");
    }

    TypedVal rescale_decimal_up(const TypedVal& v, uint8_t target_scale) {
        if (target_scale <= v.scale) return v;
        uint8_t delta = static_cast<uint8_t>(target_scale - v.scale);
        uint16_t new_p = static_cast<uint16_t>(std::min<int>(18, static_cast<int>(v.precision) + delta));
        if (is_scalar_bank(v.bank)) {
            uint32_t saved_pin = pinned_r_mask;
            pin_r(v);
            uint8_t dst = alloc_r();
            pinned_r_mask = saved_pin;
            b.emit_u16(Opcode::OP_SCALAR_DECIMAL_ALU, subop::kSCALAR_DECIMAL_ALU_RESCALE, 0,
                       dst, v.reg, 0, 0, delta, 0u);
            return TypedVal::decimal(dst, new_p, target_scale);
        }
        uint8_t dst = alloc_v();
        b.emit_u16(Opcode::OP_VEC_DECIMAL_ALU, subop::kVEC_DECIMAL_ALU_RESCALE, 0,
                   dst, v.reg, 0, 0, delta, 0u);
        return TypedVal::vec_decimal(dst, v.dom_id, new_p, target_scale);
    }

    TypedVal eval_decimal_binary(const std::string& head, const TypedVal& raw_lhs, const TypedVal& raw_rhs) {
        PinGuard pg(pinned_r_mask, pinned_v_mask);
        pin_r(raw_lhs);
        pin_v(raw_lhs);
        pin_r(raw_rhs);
        pin_v(raw_rhs);

        TypedVal lhs = ensure_decimal(raw_lhs, head);
        TypedVal rhs = ensure_decimal(raw_rhs, head);

        bool l_vec = is_vec_bank(lhs.bank);
        bool r_vec = is_vec_bank(rhs.bank);
        if (l_vec && r_vec && lhs.dom_id != rhs.dom_id) {
            throw std::runtime_error("DOMAIN_MISMATCH: vector decimal op domain mismatch");
        }
        uint16_t out_dom = l_vec ? lhs.dom_id : (r_vec ? rhs.dom_id : 0xFFFFu);

        auto unpin_if_dead_v = [&](uint8_t reg, uint8_t keep1, uint8_t keep2) noexcept {
            if (reg < 16u && reg != keep1 && reg != keep2 &&
                (pg.saved_v & (1u << reg)) == 0u && !is_vec_in_env(reg)) {
                pinned_v_mask &= ~(1u << reg);
            }
        };

        if (head == "+" || head == "-") {
            uint8_t s = std::max(lhs.scale, rhs.scale);
            int int_digits = std::max(static_cast<int>(lhs.precision) - lhs.scale,
                                      static_cast<int>(rhs.precision) - rhs.scale);
            uint16_t p = static_cast<uint16_t>(std::min(18, int_digits + static_cast<int>(s) + 1));
            TypedVal a = rescale_decimal_up(lhs, s);
            if (l_vec && a.reg != lhs.reg) {
                unpin_if_dead_v(lhs.reg, a.reg, r_vec ? rhs.reg : 0xFFu);
            }
            pin_r(a);
            pin_v(a);
            TypedVal bv = rescale_decimal_up(rhs, s);
            if (r_vec && bv.reg != rhs.reg) {
                unpin_if_dead_v(rhs.reg, bv.reg, l_vec ? a.reg : 0xFFu);
            }
            pin_r(bv);
            pin_v(bv);

            if (!l_vec && !r_vec) {
                uint8_t dst = alloc_r();
                uint8_t sub = (head == "+") ? subop::kSCALAR_DECIMAL_ALU_ADD_CHK
                                            : subop::kSCALAR_DECIMAL_ALU_SUB_CHK;
                b.emit(Opcode::OP_SCALAR_DECIMAL_ALU, sub, 0, dst, a.reg, bv.reg, 0, 0);
                return TypedVal::decimal(dst, p, s);
            }
            if (l_vec && r_vec) {
                uint8_t dst = alloc_dst_v(&a, &bv);
                uint8_t sub = (head == "+") ? subop::kVEC_DECIMAL_ALU_ADD_CHK
                                            : subop::kVEC_DECIMAL_ALU_SUB_CHK;
                b.emit(Opcode::OP_VEC_DECIMAL_ALU, sub, 0, dst, a.reg, bv.reg, 0, 0);
                return TypedVal::vec_decimal(dst, out_dom, p, s);
            }
            if (l_vec) {
                uint8_t dst = alloc_dst_v(&a, nullptr);
                uint8_t sub = (head == "+") ? subop::kVEC_DECIMAL_ALU_ADD_CHK
                                            : subop::kVEC_DECIMAL_ALU_SUB_CHK;
                b.emit(Opcode::OP_VEC_DECIMAL_ALU, sub, FLAG_SCALAR_RHS, dst, a.reg, bv.reg, 0, 0);
                return TypedVal::vec_decimal(dst, out_dom, p, s);
            }
            // scalar +/- vec
            if (head == "+") {
                uint8_t dst = alloc_dst_v(&bv, nullptr);
                b.emit(Opcode::OP_VEC_DECIMAL_ALU, subop::kVEC_DECIMAL_ALU_ADD_CHK,
                       FLAG_SCALAR_RHS, dst, bv.reg, a.reg, 0, 0);
                return TypedVal::vec_decimal(dst, out_dom, p, s);
            }
            // scalar - vec => (-vec) + scalar
            uint8_t neg_v = alloc_pinned_v();
            b.emit(Opcode::OP_VEC_DECIMAL_ALU, subop::kVEC_DECIMAL_ALU_NEG_CHK, 0, neg_v, bv.reg, 0, 0, 0);
            uint8_t dst = alloc_dst_v(&bv, nullptr);
            b.emit(Opcode::OP_VEC_DECIMAL_ALU, subop::kVEC_DECIMAL_ALU_ADD_CHK,
                   FLAG_SCALAR_RHS, dst, neg_v, a.reg, 0, 0);
            return TypedVal::vec_decimal(dst, out_dom, p, s);
        }

        if (head == "*") {
            int raw_s = static_cast<int>(lhs.scale) + static_cast<int>(rhs.scale);
            uint8_t s = static_cast<uint8_t>(std::min(18, raw_s));
            uint16_t p = static_cast<uint16_t>(std::min(18, static_cast<int>(lhs.precision) + rhs.precision));
            int div_pow = raw_s - static_cast<int>(s);

            if (!l_vec && !r_vec) {
                uint8_t dst = alloc_pinned_r();
                if (div_pow == 0) {
                    b.emit(Opcode::OP_SCALAR_DECIMAL_ALU, subop::kSCALAR_DECIMAL_ALU_MUL_CHK,
                           0, dst, lhs.reg, rhs.reg, 0, 0);
                } else {
                    uint8_t r_div = alloc_r();
                    b.emit_load_const_i64(r_div, kDecimalPow10[static_cast<size_t>(div_pow)]);
                    b.emit(Opcode::OP_SCALAR_DECIMAL_ALU, subop::kSCALAR_DECIMAL_ALU_MUL_DIV_ROUND,
                           0, dst, lhs.reg, rhs.reg, r_div, 0);
                }
                return TypedVal::decimal(dst, p, s);
            }
            const TypedVal& v_op = l_vec ? lhs : rhs;
            const TypedVal& other = l_vec ? rhs : lhs;
            bool scalar_rhs = !is_vec_bank(other.bank);
            uint8_t flags = scalar_rhs ? FLAG_SCALAR_RHS : 0u;
            uint8_t dst = alloc_dst_v(&v_op, scalar_rhs ? nullptr : &other);
            if (div_pow == 0) {
                b.emit(Opcode::OP_VEC_DECIMAL_ALU, subop::kVEC_DECIMAL_ALU_MUL_CHK,
                       flags, dst, v_op.reg, other.reg, 0, 0);
            } else {
                uint8_t r_div = alloc_r();
                emit_const_i64(r_div, kDecimalPow10[static_cast<size_t>(div_pow)]);
                b.emit(Opcode::OP_VEC_DECIMAL_ALU, subop::kVEC_DECIMAL_ALU_MUL_DIV_R,
                       flags, dst, v_op.reg, other.reg, r_div, 0);
            }
            return TypedVal::vec_decimal(dst, out_dom, p, s);
        }

        // "/" or "safe-div"
        bool is_safe = (head == "safe-div");
        uint8_t s = static_cast<uint8_t>(std::min(18, std::max<int>(6, lhs.scale)));
        uint16_t p = 18u;
        int raw_k = static_cast<int>(s) - static_cast<int>(lhs.scale) + static_cast<int>(rhs.scale);

        if (!l_vec && !r_vec) {
            uint8_t dst = alloc_pinned_r();
            if (!is_safe) {
                TypedVal num = lhs;
                int k = raw_k;
                if (k > 18) {
                    num = rescale_decimal_up(lhs, static_cast<uint8_t>(lhs.scale + (k - 18)));
                    pin_r(num);
                    k = 18;
                }
                uint8_t r_mul = alloc_r();
                b.emit_load_const_i64(r_mul, kDecimalPow10[static_cast<size_t>(k)]);
                b.emit(Opcode::OP_SCALAR_DECIMAL_ALU, subop::kSCALAR_DECIMAL_ALU_MUL_DIV_ROUND,
                       0, dst, num.reg, r_mul, rhs.reg, 0);
            } else if (raw_k == 0) {
                b.emit(Opcode::OP_SCALAR_DECIMAL_ALU, subop::kSCALAR_DECIMAL_ALU_DIV_SAFE,
                       0, dst, lhs.reg, rhs.reg, 0, 0);
            } else {
                // Safe-div with 128-bit scaling: check rhs IS NULL or rhs == 0 BEFORE any k > 18 pre-rescale!
                uint8_t r_zero = alloc_pinned_r();
                uint8_t r_is_bad = alloc_pinned_r();
                b.emit(Opcode::OP_SCALAR_3VL, subop::kSCALAR_3VL_IS_NULL, 0,
                       r_is_bad, rhs.reg, 0, 0, 0);
                size_t jcc_null_pc = b.emit(Opcode::OP_JCC, 6u, 0, 0, r_is_bad, 0, 0, 0);
                b.emit_load_const_i64(r_zero, 0LL);
                b.emit(Opcode::OP_SCALAR_3VL, subop::kSCALAR_3VL_CMP_EQ_I64, 0,
                       r_is_bad, rhs.reg, r_zero, 0, 0);
                size_t jcc_zero_pc = b.emit(Opcode::OP_JCC, 6u, 0, 0, r_is_bad, 0, 0, 0);
                TypedVal num = lhs;
                int k = raw_k;
                if (k > 18) {
                    num = rescale_decimal_up(lhs, static_cast<uint8_t>(lhs.scale + (k - 18)));
                    pin_r(num);
                    k = 18;
                }
                uint8_t r_mul = alloc_r();
                b.emit_load_const_i64(r_mul, kDecimalPow10[static_cast<size_t>(k)]);
                b.emit(Opcode::OP_SCALAR_DECIMAL_ALU, subop::kSCALAR_DECIMAL_ALU_MUL_DIV_ROUND,
                       0, dst, num.reg, r_mul, rhs.reg, 0);
                size_t jmp_end_pc = b.emit(Opcode::OP_JMP, 0, 0, 0, 0, 0, 0, 0);
                size_t null_pc = b.pc();
                b.patch_imm32(jcc_null_pc, static_cast<uint32_t>(null_pc - jcc_null_pc));
                b.patch_imm32(jcc_zero_pc, static_cast<uint32_t>(null_pc - jcc_zero_pc));
                b.emit(Opcode::OP_LOAD_CONST, subop::kLOAD_CONST_NULL, 0, dst, 0, 0, 0, 0);
                size_t end_pc = b.pc();
                b.patch_imm32(jmp_end_pc, static_cast<uint32_t>(end_pc - jmp_end_pc));
            }
            return TypedVal::decimal(dst, p, s);
        }

        TypedVal num = lhs;
        if (!l_vec) {
            uint8_t v_num = alloc_pinned_v();
            b.emit_u16(Opcode::OP_VEC_INIT, subop::kVEC_INIT_DOM_I64, FLAG_SCALAR_RHS,
                       v_num, 0, num.reg, 0, out_dom, 0);
            num = TypedVal::vec_decimal(v_num, out_dom, num.precision, num.scale);
        }

        if (!r_vec) {
            int k = raw_k;
            if (is_safe && k > 18) {
                uint8_t dst = alloc_pinned_v();
                uint8_t r_zero = alloc_pinned_r();
                uint8_t r_is_bad = alloc_pinned_r();
                b.emit(Opcode::OP_SCALAR_3VL, subop::kSCALAR_3VL_IS_NULL, 0,
                       r_is_bad, rhs.reg, 0, 0, 0);
                size_t jcc_null_pc = b.emit(Opcode::OP_JCC, 6u, 0, 0, r_is_bad, 0, 0, 0);
                b.emit_load_const_i64(r_zero, 0LL);
                b.emit(Opcode::OP_SCALAR_3VL, subop::kSCALAR_3VL_CMP_EQ_I64, 0,
                       r_is_bad, rhs.reg, r_zero, 0, 0);
                size_t jcc_zero_pc = b.emit(Opcode::OP_JCC, 6u, 0, 0, r_is_bad, 0, 0, 0);
                TypedVal pre_num = rescale_decimal_up(num, static_cast<uint8_t>(num.scale + (k - 18)));
                pin_v(pre_num);
                uint8_t r_mul = alloc_r();
                b.emit_load_const_i64(r_mul, kDecimalPow10[18]);
                b.emit(Opcode::OP_VEC_DECIMAL_ALU, subop::kVEC_DECIMAL_ALU_MUL_DIV_SAFE_R,
                       FLAG_SCALAR_RHS, dst, pre_num.reg, r_mul, rhs.reg, 0);
                size_t jmp_end_pc = b.emit(Opcode::OP_JMP, 0, 0, 0, 0, 0, 0, 0);
                size_t null_pc = b.pc();
                b.patch_imm32(jcc_null_pc, static_cast<uint32_t>(null_pc - jcc_null_pc));
                b.patch_imm32(jcc_zero_pc, static_cast<uint32_t>(null_pc - jcc_zero_pc));
                uint8_t r_null = alloc_r();
                b.emit(Opcode::OP_LOAD_CONST, subop::kLOAD_CONST_NULL, 0, r_null, 0, 0, 0, 0);
                b.emit_u16(Opcode::OP_VEC_INIT, subop::kVEC_INIT_DOM_I64, FLAG_SCALAR_RHS,
                           dst, 0, r_null, 0, out_dom, 0);
                size_t end_pc = b.pc();
                b.patch_imm32(jmp_end_pc, static_cast<uint32_t>(end_pc - jmp_end_pc));
                return TypedVal::vec_decimal(dst, out_dom, p, s);
            }
            if (k > 18) {
                TypedVal scaled_num = rescale_decimal_up(num, static_cast<uint8_t>(num.scale + (k - 18)));
                if (scaled_num.reg != num.reg) unpin_if_dead_v(num.reg, scaled_num.reg, 0xFFu);
                num = scaled_num;
                pin_v(num);
                k = 18;
            }
            uint8_t dst = alloc_dst_v(&num, nullptr);
            uint8_t r_mul = alloc_r();
            emit_const_i64(r_mul, kDecimalPow10[static_cast<size_t>(k)]);
            uint8_t sub = is_safe ? subop::kVEC_DECIMAL_ALU_MUL_DIV_SAFE_R
                                  : subop::kVEC_DECIMAL_ALU_MUL_DIV_R;
            b.emit(Opcode::OP_VEC_DECIMAL_ALU, sub, FLAG_SCALAR_RHS, dst, num.reg, r_mul, rhs.reg, 0);
            return TypedVal::vec_decimal(dst, out_dom, p, s);
        }

        // vec / vec
        if (is_safe) {
            uint8_t r_zero = alloc_pinned_r();
            uint8_t r_null = alloc_pinned_r();
            emit_const_i64(r_zero, 0LL);
            emit_const_null(r_null);
            uint8_t v_non_zero = alloc_pinned_v();
            b.emit(Opcode::OP_VEC_CMP, subop::kVEC_CMP_NE, FLAG_SCALAR_RHS,
                   v_non_zero, rhs.reg, r_zero, 0, 0);
            uint8_t v_safe_rhs = alloc_pinned_v();
            b.emit(Opcode::OP_VEC_BLEND, subop::kVEC_BLEND_KLEENE_VR, 0,
                   v_safe_rhs, v_non_zero, rhs.reg, r_null, 0);
            unpin_if_dead_v(rhs.reg, v_safe_rhs, num.reg);
            int k = raw_k;
            if (k > 18) {
                uint8_t v_safe_num = alloc_pinned_v();
                b.emit(Opcode::OP_VEC_BLEND, subop::kVEC_BLEND_KLEENE_VR, 0,
                       v_safe_num, v_non_zero, num.reg, r_null, 0);
                unpin_if_dead_v(v_non_zero, v_safe_rhs, v_safe_num);
                unpin_if_dead_v(num.reg, v_safe_rhs, v_safe_num);
                num = rescale_decimal_up(
                    TypedVal::vec_decimal(v_safe_num, out_dom, num.precision, num.scale),
                    static_cast<uint8_t>(num.scale + (k - 18)));
                if (num.reg != v_safe_num) unpin_if_dead_v(v_safe_num, v_safe_rhs, num.reg);
                pin_v(num);
                k = 18;
            } else {
                unpin_if_dead_v(v_non_zero, v_safe_rhs, num.reg);
            }
            uint8_t r_mul = alloc_r();
            emit_const_i64(r_mul, kDecimalPow10[static_cast<size_t>(k)]);
            TypedVal safe_rhs_tv = TypedVal::vec_decimal(v_safe_rhs, out_dom, rhs.precision, rhs.scale);
            uint8_t dst = alloc_dst_v(&num, &safe_rhs_tv);
            b.emit(Opcode::OP_VEC_DECIMAL_ALU, subop::kVEC_DECIMAL_ALU_MUL_DIV_V,
                   FLAG_SCALAR_RHS, dst, num.reg, r_mul, v_safe_rhs, 0);
            return TypedVal::vec_decimal(dst, out_dom, p, s);
        }

        int k = raw_k;
        if (k > 18) {
            TypedVal scaled_num = rescale_decimal_up(num, static_cast<uint8_t>(num.scale + (k - 18)));
            if (scaled_num.reg != num.reg) unpin_if_dead_v(num.reg, scaled_num.reg, rhs.reg);
            num = scaled_num;
            pin_v(num);
            k = 18;
        }
        uint8_t dst = alloc_dst_v(&num, &rhs);
        uint8_t r_mul = alloc_r();
        emit_const_i64(r_mul, kDecimalPow10[static_cast<size_t>(k)]);
        b.emit(Opcode::OP_VEC_DECIMAL_ALU, subop::kVEC_DECIMAL_ALU_MUL_DIV_V,
               FLAG_SCALAR_RHS, dst, num.reg, r_mul, rhs.reg, 0);
        return TypedVal::vec_decimal(dst, out_dom, p, s);
    }

    TypedVal eval_cast(const TypedVal& v, const TypeSpec& target) {
        PinGuard pg(pinned_r_mask, pinned_v_mask);
        pin_r(v);
        pin_v(v);
        bool is_vec = is_vec_bank(v.bank);

        if (target.scalar_bank == ValBank::ScalarDecimal) {
            uint16_t tp = target.precision;
            uint8_t ts = target.scale;
            if (is_decimal_bank(v.bank)) {
                if (ts > v.scale) {
                    uint8_t delta = static_cast<uint8_t>(ts - v.scale);
                    if (!is_vec) {
                        uint8_t r1 = alloc_r();
                        b.emit_u16(Opcode::OP_SCALAR_DECIMAL_ALU, subop::kSCALAR_DECIMAL_ALU_RESCALE, 0,
                                   r1, v.reg, 0, 0, delta, 0u);
                        uint8_t r2 = alloc_r();
                        b.emit_u16(Opcode::OP_SCALAR_DECIMAL_ALU, subop::kSCALAR_DECIMAL_ALU_BOUND_CHK, 0,
                                   r2, r1, 0, 0, tp, 0u);
                        return TypedVal::decimal(r2, tp, ts);
                    }
                    uint8_t v1 = alloc_v();
                    b.emit_u16(Opcode::OP_VEC_DECIMAL_ALU, subop::kVEC_DECIMAL_ALU_RESCALE, 0,
                               v1, v.reg, 0, 0, delta, 0u);
                    uint8_t v2 = alloc_v();
                    b.emit_u16(Opcode::OP_VEC_DECIMAL_ALU, subop::kVEC_DECIMAL_ALU_BOUND_CHK, 0,
                               v2, v1, 0, 0, tp, 0u);
                    return TypedVal::vec_decimal(v2, v.dom_id, tp, ts);
                } else if (ts < v.scale) {
                    uint8_t delta = static_cast<uint8_t>(v.scale - ts);
                    if (!is_vec) {
                        uint8_t r1 = alloc_r();
                        b.emit_u16(Opcode::OP_SCALAR_DECIMAL_ALU, subop::kSCALAR_DECIMAL_ALU_ROUND_SCALE, 0,
                                   r1, v.reg, 0, 0, delta, 0u);
                        uint8_t r2 = alloc_r();
                        b.emit_u16(Opcode::OP_SCALAR_DECIMAL_ALU, subop::kSCALAR_DECIMAL_ALU_BOUND_CHK, 0,
                                   r2, r1, 0, 0, tp, 0u);
                        return TypedVal::decimal(r2, tp, ts);
                    }
                    uint8_t v1 = alloc_v();
                    b.emit_u16(Opcode::OP_VEC_DECIMAL_ALU, subop::kVEC_DECIMAL_ALU_ROUND_SCALE, 0,
                               v1, v.reg, 0, 0, delta, 0u);
                    uint8_t v2 = alloc_v();
                    b.emit_u16(Opcode::OP_VEC_DECIMAL_ALU, subop::kVEC_DECIMAL_ALU_BOUND_CHK, 0,
                               v2, v1, 0, 0, tp, 0u);
                    return TypedVal::vec_decimal(v2, v.dom_id, tp, ts);
                } else {
                    if (tp < v.precision) {
                        if (!is_vec) {
                            uint8_t r2 = alloc_r();
                            b.emit_u16(Opcode::OP_SCALAR_DECIMAL_ALU, subop::kSCALAR_DECIMAL_ALU_BOUND_CHK, 0,
                                       r2, v.reg, 0, 0, tp, 0u);
                            return TypedVal::decimal(r2, tp, ts);
                        }
                        uint8_t v2 = alloc_v();
                        b.emit_u16(Opcode::OP_VEC_DECIMAL_ALU, subop::kVEC_DECIMAL_ALU_BOUND_CHK, 0,
                                   v2, v.reg, 0, 0, tp, 0u);
                        return TypedVal::vec_decimal(v2, v.dom_id, tp, ts);
                    }
                    return !is_vec ? TypedVal::decimal(v.reg, tp, ts)
                                   : TypedVal::vec_decimal(v.reg, v.dom_id, tp, ts);
                }
            }
            if (is_int_bank(v.bank)) {
                if (!is_vec) {
                    uint8_t cur_r = v.reg;
                    if (ts > 0u) {
                        uint8_t r1 = alloc_r();
                        b.emit_u16(Opcode::OP_SCALAR_DECIMAL_ALU, subop::kSCALAR_DECIMAL_ALU_RESCALE, 0,
                                   r1, cur_r, 0, 0, ts, 0u);
                        cur_r = r1;
                    }
                    uint8_t r2 = alloc_r();
                    b.emit_u16(Opcode::OP_SCALAR_DECIMAL_ALU, subop::kSCALAR_DECIMAL_ALU_BOUND_CHK, 0,
                               r2, cur_r, 0, 0, tp, 0u);
                    return TypedVal::decimal(r2, tp, ts);
                }
                uint8_t cur_v = v.reg;
                if (ts > 0u) {
                    uint8_t v1 = alloc_v();
                    b.emit_u16(Opcode::OP_VEC_DECIMAL_ALU, subop::kVEC_DECIMAL_ALU_RESCALE, 0,
                               v1, cur_v, 0, 0, ts, 0u);
                    cur_v = v1;
                }
                uint8_t v2 = alloc_v();
                b.emit_u16(Opcode::OP_VEC_DECIMAL_ALU, subop::kVEC_DECIMAL_ALU_BOUND_CHK, 0,
                           v2, cur_v, 0, 0, tp, 0u);
                return TypedVal::vec_decimal(v2, v.dom_id, tp, ts);
            }
            if (is_float_bank(v.bank)) {
                if (!is_vec) {
                    uint8_t dst = alloc_r();
                    b.emit_u16(Opcode::OP_SCALAR_DECIMAL_ALU, subop::kSCALAR_DECIMAL_ALU_F64_TO_DEC, 0,
                               dst, v.reg, 0, 0, ts, tp);
                    return TypedVal::decimal(dst, tp, ts);
                }
                uint8_t dst = alloc_v();
                b.emit_u16(Opcode::OP_VEC_DECIMAL_ALU, subop::kVEC_DECIMAL_ALU_F64_TO_DEC, 0,
                           dst, v.reg, 0, 0, ts, tp);
                return TypedVal::vec_decimal(dst, v.dom_id, tp, ts);
            }
            throw std::runtime_error(std::string("TYPE_MISMATCH: cannot cast ") + val_bank_name(v.bank) + " to DECIMAL");
        }

        if (target.scalar_bank == ValBank::ScalarF64) {
            if (is_decimal_bank(v.bank)) {
                if (!is_vec) {
                    uint8_t dst = alloc_r();
                    b.emit_u16(Opcode::OP_SCALAR_DECIMAL_ALU, subop::kSCALAR_DECIMAL_ALU_DEC_TO_F64, 0,
                               dst, v.reg, 0, 0, v.scale, 0u);
                    return TypedVal::scalar(ValBank::ScalarF64, dst);
                }
                uint8_t dst = alloc_v();
                b.emit_u16(Opcode::OP_VEC_DECIMAL_ALU, subop::kVEC_DECIMAL_ALU_DEC_TO_F64, 0,
                           dst, v.reg, 0, 0, v.scale, 0u);
                return TypedVal::vec(ValBank::VecF64, dst, v.dom_id);
            }
            if (!is_vec) {
                uint8_t dst = alloc_r();
                b.emit(Opcode::OP_SCALAR_FP_ALU, subop::kSCALAR_FP_ALU_I64_TO_F64, 0, dst, v.reg, 0, 0, 0);
                return TypedVal::scalar(ValBank::ScalarF64, dst);
            }
            uint8_t dst = alloc_v();
            b.emit_u16(Opcode::OP_VEC_POINT, subop::kVEC_POINT_VEC_CAST, 0, dst, v.reg, 0, 0, 4u, 0u);
            return TypedVal::vec(ValBank::VecF64, dst, v.dom_id);
        }

        if (target.scalar_bank == ValBank::ScalarI64 || target.scalar_bank == ValBank::ScalarU64) {
            if (is_decimal_bank(v.bank)) {
                if (!is_vec) {
                    if (v.scale == 0u) return TypedVal::scalar(target.scalar_bank, v.reg);
                    uint8_t dst = alloc_r();
                    b.emit_u16(Opcode::OP_SCALAR_DECIMAL_ALU, subop::kSCALAR_DECIMAL_ALU_TRUNC_SCALE, 0,
                               dst, v.reg, 0, 0, v.scale, 0u);
                    return TypedVal::scalar(target.scalar_bank, dst);
                }
                ValBank out_vb = (target.scalar_bank == ValBank::ScalarU64) ? ValBank::VecU64 : ValBank::VecI64;
                if (v.scale == 0u) return TypedVal::vec(out_vb, v.reg, v.dom_id);
                uint8_t dst = alloc_v();
                b.emit_u16(Opcode::OP_VEC_DECIMAL_ALU, subop::kVEC_DECIMAL_ALU_TRUNC_SCALE, 0,
                           dst, v.reg, 0, 0, v.scale, 0u);
                return TypedVal::vec(out_vb, dst, v.dom_id);
            }
            if (!is_vec) {
                uint8_t dst = alloc_r();
                b.emit(Opcode::OP_SCALAR_FP_ALU, subop::kSCALAR_FP_ALU_F64_TO_I64, 0, dst, v.reg, 0, 0, 0);
                return TypedVal::scalar(target.scalar_bank, dst);
            }
            uint8_t dst = alloc_v();
            uint16_t ty_code = (target.scalar_bank == ValBank::ScalarU64) ? 1u : 0u;
            b.emit_u16(Opcode::OP_VEC_POINT, subop::kVEC_POINT_VEC_CAST, 0, dst, v.reg, 0, 0, ty_code, 0u);
            return TypedVal::vec((ty_code == 1u) ? ValBank::VecU64 : ValBank::VecI64, dst, v.dom_id);
        }
        throw std::runtime_error(std::string("TYPE_MISMATCH: unsupported cast target ") +
                                 val_bank_name(target.scalar_bank));
    }

    void emit_scalar_i64_cmp(DecimalCmpOp op, uint8_t dst, uint8_t a, uint8_t b_reg) {
        switch (op) {
            case DecimalCmpOp::EQ:
                b.emit(Opcode::OP_SCALAR_3VL, subop::kSCALAR_3VL_CMP_EQ_I64, 0, dst, a, b_reg, 0, 0); break;
            case DecimalCmpOp::NE:
                b.emit(Opcode::OP_SCALAR_3VL, subop::kSCALAR_3VL_CMP_NE_I64, 0, dst, a, b_reg, 0, 0); break;
            case DecimalCmpOp::LT:
                b.emit(Opcode::OP_SCALAR_3VL, subop::kSCALAR_3VL_CMP_LT_I64, 0, dst, a, b_reg, 0, 0); break;
            case DecimalCmpOp::LE:
                b.emit(Opcode::OP_SCALAR_3VL, subop::kSCALAR_3VL_CMP_LE_I64, 0, dst, a, b_reg, 0, 0); break;
            case DecimalCmpOp::GT:
                b.emit(Opcode::OP_SCALAR_3VL, subop::kSCALAR_3VL_CMP_LT_I64, 0, dst, b_reg, a, 0, 0); break;
            case DecimalCmpOp::GE:
                b.emit(Opcode::OP_SCALAR_3VL, subop::kSCALAR_3VL_CMP_LE_I64, 0, dst, b_reg, a, 0, 0); break;
        }
    }

    void emit_scalar_f64_cmp(DecimalCmpOp op, uint8_t dst, uint8_t a, uint8_t b_reg) {
        switch (op) {
            case DecimalCmpOp::EQ:
                b.emit(Opcode::OP_SCALAR_3VL, subop::kSCALAR_3VL_CMP_EQ_F64, 0, dst, a, b_reg, 0, 0); break;
            case DecimalCmpOp::NE:
                b.emit(Opcode::OP_SCALAR_3VL, subop::kSCALAR_3VL_CMP_NE_F64, 0, dst, a, b_reg, 0, 0); break;
            case DecimalCmpOp::LT:
                b.emit(Opcode::OP_SCALAR_3VL, subop::kSCALAR_3VL_CMP_LT_F64, 0, dst, a, b_reg, 0, 0); break;
            case DecimalCmpOp::LE:
                b.emit(Opcode::OP_SCALAR_3VL, subop::kSCALAR_3VL_CMP_LE_F64, 0, dst, a, b_reg, 0, 0); break;
            case DecimalCmpOp::GT:
                b.emit(Opcode::OP_SCALAR_3VL, subop::kSCALAR_3VL_CMP_LT_F64, 0, dst, b_reg, a, 0, 0); break;
            case DecimalCmpOp::GE:
                b.emit(Opcode::OP_SCALAR_3VL, subop::kSCALAR_3VL_CMP_LE_F64, 0, dst, b_reg, a, 0, 0); break;
        }
    }

    TypedVal emit_folded_vec_cmp(DecimalCmpOp op, const TypedVal& vec_dec, const TypedVal& const_lit, uint16_t out_dom) {
        FoldedDecimalComparison fc = fold_decimal_comparison(
            op, vec_dec.precision, vec_dec.scale, const_lit.const_val, const_lit.scale);
        if (fc.kind == DecimalFoldKind::COMPARE) {
            uint8_t r_thresh = alloc_r();
            emit_const_i64(r_thresh, fc.threshold_unscaled);
            uint8_t dst = alloc_v();
            b.emit(Opcode::OP_VEC_CMP, cmp_op_to_vec_cmp_subop(fc.op), FLAG_SCALAR_RHS,
                   dst, vec_dec.reg, r_thresh, 0, 0);
            return TypedVal::vec(ValBank::VecBool, dst, out_dom);
        }
        uint8_t dst = alloc_v();
        if (fc.kind == DecimalFoldKind::ALWAYS_FALSE) {
            // Preserves Kleene 3VL NULL lanes: vec_dec != vec_dec
            b.emit(Opcode::OP_VEC_CMP, subop::kVEC_CMP_NE, 0, dst, vec_dec.reg, vec_dec.reg, 0, 0);
        } else {
            // Preserves Kleene 3VL NULL lanes: vec_dec == vec_dec
            b.emit(Opcode::OP_VEC_CMP, subop::kVEC_CMP_EQ, 0, dst, vec_dec.reg, vec_dec.reg, 0, 0);
        }
        return TypedVal::vec(ValBank::VecBool, dst, out_dom);
    }

    TypedVal eval_comparison(const std::string& head, const TypedVal& lhs, const TypedVal& rhs) {
        PinGuard pg(pinned_r_mask, pinned_v_mask);
        pin_r(lhs);
        pin_v(lhs);
        pin_r(rhs);
        pin_v(rhs);

        DecimalCmpOp cmp_op = DecimalCmpOp::EQ;
        if (!parse_cmp_op(head, cmp_op)) {
            throw std::runtime_error("PARSE_ERROR: unknown comparison op '" + head + "'");
        }
        bool any_dec = is_decimal_bank(lhs.bank) || is_decimal_bank(rhs.bank);
        if (any_dec) {
            TypedVal dl = ensure_decimal(lhs, head);
            TypedVal dr = ensure_decimal(rhs, head);
            bool l_vec = is_vec_bank(dl.bank);
            bool r_vec = is_vec_bank(dr.bank);
            uint16_t out_dom = l_vec ? dl.dom_id : (r_vec ? dr.dom_id : 0xFFFFu);

            // Compile-time boundary folding (both vec <op> lit AND symmetric lit <op> vec)
            if (l_vec && !r_vec && dr.is_const) {
                return emit_folded_vec_cmp(cmp_op, dl, dr, out_dom);
            }
            if (!l_vec && r_vec && dl.is_const) {
                return emit_folded_vec_cmp(flip_cmp_op(cmp_op), dr, dl, out_dom);
            }

            uint8_t s = std::max(dl.scale, dr.scale);
            TypedVal a = rescale_decimal_up(dl, s);
            pin_r(a);
            pin_v(a);
            TypedVal bv = rescale_decimal_up(dr, s);
            pin_r(bv);
            pin_v(bv);

            if (!l_vec && !r_vec) {
                uint8_t dst = alloc_r();
                emit_scalar_i64_cmp(cmp_op, dst, a.reg, bv.reg);
                return TypedVal::scalar(ValBank::ScalarBool, dst);
            }
            if (l_vec && r_vec) {
                uint8_t dst = alloc_v();
                b.emit(Opcode::OP_VEC_CMP, cmp_op_to_vec_cmp_subop(cmp_op), 0, dst, a.reg, bv.reg, 0, 0);
                return TypedVal::vec(ValBank::VecBool, dst, out_dom);
            }
            if (l_vec) {
                uint8_t dst = alloc_v();
                b.emit(Opcode::OP_VEC_CMP, cmp_op_to_vec_cmp_subop(cmp_op), FLAG_SCALAR_RHS,
                       dst, a.reg, bv.reg, 0, 0);
                return TypedVal::vec(ValBank::VecBool, dst, out_dom);
            }
            uint8_t dst = alloc_v();
            b.emit(Opcode::OP_VEC_CMP, cmp_op_to_vec_cmp_subop(flip_cmp_op(cmp_op)), FLAG_SCALAR_RHS,
                   dst, bv.reg, a.reg, 0, 0);
            return TypedVal::vec(ValBank::VecBool, dst, out_dom);
        }

        if ((is_int_bank(lhs.bank) && is_float_bank(rhs.bank)) ||
            (is_float_bank(lhs.bank) && is_int_bank(rhs.bank))) {
            throw std::runtime_error("TYPE_MISMATCH: implicit int/float comparison forbidden; use (cast ...)");
        }
        if (!is_vec_bank(lhs.bank) && !is_vec_bank(rhs.bank)) {
            uint8_t dst = alloc_r();
            if (is_float_bank(lhs.bank)) {
                emit_scalar_f64_cmp(cmp_op, dst, lhs.reg, rhs.reg);
            } else {
                emit_scalar_i64_cmp(cmp_op, dst, lhs.reg, rhs.reg);
            }
            return TypedVal::scalar(ValBank::ScalarBool, dst);
        }
        uint16_t out_dom = is_vec_bank(lhs.bank) ? lhs.dom_id : rhs.dom_id;
        uint8_t dst = alloc_v();
        if (is_vec_bank(lhs.bank) && is_vec_bank(rhs.bank)) {
            b.emit(Opcode::OP_VEC_CMP, cmp_op_to_vec_cmp_subop(cmp_op), 0, dst, lhs.reg, rhs.reg, 0, 0);
        } else if (is_vec_bank(lhs.bank)) {
            b.emit(Opcode::OP_VEC_CMP, cmp_op_to_vec_cmp_subop(cmp_op), FLAG_SCALAR_RHS,
                   dst, lhs.reg, rhs.reg, 0, 0);
        } else {
            b.emit(Opcode::OP_VEC_CMP, cmp_op_to_vec_cmp_subop(flip_cmp_op(cmp_op)), FLAG_SCALAR_RHS,
                   dst, rhs.reg, lhs.reg, 0, 0);
        }
        return TypedVal::vec(ValBank::VecBool, dst, out_dom);
    }

    TypedVal eval_node_filter(const SExprPtr& e) {
        if (e->elems.size() < 3) throw std::runtime_error("PARSE_ERROR: malformed node-filter");
        TypedVal src_f = eval_expr(e->elems[1]);
        if (src_f.bank != ValBank::Frontier) {
            throw std::runtime_error("TYPE_MISMATCH: node-filter expects frontier");
        }
        std::string attr_name;
        std::string op_str;
        SExprPtr rhs_expr;
        if (e->elems.size() == 5 && e->elems[2]->kind == SExprKind::String) {
            attr_name = e->elems[2]->text;
            op_str = e->elems[3]->text;
            rhs_expr = e->elems[4];
        } else if (e->elems.size() == 3 && e->elems[2]->kind == SExprKind::List &&
                   e->elems[2]->elems.size() >= 3 && e->elems[2]->elems[0]->text == "lambda") {
            const auto& pred = e->elems[2]->elems[2];
            if (pred->kind != SExprKind::List || pred->elems.size() != 3) {
                throw std::runtime_error("PARSE_ERROR: unsupported node-filter predicate");
            }
            op_str = pred->elems[0]->text;
            const auto& lhs_form = pred->elems[1];
            rhs_expr = pred->elems[2];
            if (lhs_form->kind != SExprKind::List || lhs_form->elems.size() != 3 ||
                lhs_form->elems[0]->text != "attr") {
                throw std::runtime_error("PARSE_ERROR: node-filter predicate expects (attr u \"name\")");
            }
            attr_name = lhs_form->elems[2]->text;
        } else {
            throw std::runtime_error("PARSE_ERROR: malformed node-filter");
        }

        DecimalCmpOp cmp_op = DecimalCmpOp::EQ;
        if (!parse_cmp_op(op_str, cmp_op)) {
            throw std::runtime_error("PARSE_ERROR: unsupported node-filter op '" + op_str + "'");
        }
        AttrInfo ai = resolve_node_attr(src_f.dom_id, attr_name);
        TypedVal rhs_val = eval_expr(rhs_expr);
        uint8_t dst_f = alloc_f();

        if (ai.is_decimal() || is_decimal_bank(rhs_val.bank)) {
            uint16_t col_prec = 0;
            uint8_t col_scale = 0;
            if (ai.is_decimal()) {
                col_prec = ai.precision;
                col_scale = ai.scale;
            } else {
                ValBank col_bank = attr_type_to_vec_bank(ai);
                if (!is_int_bank(col_bank)) {
                    throw std::runtime_error("TYPE_MISMATCH: cannot compare non-decimal column '" + attr_name +
                                             "' (" + val_bank_name(col_bank) + ") with DECIMAL; use (cast ...)");
                }
                col_prec = 18u;
                col_scale = 0u;
            }
            TypedVal dec_rhs = ensure_decimal(rhs_val, op_str);
            if (dec_rhs.is_const) {
                FoldedDecimalComparison fc = fold_decimal_comparison(
                    cmp_op, col_prec, col_scale, dec_rhs.const_val, dec_rhs.scale);
                if (fc.kind == DecimalFoldKind::ALWAYS_FALSE) {
                    b.emit_u16(Opcode::OP_FSEED, subop::kFSEED_EMPTY, 0, dst_f, 0, 0, 0, src_f.dom_id, 0);
                    return TypedVal::frontier(dst_f, src_f.dom_id);
                }
                if (fc.kind == DecimalFoldKind::ALWAYS_TRUE) {
                    b.emit_u16(Opcode::OP_NODE_FILTER, subop::kNODE_FILTER_IS_NOT_NULL, 0,
                               dst_f, src_f.reg, 0, 0, src_f.dom_id, ai.attr_id);
                    return TypedVal::frontier(dst_f, src_f.dom_id);
                }
                uint8_t r_thresh = alloc_r();
                b.emit_load_const_i64(r_thresh, fc.threshold_unscaled);
                b.emit_u16(Opcode::OP_NODE_FILTER, cmp_op_to_node_filter_subop(fc.op), FLAG_SCALAR_RHS,
                           dst_f, src_f.reg, r_thresh, 0, src_f.dom_id, ai.attr_id);
                return TypedVal::frontier(dst_f, src_f.dom_id);
            }
            uint8_t r_cmp = dec_rhs.reg;
            if (dec_rhs.scale < col_scale) {
                r_cmp = rescale_decimal_up(dec_rhs, col_scale).reg;
            } else if (dec_rhs.scale > col_scale) {
                throw std::runtime_error("TYPE_MISMATCH: dynamic node-filter parameter scale (" +
                                         std::to_string(dec_rhs.scale) + ") exceeds column scale (" +
                                         std::to_string(col_scale) + "); cast parameter first");
            }
            b.emit_u16(Opcode::OP_NODE_FILTER, cmp_op_to_node_filter_subop(cmp_op), FLAG_SCALAR_RHS,
                       dst_f, src_f.reg, r_cmp, 0, src_f.dom_id, ai.attr_id);
            return TypedVal::frontier(dst_f, src_f.dom_id);
        }

        ValBank col_bank = attr_type_to_vec_bank(ai);
        if ((is_int_bank(col_bank) && is_float_bank(rhs_val.bank)) ||
            (is_float_bank(col_bank) && is_int_bank(rhs_val.bank))) {
            throw std::runtime_error("TYPE_MISMATCH: implicit int/float node-filter comparison forbidden; use (cast ...)");
        }
        b.emit_u16(Opcode::OP_NODE_FILTER, cmp_op_to_node_filter_subop(cmp_op), FLAG_SCALAR_RHS,
                   dst_f, src_f.reg, rhs_val.reg, 0, src_f.dom_id, ai.attr_id);
        return TypedVal::frontier(dst_f, src_f.dom_id);
    }

    TypedVal eval_edge_stream(const SExprPtr& e) {
        if (e->elems.size() < 5) throw std::runtime_error("PARSE_ERROR: malformed edge-stream");
        const std::string& dir = e->elems[1]->text;
        const std::string& rel_name = e->elems[2]->text;
        RelInfo rel = resolve_rel(rel_name);
        TypedVal src_f = eval_expr(e->elems[3]);
        if (src_f.bank != ValBank::Frontier) {
            throw std::runtime_error("TYPE_MISMATCH: edge-stream expects frontier");
        }
        uint16_t expected_src = (dir == ":rev") ? rel.tgt_dom : rel.src_dom;
        if (src_f.dom_id != expected_src) {
            throw std::runtime_error("DOMAIN_MISMATCH: edge-stream source frontier domain mismatch on '" + rel_name + "'");
        }
        const auto& lam = e->elems[4];
        if (lam->kind != SExprKind::List || lam->elems.size() < 3 || lam->elems[0]->text != "lambda") {
            throw std::runtime_error("PARSE_ERROR: edge-stream expects (lambda (src tgt edge) ...)");
        }
        const auto& params = lam->elems[1]->elems;
        if (params.size() != 3) {
            throw std::runtime_error("PARSE_ERROR: edge-stream lambda requires 3 params (src tgt edge)");
        }
        std::string src_var = params[0]->text;
        std::string tgt_var = params[1]->text;
        std::string edge_var = params[2]->text;

        uint8_t w_sub = (dir == ":rev") ? subop::kWALK_MORSEL_REV : subop::kWALK_MORSEL_FWD;

        // Buffer morsel body words separately so any scalar OP_LOAD_CONST inside the body
        // is hoisted cleanly BEFORE OP_WALK_MORSEL (OP_LOAD_CONST is MorselRole::Forbidden).
        V11ProgramBuilder outer_b = std::move(b);
        b = V11ProgramBuilder();

        auto prev_morsel = std::move(morsel);
        uint32_t saved_pin_r = pinned_r_mask;
        morsel = std::make_unique<MorselScope>();
        morsel->rel = rel;
        morsel->src_var = src_var;
        morsel->tgt_var = tgt_var;
        morsel->edge_var = edge_var;
        morsel->hoisted_b = &outer_b;

        TypedVal result = src_f;
        try {
            for (size_t i = 2; i < lam->elems.size(); ++i) {
                const auto& stmt = lam->elems[i];
                if (stmt->kind != SExprKind::List || stmt->elems.empty()) {
                    throw std::runtime_error("PARSE_ERROR: malformed edge-stream statement");
                }
                const std::string& sh = stmt->elems[0]->text;
                if (sh == "where") {
                    TypedVal pred = eval_expr(stmt->elems[1]);
                    if (pred.bank != ValBank::VecBool) {
                        throw std::runtime_error("TYPE_MISMATCH: where requires bool vector predicate");
                    }
                    // Convert V-bank bool vector into F11 morsel mask frontier (FLAG_MASKED_S3 reads F-bank)
                    b.emit(Opcode::OP_SET_CONVERT, 2u, 0, 11u, pred.reg, 0, 0, 0);
                    morsel->pred_reg = 11;
                } else if (sh == "scatter-add") {
                    TypedVal out_vec = eval_expr(stmt->elems[1]);
                    const std::string& idx_sym = stmt->elems[2]->text;
                    int idx_reg = (idx_sym == tgt_var) ? 9 : ((idx_sym == src_var) ? 8 : -1);
                    if (idx_reg < 0) {
                        throw std::runtime_error("PARSE_ERROR: scatter-add target must be src or tgt cursor");
                    }
                    TypedVal val_expr = eval_expr(stmt->elems[3]);
                    uint8_t flags = (morsel->pred_reg >= 0) ? FLAG_MASKED_S3 : 0u;
                    uint8_t pred_r = (morsel->pred_reg >= 0) ? static_cast<uint8_t>(morsel->pred_reg) : 0u;
                    if (is_decimal_bank(out_vec.bank) || is_decimal_bank(val_expr.bank)) {
                        TypedVal dec_val = ensure_decimal(val_expr, "scatter-add");
                        if (!is_decimal_bank(out_vec.bank)) {
                            throw std::runtime_error("TYPE_MISMATCH: scatter-add decimal value into non-decimal accumulator");
                        }
                        if (dec_val.scale < out_vec.scale) {
                            dec_val = rescale_decimal_up(dec_val, out_vec.scale);
                        } else if (dec_val.scale > out_vec.scale) {
                            uint8_t v_round = alloc_v();
                            b.emit_u16(Opcode::OP_VEC_DECIMAL_ALU, subop::kVEC_DECIMAL_ALU_ROUND_SCALE, 0,
                                       v_round, dec_val.reg, 0, 0,
                                       static_cast<uint16_t>(dec_val.scale - out_vec.scale), 0u);
                            dec_val = TypedVal::vec_decimal(v_round, out_vec.dom_id, out_vec.precision, out_vec.scale);
                        }
                        b.emit(Opcode::OP_MORSEL_COMMIT, subop::kMORSEL_COMMIT_SCATTER_ADD_CHK,
                               flags, out_vec.reg, static_cast<uint8_t>(idx_reg), dec_val.reg, pred_r, 0);
                    } else {
                        b.emit(Opcode::OP_MORSEL_COMMIT, subop::kMORSEL_COMMIT_SCATTER_ADD,
                               flags, out_vec.reg, static_cast<uint8_t>(idx_reg), val_expr.reg, pred_r, 0);
                    }
                    result = out_vec;
                } else if (sh == "yield-frontier") {
                    TypedVal out_f = eval_expr(stmt->elems[1]);
                    const std::string& idx_sym = stmt->elems[2]->text;
                    uint8_t idx_reg = (idx_sym == tgt_var) ? 9u : 8u;
                    uint8_t flags = (morsel->pred_reg >= 0) ? FLAG_MASKED_S3 : 0u;
                    uint8_t pred_r = (morsel->pred_reg >= 0) ? static_cast<uint8_t>(morsel->pred_reg) : 0u;
                    b.emit(Opcode::OP_MORSEL_COMMIT, subop::kMORSEL_COMMIT_YIELD_FRONTIER,
                           flags, out_f.reg, idx_reg, 0, pred_r, 0);
                    result = out_f;
                } else {
                    result = eval_expr(stmt);
                }
            }
        } catch (...) {
            b = std::move(outer_b);
            pinned_r_mask = saved_pin_r;
            morsel = std::move(prev_morsel);
            throw;
        }
        std::vector<uint64_t> body_words = b.words();
        b = std::move(outer_b);
        pinned_r_mask = saved_pin_r;
        morsel = std::move(prev_morsel);

        uint16_t body_len = static_cast<uint16_t>(1u + body_words.size());
        b.emit_u16(Opcode::OP_WALK_MORSEL, w_sub, 0, 0, src_f.reg, 0, 0, rel.rel_id, body_len);
        b.append_words(body_words);
        b.emit_u16(Opcode::OP_MORSEL_END, 0, 0, 0, 0, 0, 0, rel.rel_id, body_len);
        return result;
    }

    TypedVal eval_expr(const SExprPtr& e) {
        if (e->kind == SExprKind::Int) {
            uint8_t r = alloc_r();
            emit_const_i64(r, e->i_val);
            return TypedVal::const_int(r, e->i_val);
        }
        if (e->kind == SExprKind::Float) {
            uint8_t r = alloc_r();
            emit_const_f64(r, e->f_val);
            return TypedVal::scalar(ValBank::ScalarF64, r);
        }
        if (e->kind == SExprKind::Bool) {
            uint8_t r = alloc_r();
            emit_const_i64(r, e->b_val ? 1LL : 0LL);
            return TypedVal::scalar(ValBank::ScalarBool, r);
        }
        if (e->kind == SExprKind::String) {
            uint8_t r = alloc_r();
            auto& cb = const_builder();
            uint32_t cp_id = cb.add_const_pool_str(e->text);
            if (morsel && morsel->hoisted_b) pinned_r_mask |= (1u << r);
            cb.emit(Opcode::OP_LOAD_CONST_POOL, subop::kLOAD_CONST_POOL_STR, 0, r, 0, 0, 0, cp_id);
            return TypedVal::scalar(ValBank::ScalarStr, r);
        }
        if (e->kind == SExprKind::Null) {
            uint8_t r = alloc_r();
            emit_const_null(r);
            return TypedVal::scalar(ValBank::ScalarI64, r);
        }
        if (e->kind == SExprKind::Symbol || e->kind == SExprKind::Param) {
            auto it = env.find(e->text);
            if (it == env.end()) {
                throw std::runtime_error("UNBOUND_SYMBOL: '" + e->text + "'");
            }
            return it->second;
        }
        if (e->kind != SExprKind::List || e->elems.empty() || e->elems[0]->kind != SExprKind::Symbol) {
            throw std::runtime_error("PARSE_ERROR: expected form");
        }
        const std::string& head = e->elems[0]->text;

        // Exact decimal literal: (dec "123.45" P S)
        if (head == "dec") {
            if (e->elems.size() != 4 || e->elems[1]->kind != SExprKind::String ||
                e->elems[2]->kind != SExprKind::Int || e->elems[3]->kind != SExprKind::Int) {
                throw std::runtime_error("PARSE_ERROR: (dec \"digits\" P S) expected");
            }
            int64_t p_raw = e->elems[2]->i_val;
            int64_t s_raw = e->elems[3]->i_val;
            if (p_raw < 1 || p_raw > 18 || s_raw < 0 || s_raw > p_raw) {
                throw std::runtime_error("PARSE_ERROR: invalid DECIMAL(" + std::to_string(p_raw) + ", " +
                                         std::to_string(s_raw) + ")");
            }
            uint16_t p = static_cast<uint16_t>(p_raw);
            uint8_t s = static_cast<uint8_t>(s_raw);
            int64_t unscaled = parse_decimal_unscaled_exact(e->elems[1]->text, p, s);
            uint8_t r = alloc_r();
            emit_const_i64(r, unscaled);
            return TypedVal::const_decimal(r, p, s, unscaled);
        }

        if (head == "let") {
            if (e->elems.size() < 3 || e->elems[1]->kind != SExprKind::List) {
                throw std::runtime_error("PARSE_ERROR: malformed let");
            }
            auto saved_env = env;
            for (const auto& bind : e->elems[1]->elems) {
                if (bind->kind != SExprKind::List || bind->elems.size() != 2 || bind->elems[0]->kind != SExprKind::Symbol) {
                    throw std::runtime_error("PARSE_ERROR: malformed let binding");
                }
                TypedVal v = eval_expr(bind->elems[1]);
                env[bind->elems[0]->text] = v;
            }
            TypedVal last{};
            for (size_t i = 2; i < e->elems.size(); ++i) {
                last = eval_expr(e->elems[i]);
            }
            env = std::move(saved_env);
            return last;
        }

        if (head == "domain-all") {
            if (e->elems.size() != 2 || e->elems[1]->kind != SExprKind::String)
                throw std::runtime_error("PARSE_ERROR: (domain-all \"Dom\")");
            uint16_t dom = resolve_domain(e->elems[1]->text);
            uint8_t dst = alloc_f();
            b.emit_u16(Opcode::OP_INIT_DOMAIN_SPAN, subop::kINIT_DOMAIN_SPAN_ALL_SPAN, 0, dst, 0, 0, 0, dom, 0);
            return TypedVal::frontier(dst, dom);
        }

        if (head == "seed") {
            if (e->elems.size() < 2 || e->elems[1]->kind != SExprKind::String)
                throw std::runtime_error("PARSE_ERROR: (seed \"Dom\" id...)");
            uint16_t dom = resolve_domain(e->elems[1]->text);
            uint8_t dst = alloc_f();
            if (e->elems.size() == 2) {
                b.emit_u16(Opcode::OP_FSEED, subop::kFSEED_EMPTY, 0, dst, 0, 0, 0, dom, 0);
            } else {
                TypedVal id0 = eval_expr(e->elems[2]);
                if (id0.dom_id != 0xFFFFu && id0.dom_id != dom) {
                    throw std::runtime_error("DOMAIN_MISMATCH: seed id domain mismatch");
                }
                b.emit_u16(Opcode::OP_FSEED, subop::kFSEED_SINGLE_REG, 0, dst, id0.reg, 0, 0, dom, 0);
                for (size_t i = 3; i < e->elems.size(); ++i) {
                    TypedVal idi = eval_expr(e->elems[i]);
                    b.emit_u16(Opcode::OP_FSEED, subop::kFSEED_INSERT_REG, 0, dst, idi.reg, 0, 0, dom, 0);
                }
            }
            return TypedVal::frontier(dst, dom);
        }

        if (head == "walk") {
            if (e->elems.size() < 4 || e->elems[1]->kind != SExprKind::Keyword || e->elems[2]->kind != SExprKind::String) {
                throw std::runtime_error("PARSE_ERROR: (walk :fwd|:rev|:both \"REL\" f ...)");
            }
            const std::string& dir = e->elems[1]->text;
            RelInfo rel = resolve_rel(e->elems[2]->text);
            TypedVal src_f = eval_expr(e->elems[3]);
            if (src_f.bank != ValBank::Frontier) throw std::runtime_error("TYPE_MISMATCH: walk expects frontier");

            bool trans_plus = false, trans_star = false;
            bool has_exclude = false;
            TypedVal excl_f{};
            for (size_t i = 4; i < e->elems.size(); ++i) {
                if (e->elems[i]->kind == SExprKind::Keyword) {
                    const std::string& kw = e->elems[i]->text;
                    if (kw == ":transitive+") trans_plus = true;
                    else if (kw == ":transitive*") trans_star = true;
                    else if (kw == ":exclude" && i + 1 < e->elems.size()) {
                        excl_f = eval_expr(e->elems[++i]);
                        has_exclude = true;
                    }
                }
            }

            uint16_t expected_src = (dir == ":rev") ? rel.tgt_dom : rel.src_dom;
            uint16_t out_dom = (dir == ":rev") ? rel.src_dom : rel.tgt_dom;
            if ((dir == ":both" || trans_plus || trans_star) && rel.src_dom != rel.tgt_dom) {
                throw std::runtime_error("DOMAIN_MISMATCH: non-endo relation '" + e->elems[2]->text + "' in :both/:transitive");
            }
            if (src_f.dom_id != expected_src) {
                throw std::runtime_error("DOMAIN_MISMATCH: walk source frontier domain mismatch on '" + e->elems[2]->text + "'");
            }
            if (has_exclude && excl_f.dom_id != out_dom) {
                throw std::runtime_error("DOMAIN_MISMATCH: walk :exclude frontier domain mismatch");
            }

            uint8_t wsub = subop::kWALK_FWD;
            if (dir == ":fwd") {
                if (trans_star) wsub = subop::kWALK_FWD_FIXPOINT_STAR;
                else if (trans_plus) wsub = subop::kWALK_FWD_FIXPOINT_PLUS;
                else wsub = subop::kWALK_FWD;
            } else if (dir == ":rev") {
                if (trans_star) wsub = subop::kWALK_REV_FIXPOINT_STAR;
                else if (trans_plus) wsub = subop::kWALK_REV_FIXPOINT_PLUS;
                else wsub = subop::kWALK_REV;
            } else if (dir == ":both") {
                wsub = subop::kWALK_BOTH;
            }
            uint8_t dst = alloc_f();
            uint8_t flags = has_exclude ? FLAG_MASKED_S3 : 0u;
            uint8_t s3 = has_exclude ? excl_f.reg : 0u;
            b.emit_u16(Opcode::OP_WALK, wsub, flags, dst, src_f.reg, 0, s3, rel.rel_id, 0);
            return TypedVal::frontier(dst, out_dom);
        }

        if (head == "set-union" || head == "set-intersect" || head == "set-diff" || head == "set-sym-diff") {
            if (e->elems.size() != 3) throw std::runtime_error("PARSE_ERROR: set-op requires 2 frontiers");
            TypedVal a = eval_expr(e->elems[1]);
            TypedVal bv = eval_expr(e->elems[2]);
            if (a.bank != ValBank::Frontier || bv.bank != ValBank::Frontier)
                throw std::runtime_error("TYPE_MISMATCH: set-op requires frontiers");
            if (a.dom_id != bv.dom_id)
                throw std::runtime_error("DOMAIN_MISMATCH: set-op domain mismatch");
            uint8_t ssub = subop::kSET_OP_UNION;
            if (head == "set-intersect") ssub = subop::kSET_OP_INTERSECT;
            else if (head == "set-diff") ssub = subop::kSET_OP_DIFF;
            else if (head == "set-sym-diff") ssub = subop::kSET_OP_SYM_DIFF;
            uint8_t dst = alloc_f();
            b.emit(Opcode::OP_SET_OP, ssub, 0, dst, a.reg, bv.reg, 0, 0);
            return TypedVal::frontier(dst, a.dom_id);
        }

        if (head == "cardinality") {
            if (e->elems.size() != 2) throw std::runtime_error("PARSE_ERROR: (cardinality f)");
            TypedVal f = eval_expr(e->elems[1]);
            if (f.bank != ValBank::Frontier) throw std::runtime_error("TYPE_MISMATCH: cardinality requires frontier");
            uint8_t dst = alloc_r();
            b.emit(Opcode::OP_SET_CARDINALITY, subop::kSET_CARDINALITY_CARD_1BIT, 0, dst, f.reg, 0, 0, 0);
            return TypedVal::scalar(ValBank::ScalarU64, dst);
        }

        if (head == "contains?") {
            if (e->elems.size() != 3) throw std::runtime_error("PARSE_ERROR: (contains? f id)");
            TypedVal f = eval_expr(e->elems[1]);
            TypedVal id = eval_expr(e->elems[2]);
            if (f.bank != ValBank::Frontier) throw std::runtime_error("TYPE_MISMATCH: contains? requires frontier");
            if (id.dom_id != 0xFFFFu && id.dom_id != f.dom_id)
                throw std::runtime_error("DOMAIN_MISMATCH: contains? id domain mismatch");
            uint8_t dst = alloc_r();
            b.emit(Opcode::OP_SET_CONTAINS, subop::kSET_CONTAINS_CONTAINS_1BIT, 0, dst, f.reg, id.reg, 0, 0);
            return TypedVal::scalar(ValBank::ScalarBool, dst);
        }

        if (head == "load-node-attr") {
            if (e->elems.size() != 3 || e->elems[1]->kind != SExprKind::String || e->elems[2]->kind != SExprKind::String)
                throw std::runtime_error("PARSE_ERROR: (load-node-attr \"Dom\" \"attr\")");
            uint16_t dom = resolve_domain(e->elems[1]->text);
            AttrInfo ai = resolve_node_attr(dom, e->elems[2]->text);
            uint8_t dst_v = alloc_v();
            b.emit_u16(Opcode::OP_LOAD_COLUMN, subop::kLOAD_COLUMN_NODE_ATTR, 0, dst_v, 0, 0, 0, dom, ai.attr_id);
            if (ai.is_decimal()) {
                return TypedVal::vec_decimal(dst_v, dom, ai.precision, ai.scale);
            }
            return TypedVal::vec(attr_type_to_vec_bank(ai), dst_v, dom);
        }

        if (head == "vec-fill") {
            if (e->elems.size() != 4 || e->elems[1]->kind != SExprKind::String)
                throw std::runtime_error("PARSE_ERROR: (vec-fill \"Dom\" type val)");
            uint16_t dom = resolve_domain(e->elems[1]->text);
            TypeSpec ts = parse_scalar_type_spec(e->elems[2]);
            TypedVal init_val = eval_expr(e->elems[3]);
            uint8_t dst_v = alloc_v();
            uint8_t sub = subop::kVEC_INIT_DOM_I64;
            switch (ts.scalar_bank) {
                case ValBank::ScalarI64:
                case ValBank::ScalarDecimal: sub = subop::kVEC_INIT_DOM_I64; break;
                case ValBank::ScalarU64: sub = subop::kVEC_INIT_DOM_U64; break;
                case ValBank::ScalarF64: sub = subop::kVEC_INIT_DOM_F64; break;
                case ValBank::ScalarBool: sub = subop::kVEC_INIT_DOM_BOOL_3VL; break;
                default: throw std::runtime_error("TYPE_MISMATCH: unsupported vec-fill type");
            }
            if (ts.scalar_bank == ValBank::ScalarDecimal) {
                TypedVal dv = ensure_decimal(init_val, "vec-fill");
                if (dv.is_const && dv.const_val == 0LL) {
                    b.emit_u16(Opcode::OP_VEC_INIT, sub, 0, dst_v, 0, 0, 0, dom, 0);
                } else {
                    TypedVal cast_val = eval_cast(dv, ts);
                    b.emit_u16(Opcode::OP_VEC_INIT, sub, FLAG_SCALAR_RHS, dst_v, 0, cast_val.reg, 0, dom, 0);
                }
                return TypedVal::vec_decimal(dst_v, dom, ts.precision, ts.scale);
            }
            if (!is_scalar_bank(init_val.bank)) {
                throw std::runtime_error("TYPE_MISMATCH: vec-fill requires scalar initial value");
            }
            if ((ts.scalar_bank == ValBank::ScalarF64 && !is_float_bank(init_val.bank)) ||
                ((ts.scalar_bank == ValBank::ScalarI64 || ts.scalar_bank == ValBank::ScalarU64) && !is_int_bank(init_val.bank)) ||
                (ts.scalar_bank == ValBank::ScalarBool && !is_bool_bank(init_val.bank))) {
                throw std::runtime_error("TYPE_MISMATCH: vec-fill initial value type mismatch");
            }
            b.emit_u16(Opcode::OP_VEC_INIT, sub, FLAG_SCALAR_RHS, dst_v, 0, init_val.reg, 0, dom, 0);
            ValBank vb = ValBank::VecI64;
            if (ts.scalar_bank == ValBank::ScalarU64) vb = ValBank::VecU64;
            else if (ts.scalar_bank == ValBank::ScalarF64) vb = ValBank::VecF64;
            else if (ts.scalar_bank == ValBank::ScalarBool) vb = ValBank::VecBool;
            return TypedVal::vec(vb, dst_v, dom);
        }

        if (head == "node-filter") return eval_node_filter(e);
        if (head == "edge-stream") return eval_edge_stream(e);

        if (head == "edge-attr") {
            if (!morsel) throw std::runtime_error("PARSE_ERROR: edge-attr only valid inside edge-stream");
            if (e->elems.size() != 3 || e->elems[2]->kind != SExprKind::String)
                throw std::runtime_error("PARSE_ERROR: (edge-attr edge \"attr\")");
            AttrInfo ai = resolve_edge_attr(morsel->rel, e->elems[2]->text);
            uint8_t dst_v = alloc_v();
            b.emit_u16(Opcode::OP_GATHER_EDGE_ATTR, subop::kGATHER_EDGE_ATTR_VEC, 0,
                       dst_v, 10u, 0, 0, morsel->rel.rel_id, ai.attr_id);
            if (ai.is_decimal()) {
                return TypedVal::vec_decimal(dst_v, morsel->rel.tgt_dom, ai.precision, ai.scale);
            }
            return TypedVal::vec(attr_type_to_vec_bank(ai), dst_v, morsel->rel.tgt_dom);
        }

        if (head == "attr") {
            if (!morsel) throw std::runtime_error("PARSE_ERROR: attr only valid inside edge-stream/node-filter");
            if (e->elems.size() != 3 || e->elems[2]->kind != SExprKind::String)
                throw std::runtime_error("PARSE_ERROR: (attr cursor \"attr\")");
            const std::string& cursor = e->elems[1]->text;
            uint8_t idx_v = 0;
            uint16_t dom_id = 0;
            if (cursor == morsel->src_var) {
                idx_v = 8u;
                dom_id = morsel->rel.src_dom;
            } else if (cursor == morsel->tgt_var) {
                idx_v = 9u;
                dom_id = morsel->rel.tgt_dom;
            } else {
                throw std::runtime_error("UNBOUND_SYMBOL: unknown morsel cursor '" + cursor + "'");
            }
            AttrInfo ai = resolve_node_attr(dom_id, e->elems[2]->text);
            uint8_t dst_v = alloc_v();
            b.emit_u16(Opcode::OP_GATHER_NODE_ATTR, subop::kGATHER_NODE_ATTR_VEC, 0,
                       dst_v, idx_v, 0, 0, dom_id, ai.attr_id);
            if (ai.is_decimal()) {
                return TypedVal::vec_decimal(dst_v, dom_id, ai.precision, ai.scale);
            }
            return TypedVal::vec(attr_type_to_vec_bank(ai), dst_v, dom_id);
        }

        if (head == "vec-at") {
            if (!morsel) throw std::runtime_error("PARSE_ERROR: vec-at only valid inside edge-stream");
            TypedVal vec = eval_expr(e->elems[1]);
            const std::string& cursor = e->elems[2]->text;
            int idx_v = (cursor == morsel->src_var) ? 8 : ((cursor == morsel->tgt_var) ? 9 : -1);
            if (idx_v < 0) throw std::runtime_error("UNBOUND_SYMBOL: unknown morsel cursor '" + cursor + "'");
            uint8_t dst_v = alloc_v();
            b.emit(Opcode::OP_GATHER_VEC, subop::kGATHER_VEC_GATHER, 0,
                   dst_v, vec.reg, static_cast<uint8_t>(idx_v), 0, 0);
            return is_decimal_bank(vec.bank)
                ? TypedVal::vec_decimal(dst_v, vec.dom_id, vec.precision, vec.scale)
                : TypedVal::vec(vec.bank, dst_v, vec.dom_id);
        }

        if (head == "reduce") {
            if (e->elems.size() < 3) throw std::runtime_error("PARSE_ERROR: (reduce :kind v [:mask f])");
            const std::string& kind = e->elems[1]->text;
            TypedVal v = eval_expr(e->elems[2]);
            if (!is_vec_bank(v.bank)) throw std::runtime_error("TYPE_MISMATCH: reduce expects vector");
            uint8_t flags = 0;
            uint8_t mask_f = 0;
            if (e->elems.size() >= 5 && e->elems[3]->text == ":mask") {
                TypedVal mf = eval_expr(e->elems[4]);
                if (mf.bank != ValBank::Frontier || mf.dom_id != v.dom_id) {
                    throw std::runtime_error("DOMAIN_MISMATCH: reduce :mask frontier mismatch");
                }
                flags = FLAG_MASKED_S3;
                mask_f = mf.reg;
            }
            if (v.bank == ValBank::VecDecimal) {
                if (kind == ":sum") {
                    uint8_t dst = alloc_r();
                    b.emit(Opcode::OP_VEC_REDUCE, subop::kVEC_REDUCE_SUM_I64_CHK, flags, dst, v.reg, 0, mask_f, 0);
                    return TypedVal::decimal(dst, 18u, v.scale);
                }
                if (kind == ":min" || kind == ":max") {
                    // Exact signed 64-bit integer min/max loop over active valid lanes (no f64 precision loss > 2^53)
                    bool is_min = (kind == ":min");
                    PinGuard pg(pinned_r_mask, pinned_v_mask);
                    pin_v(v);
                    uint8_t r_acc = alloc_pinned_r();
                    uint8_t r_idx = alloc_pinned_r();
                    uint8_t r_len = alloc_pinned_r();
                    uint8_t r_one = alloc_pinned_r();
                    uint8_t r_cur = alloc_pinned_r();
                    uint8_t r_tmp = alloc_pinned_r();

                    b.emit(Opcode::OP_LOAD_CONST, subop::kLOAD_CONST_NULL, 0, r_acc, 0, 0, 0, 0);
                    b.emit_load_const_i64(r_idx, 0LL);
                    b.emit_load_const_i64(r_one, 1LL);
                    b.emit(Opcode::OP_VEC_POINT, subop::kVEC_POINT_VEC_LEN, 0, r_len, v.reg, 0, 0, 0);

                    size_t loop_pc = b.pc();
                    b.emit(Opcode::OP_SCALAR_3VL, subop::kSCALAR_3VL_CMP_LT_I64, 0, r_tmp, r_idx, r_len, 0, 0);
                    size_t jcc_end_pc = b.emit(Opcode::OP_JCC, 7u, 0, 0, r_tmp, 0, 0, 0);

                    bool has_mask = (flags & FLAG_MASKED_S3) != 0u;
                    size_t jcc_skip_mask_pc = 0;
                    if (has_mask) {
                        b.emit(Opcode::OP_SET_CONTAINS, subop::kSET_CONTAINS_CONTAINS_1BIT, 0,
                               r_tmp, mask_f, r_idx, 0, 0);
                        jcc_skip_mask_pc = b.emit(Opcode::OP_JCC, 7u, 0, 0, r_tmp, 0, 0, 0);
                    }

                    b.emit(Opcode::OP_VEC_POINT, subop::kVEC_POINT_VEC_GET, 0, r_cur, v.reg, r_idx, 0, 0);
                    b.emit(Opcode::OP_SCALAR_3VL, subop::kSCALAR_3VL_IS_NULL, 0, r_tmp, r_cur, 0, 0, 0);
                    size_t jcc_skip_null_pc = b.emit(Opcode::OP_JCC, 6u, 0, 0, r_tmp, 0, 0, 0);

                    b.emit(Opcode::OP_SCALAR_3VL, subop::kSCALAR_3VL_IS_NULL, 0, r_tmp, r_acc, 0, 0, 0);
                    size_t jcc_take_first_pc = b.emit(Opcode::OP_JCC, 6u, 0, 0, r_tmp, 0, 0, 0);

                    if (is_min) {
                        b.emit(Opcode::OP_SCALAR_3VL, subop::kSCALAR_3VL_CMP_LT_I64, 0, r_tmp, r_cur, r_acc, 0, 0);
                    } else {
                        b.emit(Opcode::OP_SCALAR_3VL, subop::kSCALAR_3VL_CMP_LT_I64, 0, r_tmp, r_acc, r_cur, 0, 0);
                    }
                    size_t jcc_not_better_pc = b.emit(Opcode::OP_JCC, 7u, 0, 0, r_tmp, 0, 0, 0);

                    size_t take_pc = b.pc();
                    b.patch_imm32(jcc_take_first_pc, static_cast<uint32_t>(take_pc - jcc_take_first_pc));
                    b.emit(Opcode::OP_MOV, subop::kMOV_R, 0, r_acc, r_cur, 0, 0, 0);

                    size_t next_pc = b.pc();
                    if (has_mask) {
                        b.patch_imm32(jcc_skip_mask_pc, static_cast<uint32_t>(next_pc - jcc_skip_mask_pc));
                    }
                    b.patch_imm32(jcc_skip_null_pc, static_cast<uint32_t>(next_pc - jcc_skip_null_pc));
                    b.patch_imm32(jcc_not_better_pc, static_cast<uint32_t>(next_pc - jcc_not_better_pc));
                    b.emit(Opcode::OP_SCALAR_INT_ALU, subop::kSCALAR_INT_ALU_ADD, 0, r_idx, r_idx, r_one, 0, 0);
                    size_t jmp_loop_pc = b.emit(Opcode::OP_JMP, 0, 0, 0, 0, 0, 0, 0);
                    b.patch_imm32(jmp_loop_pc, static_cast<uint32_t>(static_cast<int32_t>(loop_pc) - static_cast<int32_t>(jmp_loop_pc)));

                    size_t end_pc = b.pc();
                    b.patch_imm32(jcc_end_pc, static_cast<uint32_t>(end_pc - jcc_end_pc));
                    return TypedVal::decimal(r_acc, v.precision, v.scale);
                }
                if (kind == ":count-valid") {
                    uint8_t dst = alloc_r();
                    b.emit(Opcode::OP_VEC_REDUCE, subop::kVEC_REDUCE_COUNT_VALID, flags, dst, v.reg, 0, mask_f, 0);
                    return TypedVal::scalar(ValBank::ScalarU64, dst);
                }
                if (kind == ":avg" || kind == ":mean") {
                    uint32_t saved_pin = pinned_r_mask;
                    uint8_t r_sum = alloc_pinned_r();
                    uint8_t r_cnt = alloc_pinned_r();
                    b.emit(Opcode::OP_VEC_REDUCE, subop::kVEC_REDUCE_SUM_I64_CHK, flags, r_sum, v.reg, 0, mask_f, 0);
                    b.emit(Opcode::OP_VEC_REDUCE, subop::kVEC_REDUCE_COUNT_VALID, flags, r_cnt, v.reg, 0, mask_f, 0);
                    uint8_t dst = alloc_r();
                    pinned_r_mask = saved_pin;
                    b.emit(Opcode::OP_SCALAR_DECIMAL_ALU, subop::kSCALAR_DECIMAL_ALU_DIV_SAFE, 0,
                           dst, r_sum, r_cnt, 0, 0);
                    return TypedVal::decimal(dst, 18u, v.scale);
                }
                throw std::runtime_error("TYPE_MISMATCH: unsupported decimal reduction '" + kind + "'");
            }
            uint8_t rsub = subop::kVEC_REDUCE_SUM;
            if (kind == ":sum") rsub = subop::kVEC_REDUCE_SUM;
            else if (kind == ":min") rsub = subop::kVEC_REDUCE_MIN;
            else if (kind == ":max") rsub = subop::kVEC_REDUCE_MAX;
            else if (kind == ":count-valid") rsub = subop::kVEC_REDUCE_COUNT_VALID;
            else if (kind == ":mean" || kind == ":avg") rsub = subop::kVEC_REDUCE_MEAN;
            else throw std::runtime_error("PARSE_ERROR: unknown reduce kind '" + kind + "'");
            uint8_t dst = alloc_r();
            b.emit(Opcode::OP_VEC_REDUCE, rsub, flags, dst, v.reg, 0, mask_f, 0);
            return TypedVal::scalar((kind == ":count-valid") ? ValBank::ScalarU64 : ValBank::ScalarF64, dst);
        }

        if (head == "abs" || head == "neg") {
            if (e->elems.size() != 2) throw std::runtime_error("PARSE_ERROR: unary op requires 1 operand");
            TypedVal v = eval_expr(e->elems[1]);
            if (is_decimal_bank(v.bank)) {
                if (is_scalar_bank(v.bank)) {
                    uint8_t dst = alloc_r();
                    uint8_t sub = (head == "abs") ? subop::kSCALAR_DECIMAL_ALU_ABS_CHK
                                                  : subop::kSCALAR_DECIMAL_ALU_NEG_CHK;
                    b.emit(Opcode::OP_SCALAR_DECIMAL_ALU, sub, 0, dst, v.reg, 0, 0, 0);
                    return TypedVal::decimal(dst, v.precision, v.scale);
                }
                uint8_t dst = alloc_v();
                uint8_t sub = (head == "abs") ? subop::kVEC_DECIMAL_ALU_ABS_CHK
                                              : subop::kVEC_DECIMAL_ALU_NEG_CHK;
                b.emit(Opcode::OP_VEC_DECIMAL_ALU, sub, 0, dst, v.reg, 0, 0, 0);
                return TypedVal::vec_decimal(dst, v.dom_id, v.precision, v.scale);
            }
            if (v.bank == ValBank::ScalarF64) {
                uint8_t dst = alloc_r();
                uint8_t sub = (head == "abs") ? subop::kSCALAR_FP_ALU_ABS : subop::kSCALAR_FP_ALU_NEG;
                b.emit(Opcode::OP_SCALAR_FP_ALU, sub, 0, dst, v.reg, 0, 0, 0);
                return TypedVal::scalar(ValBank::ScalarF64, dst);
            }
            if (v.bank == ValBank::VecF64) {
                uint8_t dst = alloc_v();
                uint8_t sub = (head == "abs") ? subop::kVEC_MATH_ARITH_ABS : subop::kVEC_MATH_ARITH_NEG;
                b.emit(Opcode::OP_VEC_MATH_ARITH, sub, 0, dst, v.reg, 0, 0, 0);
                return TypedVal::vec(ValBank::VecF64, dst, v.dom_id);
            }
            throw std::runtime_error("TYPE_MISMATCH: " + head + " unsupported on " + val_bank_name(v.bank));
        }

        if (head == "log" || head == "exp" || head == "sqrt") {
            if (e->elems.size() != 2) throw std::runtime_error("PARSE_ERROR: unary math requires 1 operand");
            TypedVal v = eval_expr(e->elems[1]);
            if (is_decimal_bank(v.bank)) {
                throw std::runtime_error("TYPE_MISMATCH: transcendental math '" + head +
                                         "' forbids DECIMAL; use (cast expr f64)");
            }
            if (v.bank == ValBank::VecF64) {
                uint8_t dst = alloc_v();
                if (head == "sqrt") {
                    b.emit(Opcode::OP_VEC_MATH_ARITH, subop::kVEC_MATH_ARITH_SQRT, 0, dst, v.reg, 0, 0, 0);
                } else {
                    uint8_t esub = (head == "log") ? subop::kVEC_MATH_EXP_LOG_LOG : subop::kVEC_MATH_EXP_LOG_EXP;
                    b.emit(Opcode::OP_VEC_MATH_EXP_LOG, esub, 0, dst, v.reg, 0, 0, 0);
                }
                return TypedVal::vec(ValBank::VecF64, dst, v.dom_id);
            }
            if (v.bank == ValBank::ScalarF64 && head == "sqrt") {
                uint8_t dst = alloc_r();
                b.emit(Opcode::OP_SCALAR_FP_ALU, subop::kSCALAR_FP_ALU_SQRT, 0, dst, v.reg, 0, 0, 0);
                return TypedVal::scalar(ValBank::ScalarF64, dst);
            }
            throw std::runtime_error("TYPE_MISMATCH: " + head + " requires f64 vector, got " + val_bank_name(v.bank));
        }

        if (head == "+" || head == "-" || head == "*" || head == "/" || head == "safe-div") {
            if (e->elems.size() != 3) throw std::runtime_error("PARSE_ERROR: arithmetic requires 2 operands");
            TypedVal lhs = eval_expr(e->elems[1]);
            TypedVal rhs{};
            {
                PinGuard pg(pinned_r_mask, pinned_v_mask);
                pin_r(lhs);
                pin_v(lhs);
                rhs = eval_expr(e->elems[2]);
            }
            if (is_decimal_bank(lhs.bank) || is_decimal_bank(rhs.bank)) {
                return eval_decimal_binary(head, lhs, rhs);
            }
            if (is_str_bank(lhs.bank) || is_str_bank(rhs.bank) ||
                is_bool_bank(lhs.bank) || is_bool_bank(rhs.bank)) {
                throw std::runtime_error("TYPE_MISMATCH: arithmetic operator '" + head + "' invalid on " +
                                         val_bank_name(lhs.bank) + " and " + val_bank_name(rhs.bank));
            }
            bool li = is_int_bank(lhs.bank);
            bool ri = is_int_bank(rhs.bank);
            bool lf = is_float_bank(lhs.bank);
            bool rf = is_float_bank(rhs.bank);
            if ((li && rf) || (lf && ri)) {
                throw std::runtime_error("TYPE_MISMATCH: implicit int/float mixing forbidden; use (cast ...)");
            }
            if (!is_vec_bank(lhs.bank) && !is_vec_bank(rhs.bank)) {
                uint8_t dst = alloc_r();
                if (li && ri) {
                    uint8_t asub = subop::kSCALAR_INT_ALU_ADD;
                    if (head == "-") asub = subop::kSCALAR_INT_ALU_SUB;
                    else if (head == "*") asub = subop::kSCALAR_INT_ALU_MUL;
                    else if (head == "/" || head == "safe-div") asub = subop::kSCALAR_INT_ALU_DIV_S;
                    b.emit(Opcode::OP_SCALAR_INT_ALU, asub, 0, dst, lhs.reg, rhs.reg, 0, 0);
                    return TypedVal::scalar(ValBank::ScalarI64, dst);
                }
                uint8_t fsub = subop::kSCALAR_FP_ALU_ADD;
                if (head == "-") fsub = subop::kSCALAR_FP_ALU_SUB;
                else if (head == "*") fsub = subop::kSCALAR_FP_ALU_MUL;
                else if (head == "/" || head == "safe-div") fsub = subop::kSCALAR_FP_ALU_DIV;
                b.emit(Opcode::OP_SCALAR_FP_ALU, fsub, 0, dst, lhs.reg, rhs.reg, 0, 0);
                return TypedVal::scalar(ValBank::ScalarF64, dst);
            }
            bool l_vec = is_vec_bank(lhs.bank);
            bool r_vec = is_vec_bank(rhs.bank);
            if (l_vec && r_vec && lhs.dom_id != rhs.dom_id) {
                throw std::runtime_error("DOMAIN_MISMATCH: vector arithmetic domain mismatch");
            }
            uint16_t out_dom = l_vec ? lhs.dom_id : rhs.dom_id;
            uint8_t msub = subop::kVEC_MATH_ARITH_ADD;
            if (head == "-") msub = subop::kVEC_MATH_ARITH_SUB;
            else if (head == "*") msub = subop::kVEC_MATH_ARITH_MUL;
            else if (head == "/" || head == "safe-div") msub = subop::kVEC_MATH_ARITH_DIV;
            ValBank out_bank = (lf || rf) ? ValBank::VecF64 : ValBank::VecI64;
            if (l_vec && r_vec) {
                uint8_t dst = alloc_dst_v(&lhs, &rhs);
                b.emit(Opcode::OP_VEC_MATH_ARITH, msub, 0, dst, lhs.reg, rhs.reg, 0, 0);
                return TypedVal::vec(out_bank, dst, out_dom);
            }
            if (l_vec) {
                uint8_t dst = alloc_dst_v(&lhs, nullptr);
                b.emit(Opcode::OP_VEC_MATH_ARITH, msub, FLAG_SCALAR_RHS, dst, lhs.reg, rhs.reg, 0, 0);
                return TypedVal::vec(out_bank, dst, out_dom);
            }
            // scalar <op> vec
            if (head == "+" || head == "*") {
                uint8_t dst = alloc_dst_v(&rhs, nullptr);
                b.emit(Opcode::OP_VEC_MATH_ARITH, msub, FLAG_SCALAR_RHS, dst, rhs.reg, lhs.reg, 0, 0);
                return TypedVal::vec(out_bank, dst, out_dom);
            }
            // Non-commutative scalar - vec or scalar / vec: broadcast scalar into domain vector
            PinGuard pg_bcast(pinned_r_mask, pinned_v_mask);
            pin_v(rhs);
            uint8_t v_lhs = alloc_pinned_v();
            uint8_t init_sub = (out_bank == ValBank::VecF64) ? subop::kVEC_INIT_DOM_F64 : subop::kVEC_INIT_DOM_I64;
            b.emit_u16(Opcode::OP_VEC_INIT, init_sub, FLAG_SCALAR_RHS, v_lhs, 0, lhs.reg, 0, out_dom, 0);
            uint8_t dst = alloc_dst_v(&rhs, nullptr);
            b.emit(Opcode::OP_VEC_MATH_ARITH, msub, 0, dst, v_lhs, rhs.reg, 0, 0);
            return TypedVal::vec(out_bank, dst, out_dom);
        }

        if (head == "=" || head == "!=" || head == "<" || head == "<=" || head == ">" || head == ">=") {
            if (e->elems.size() != 3) throw std::runtime_error("PARSE_ERROR: comparison requires 2 operands");
            TypedVal lhs = eval_expr(e->elems[1]);
            TypedVal rhs{};
            {
                PinGuard pg(pinned_r_mask, pinned_v_mask);
                pin_r(lhs);
                pin_v(lhs);
                rhs = eval_expr(e->elems[2]);
            }
            return eval_comparison(head, lhs, rhs);
        }

        if (head == "cast") {
            if (e->elems.size() != 3) throw std::runtime_error("PARSE_ERROR: (cast expr T)");
            TypedVal v = eval_expr(e->elems[1]);
            TypeSpec target = parse_scalar_type_spec(e->elems[2]);
            return eval_cast(v, target);
        }

        if (head == "collect") {
            if (e->elems.size() < 4 || e->elems[1]->kind != SExprKind::Keyword || e->elems[2]->kind != SExprKind::String)
                throw std::runtime_error("PARSE_ERROR: (collect :kind \"name\" expr [:mask f])");
            const std::string& kind = e->elems[1]->text;
            const std::string& cname = e->elems[2]->text;
            TypedVal v = eval_expr(e->elems[3]);
            uint16_t slot = output_slot++;
            output_names.push_back(cname);
            std::string ckind = (!kind.empty() && kind[0] == ':') ? kind.substr(1) : kind;
            output_schema.push_back(v.to_output_meta(cname, ckind));
            uint8_t flags = 0;
            uint8_t mask_f = 0;
            if (e->elems.size() >= 6 && e->elems[4]->text == ":mask") {
                TypedVal mf = eval_expr(e->elems[5]);
                flags = FLAG_MASKED_S3;
                mask_f = mf.reg;
            }
            if (kind == ":frontier-bitset") {
                b.emit_u16(Opcode::OP_COLLECT, subop::kCOLLECT_FRONTIER_BITSET, flags, 0, v.reg, 0, mask_f, slot, 0);
            } else if (kind == ":frontier-ids") {
                b.emit_u16(Opcode::OP_COLLECT, subop::kCOLLECT_FRONTIER_IDS, flags, 0, v.reg, 0, mask_f, slot, 0);
            } else if (kind == ":vec") {
                b.emit_u16(Opcode::OP_COLLECT, subop::kCOLLECT_VEC, flags, 0, v.reg, 0, mask_f, slot, 0);
            } else if (kind == ":scalar") {
                b.emit_u16(Opcode::OP_COLLECT, subop::kCOLLECT_SCALAR, flags, 0, v.reg, 0, mask_f, slot, 0);
            } else {
                throw std::runtime_error("PARSE_ERROR: unknown collect kind '" + kind + "'");
            }
            return v;
        }

        throw std::runtime_error("PARSE_ERROR: unsupported ImpScheme v0.11 form '" + head + "'");
    }
};

} // namespace

FoldedDecimalComparison fold_decimal_comparison(
    DecimalCmpOp op,
    uint16_t col_precision,
    uint8_t col_scale,
    int64_t lit_unscaled,
    uint8_t lit_scale
) {
    validate_decimal_precision_scale(col_precision, col_scale);
    if (lit_scale > 18u) {
        throw std::runtime_error("PARSE_ERROR: Literal scale must be in [0, 18], got " +
                                 std::to_string(lit_scale));
    }
    int64_t col_max = kDecimalPow10[col_precision] - 1LL;
    int64_t col_min = -col_max;

    if (lit_scale <= col_scale) {
        uint8_t delta = static_cast<uint8_t>(col_scale - lit_scale);
        int64_t mult = kDecimalPow10[delta];
        if (lit_unscaled > col_max / mult) return fold_above_max(op);
        if (lit_unscaled < col_min / mult) return fold_below_min(op);
        int64_t t = lit_unscaled * mult;
        return fold_exact_at_col_scale(op, t, col_min, col_max);
    }

    uint8_t delta = static_cast<uint8_t>(lit_scale - col_scale);
    int64_t d = kDecimalPow10[delta];
    int64_t q = lit_unscaled / d;
    int64_t r = lit_unscaled % d;
    if (r < 0) {
        r += d;
        q -= 1;
    }

    if (r == 0LL) {
        return fold_exact_at_col_scale(op, q, col_min, col_max);
    }

    // Strictly between q and q + 1 at column scale: q < lit / d < q + 1
    switch (op) {
        case DecimalCmpOp::EQ: return FoldedDecimalComparison::always_false();
        case DecimalCmpOp::NE: return FoldedDecimalComparison::always_true();
        case DecimalCmpOp::GT:
        case DecimalCmpOp::GE: {
            if (q >= col_max) return FoldedDecimalComparison::always_false();
            int64_t bound = q + 1LL;
            if (bound <= col_min) return FoldedDecimalComparison::always_true();
            return FoldedDecimalComparison::compare(DecimalCmpOp::GE, bound);
        }
        case DecimalCmpOp::LT:
        case DecimalCmpOp::LE: {
            if (q < col_min) return FoldedDecimalComparison::always_false();
            if (q >= col_max) return FoldedDecimalComparison::always_true();
            return FoldedDecimalComparison::compare(DecimalCmpOp::LE, q);
        }
    }
    return FoldedDecimalComparison::always_false();
}

int64_t parse_decimal_unscaled_exact(
    const std::string& text,
    uint16_t precision,
    uint8_t scale
) {
    validate_decimal_precision_scale(precision, scale);
    std::string s = trim_ascii(text);
    if (s.empty()) {
        throw std::runtime_error("PARSE_ERROR: Empty DECIMAL literal");
    }
    size_t idx = 0;
    bool neg = false;
    if (s[0] == '-') {
        neg = true;
        ++idx;
    } else if (s[0] == '+') {
        ++idx;
    }
    if (idx >= s.size()) {
        throw std::runtime_error("PARSE_ERROR: Malformed DECIMAL literal: '" + text + "'");
    }
    int64_t acc = 0;
    bool seen_dot = false;
    bool seen_digit = false;
    uint8_t frac_digits = 0;
    int64_t limit = kDecimalPow10[precision] - 1LL;
    for (; idx < s.size(); ++idx) {
        char c = s[idx];
        if (c == '.') {
            if (seen_dot) {
                throw std::runtime_error("PARSE_ERROR: Multiple decimal points in DECIMAL literal: '" + text + "'");
            }
            seen_dot = true;
        } else if (c >= '0' && c <= '9') {
            seen_digit = true;
            if (seen_dot) {
                ++frac_digits;
                if (frac_digits > scale) {
                    throw std::runtime_error("PARSE_ERROR: DECIMAL literal '" + text + "' has " +
                                             std::to_string(frac_digits) + " fractional digits, exceeding scale " +
                                             std::to_string(scale));
                }
            }
            int64_t d = static_cast<int64_t>(c - '0');
            if (acc > (limit - d) / 10LL) {
                throw std::runtime_error("PARSE_ERROR: DECIMAL literal '" + text + "' exceeds DECIMAL(" +
                                         std::to_string(precision) + ", " + std::to_string(scale) + ")");
            }
            acc = acc * 10LL + d;
        } else {
            throw std::runtime_error(std::string("PARSE_ERROR: Invalid character '") + c +
                                     "' in DECIMAL literal: '" + text + "'");
        }
    }
    if (!seen_digit) {
        throw std::runtime_error("PARSE_ERROR: No digits in DECIMAL literal: '" + text + "'");
    }
    uint8_t pad = static_cast<uint8_t>(scale - frac_digits);
    if (pad > 0u) {
        int64_t mult = kDecimalPow10[pad];
        if (acc > limit / mult) {
            throw std::runtime_error("PARSE_ERROR: DECIMAL literal '" + text + "' exceeds DECIMAL(" +
                                     std::to_string(precision) + ", " + std::to_string(scale) + ")");
        }
        acc *= mult;
    }
    return neg ? -acc : acc;
}

std::pair<uint16_t, uint8_t> infer_decimal_precision_scale(const std::string& text) {
    std::string s = trim_ascii(text);
    if (s.empty()) throw std::runtime_error("PARSE_ERROR: Empty DECIMAL literal");
    size_t idx = 0;
    if (s[0] == '-' || s[0] == '+') ++idx;
    if (idx >= s.size()) throw std::runtime_error("PARSE_ERROR: Malformed DECIMAL literal: '" + text + "'");

    bool seen_dot = false;
    bool seen_digit = false;
    bool seen_nonzero_int = false;
    int sig_int_digits = 0;
    int frac_digits = 0;
    for (; idx < s.size(); ++idx) {
        char c = s[idx];
        if (c == '.') {
            if (seen_dot) {
                throw std::runtime_error("PARSE_ERROR: Multiple decimal points in DECIMAL literal: '" + text + "'");
            }
            seen_dot = true;
        } else if (c >= '0' && c <= '9') {
            seen_digit = true;
            if (seen_dot) {
                ++frac_digits;
            } else if (c != '0' || seen_nonzero_int) {
                seen_nonzero_int = true;
                ++sig_int_digits;
            }
        } else {
            throw std::runtime_error(std::string("PARSE_ERROR: Invalid character '") + c +
                                     "' in DECIMAL literal: '" + text + "'");
        }
    }
    if (!seen_digit) throw std::runtime_error("PARSE_ERROR: No digits in DECIMAL literal: '" + text + "'");
    int prec = std::max(1, std::max(sig_int_digits + frac_digits, frac_digits));
    if (prec > 18 || frac_digits > 18) {
        throw std::runtime_error("PARSE_ERROR: DECIMAL literal '" + text + "' exceeds 18 digits");
    }
    uint16_t p = static_cast<uint16_t>(prec);
    uint8_t sc = static_cast<uint8_t>(frac_digits);
    (void)parse_decimal_unscaled_exact(text, p, sc);
    return {p, sc};
}

ImpScmV11CompileResult ImpScmV11Compiler::compile(
    const std::string& source,
    const impulse_snapshot_t* snapshot,
    bool enable_optimizations
) {
    Parser p{source, 0};
    SExprPtr root = p.parse_one();
    if (!root || root->kind != SExprKind::List || root->elems.empty() || root->elems[0]->kind != SExprKind::Symbol) {
        throw std::runtime_error("PARSE_ERROR: expected (query ...)");
    }
    const std::string& root_head = root->elems[0]->text;
    if (root_head != "query") {
        throw std::runtime_error("PARSE_ERROR: expected (query ...) root, got '" + root_head + "'");
    }

    CompilerCtx ctx;
    ctx.snap = snapshot;
    ctx.opt = enable_optimizations;

    for (size_t i = 1; i < root->elems.size(); ++i) {
        const auto& form = root->elems[i];
        if (form->kind == SExprKind::List && !form->elems.empty() &&
            form->elems[0]->kind == SExprKind::Symbol && form->elems[0]->text == "param") {
            if (form->elems.size() != 3 || form->elems[1]->kind != SExprKind::Param) {
                throw std::runtime_error("PARSE_ERROR: malformed (param $name Type)");
            }
            const std::string& pname = form->elems[1]->text;
            uint32_t pid = ctx.next_param_id++;
            ctx.param_map[pname] = pid;
            const auto& ty = form->elems[2];
            if (ty->kind == SExprKind::List && ty->elems.size() == 2 &&
                ty->elems[0]->kind == SExprKind::Symbol && ty->elems[0]->text == "id" &&
                ty->elems[1]->kind == SExprKind::String) {
                uint16_t dom = ctx.resolve_domain(ty->elems[1]->text);
                uint8_t r = ctx.alloc_r();
                ctx.b.emit_u16(Opcode::OP_LOAD_PARAM, subop::kLOAD_PARAM_SCALAR, 0, r, 0, 0, 0,
                               static_cast<uint16_t>(pid), static_cast<uint16_t>(ScalarType::SCALAR_U64));
                ctx.env[pname] = TypedVal::scalar_with_dom(ValBank::ScalarU64, r, dom);
                ctx.param_schema.push_back({pname, pid, "ID", false, 0, 0, dom});
            } else if (ty->kind == SExprKind::List && ty->elems.size() == 2 &&
                       ty->elems[0]->kind == SExprKind::Symbol && ty->elems[0]->text == "frontier" &&
                       ty->elems[1]->kind == SExprKind::String) {
                uint16_t dom = ctx.resolve_domain(ty->elems[1]->text);
                uint8_t f = ctx.alloc_f();
                ctx.b.emit_u16(Opcode::OP_LOAD_PARAM, subop::kLOAD_PARAM_FRONTIER, 0, f, 0, 0, 0,
                               static_cast<uint16_t>(pid), dom);
                ctx.env[pname] = TypedVal::frontier(f, dom);
                ctx.param_schema.push_back({pname, pid, "FRONTIER", false, 0, 0, dom});
            } else if (ty->kind == SExprKind::List && ty->elems.size() == 3 &&
                       ty->elems[0]->kind == SExprKind::Symbol && ty->elems[0]->text == "decimal") {
                TypeSpec ts = CompilerCtx::parse_scalar_type_spec(ty);
                uint8_t r = ctx.alloc_r();
                ctx.b.emit_u16(Opcode::OP_LOAD_PARAM, subop::kLOAD_PARAM_SCALAR, 0, r, 0, 0, 0,
                               static_cast<uint16_t>(pid), static_cast<uint16_t>(ScalarType::SCALAR_I64));
                ctx.env[pname] = TypedVal::decimal(r, ts.precision, ts.scale);
                ctx.param_schema.push_back({pname, pid, "DECIMAL", true, ts.precision, ts.scale, 0xFFFFu});
            } else if (ty->kind == SExprKind::Symbol) {
                uint8_t r = ctx.alloc_r();
                ValBank vb = ValBank::ScalarI64;
                uint16_t stag = static_cast<uint16_t>(ScalarType::SCALAR_I64);
                std::string ptype = "I64";
                if (ty->text == "u64" || ty->text == "u32") {
                    vb = ValBank::ScalarU64;
                    stag = static_cast<uint16_t>(ScalarType::SCALAR_U64);
                    ptype = "U64";
                } else if (ty->text == "f64" || ty->text == "f32") {
                    vb = ValBank::ScalarF64;
                    stag = static_cast<uint16_t>(ScalarType::SCALAR_F64);
                    ptype = "F64";
                } else if (ty->text == "bool") {
                    vb = ValBank::ScalarBool;
                    stag = static_cast<uint16_t>(ScalarType::SCALAR_BOOL_3VL);
                    ptype = "BOOL";
                } else if (ty->text == "str") {
                    vb = ValBank::ScalarStr;
                    stag = static_cast<uint16_t>(ScalarType::SCALAR_STR_HANDLE);
                    ptype = "STR";
                }
                ctx.b.emit_u16(Opcode::OP_LOAD_PARAM, subop::kLOAD_PARAM_SCALAR, 0, r, 0, 0, 0,
                               static_cast<uint16_t>(pid), stag);
                ctx.env[pname] = TypedVal::scalar(vb, r);
                ctx.param_schema.push_back({pname, pid, ptype, false, 0, 0, 0xFFFFu});
            } else {
                throw std::runtime_error("PARSE_ERROR: unsupported param type");
            }
        } else {
            ctx.eval_expr(form);
        }
    }

    if (!enable_optimizations) {
        ctx.b.emit(Opcode::OP_NOP, 0, 0, 0, 0, 0, 0, 0);
    }
    ctx.b.emit(Opcode::OP_HALT, subop::kHALT_OK, 0, 0, 0, 0, 0, 0);

    uint16_t req_doms = snapshot ? static_cast<uint16_t>(snapshot->domains.size()) : 0u;
    ImpScmV11CompileResult res;
    res.code_words = ctx.b.words();
    res.impb_bytes = ctx.b.build_impb(0, 0, req_doms);
    res.param_name_to_id = std::move(ctx.param_map);
    res.param_schema = std::move(ctx.param_schema);
    res.output_names = std::move(ctx.output_names);
    res.output_schema = std::move(ctx.output_schema);

    impulse_vm_validation_v11_t vr = impulse_vm_validate_v11(res.impb_bytes.data(), res.impb_bytes.size());
    if (vr.status != IMPULSE_VM_V11_OK) {
        throw std::runtime_error("VALIDATION_ERROR: emitted invalid v0.11 bytecode at pc=" + std::to_string(vr.bad_pc));
    }
    return res;
}

} // namespace impulse::compiler::v11

extern "C" IMPULSE_API int impulse_compile_impscm_v11(
    const char* impscm_src,
    const impulse_snapshot_t* snapshot,
    int opt_level,
    uint8_t** out_impb,
    size_t* out_len,
    char* err_buf,
    size_t err_cap
) {
    if (!impscm_src || !out_impb || !out_len) return -1;
    *out_impb = nullptr;
    *out_len = 0;
    try {
        auto res = impulse::compiler::v11::ImpScmV11Compiler::compile(impscm_src, snapshot, opt_level != 0);
        auto* buf = static_cast<uint8_t*>(std::malloc(res.impb_bytes.size()));
        if (!buf) return -2;
        std::memcpy(buf, res.impb_bytes.data(), res.impb_bytes.size());
        *out_impb = buf;
        *out_len = res.impb_bytes.size();
        return 0;
    } catch (const std::exception& ex) {
        if (err_buf && err_cap > 0) {
            std::snprintf(err_buf, err_cap, "%s", ex.what());
        }
        return 1;
    }
}

extern "C" IMPULSE_API void impulse_vm_v11_free_bytes(uint8_t* ptr) {
    std::free(ptr);
}
