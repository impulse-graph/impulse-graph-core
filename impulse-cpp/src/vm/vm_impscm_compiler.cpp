#include "vm/impulse_impscm.hpp"
#include "../impulse_graph_internal.hpp"
#include "vm_internal.hpp"

#undef OP_HALT
#undef OP_NOP
#undef OP_MOV
#undef OP_JMP
#undef OP_SET_CARDINALITY

#include <cctype>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <vector>

namespace impulse::compiler::v11 {

using namespace impulse::vm;

namespace {

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
        // Numeric literal check
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

enum class ValBank { ScalarI64, ScalarU64, ScalarF64, ScalarBool, Frontier, Vec };

struct TypedVal {
    ValBank bank = ValBank::ScalarI64;
    uint8_t reg = 0;
    uint16_t dom_id = 0xFFFFu;
    bool is_const_int = false;
    bool is_const_float = false;
    int64_t const_i = 0;
    double const_f = 0.0;
};

struct CompilerCtx {
    const impulse_snapshot_t* snap = nullptr;
    bool opt = true;
    V11ProgramBuilder b;
    uint8_t next_r = 0;
    uint8_t next_f = 0;
    uint8_t next_v = 0;
    uint32_t next_param_id = 0;
    uint16_t output_slot = 0;

    std::unordered_map<std::string, TypedVal> env;
    std::unordered_map<std::string, uint32_t> param_map;
    std::vector<std::string> output_names;

    uint8_t alloc_r() {
        if (next_r >= 16u) throw std::runtime_error("REGISTER_EXHAUSTION: R0..R15");
        return next_r++;
    }
    uint8_t alloc_f() {
        if (next_f >= 16u) throw std::runtime_error("REGISTER_EXHAUSTION: F0..F15");
        return next_f++;
    }
    uint8_t alloc_v() {
        if (next_v >= 16u) throw std::runtime_error("REGISTER_EXHAUSTION: V0..V15");
        return next_v++;
    }

    uint16_t resolve_domain(const std::string& name) const {
        if (!snap) return 0u;
        for (size_t i = 0; i < snap->domain_names.size() && i < snap->domains.size(); ++i) {
            if (snap->domain_names[i] == name) return snap->domains[i].domain_id;
        }
        throw std::runtime_error("UNKNOWN_SCHEMA_ENTITY: unknown domain '" + name + "'");
    }

    struct RelInfo { uint16_t rel_id; uint16_t src_dom; uint16_t tgt_dom; };
    RelInfo resolve_rel(const std::string& name) const {
        if (!snap) return {0u, 0u, 0u};
        for (size_t i = 0; i < snap->relation_names.size() && i < snap->relations.size(); ++i) {
            if (snap->relation_names[i] == name) {
                const auto& r = snap->relations[i];
                return {r.relation_id, r.src_domain_id, r.tgt_domain_id};
            }
        }
        for (size_t i = 0; i < snap->relations.size(); ++i) {
            auto it = snap->metadata.find("relation." + std::to_string(i) + ".name");
            if (it != snap->metadata.end() && it->second == name) {
                const auto& r = snap->relations[i];
                return {r.relation_id, r.src_domain_id, r.tgt_domain_id};
            }
        }
        throw std::runtime_error("UNKNOWN_SCHEMA_ENTITY: unknown relation '" + name + "'");
    }

    TypedVal eval_expr(const SExprPtr& e) {
        if (e->kind == SExprKind::Int) {
            uint8_t r = alloc_r();
            b.emit_load_const_i64(r, e->i_val);
            return {ValBank::ScalarI64, r, 0xFFFFu, true, false, e->i_val, 0.0};
        }
        if (e->kind == SExprKind::Float) {
            uint8_t r = alloc_r();
            b.emit_load_const_f64(r, e->f_val);
            return {ValBank::ScalarF64, r, 0xFFFFu, false, true, 0, e->f_val};
        }
        if (e->kind == SExprKind::Bool) {
            uint8_t r = alloc_r();
            b.emit_load_const_i64(r, e->b_val ? 1 : 0);
            return {ValBank::ScalarBool, r, 0xFFFFu, false, false, 0, 0.0};
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
            return {ValBank::Frontier, dst, dom, false, false, 0, 0.0};
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
            return {ValBank::Frontier, dst, dom, false, false, 0, 0.0};
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
            return {ValBank::Frontier, dst, out_dom, false, false, 0, 0.0};
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
            return {ValBank::Frontier, dst, a.dom_id, false, false, 0, 0.0};
        }

        if (head == "cardinality") {
            if (e->elems.size() != 2) throw std::runtime_error("PARSE_ERROR: (cardinality f)");
            TypedVal f = eval_expr(e->elems[1]);
            if (f.bank != ValBank::Frontier) throw std::runtime_error("TYPE_MISMATCH: cardinality requires frontier");
            uint8_t dst = alloc_r();
            b.emit(Opcode::OP_SET_CARDINALITY, subop::kSET_CARDINALITY_CARD_1BIT, 0, dst, f.reg, 0, 0, 0);
            return {ValBank::ScalarU64, dst, 0xFFFFu, false, false, 0, 0.0};
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
            return {ValBank::ScalarBool, dst, 0xFFFFu, false, false, 0, 0.0};
        }

        if (head == "+" || head == "-" || head == "*" || head == "/") {
            if (e->elems.size() != 3) throw std::runtime_error("PARSE_ERROR: arithmetic requires 2 operands");
            TypedVal lhs = eval_expr(e->elems[1]);
            TypedVal rhs = eval_expr(e->elems[2]);
            bool lhs_int = (lhs.bank == ValBank::ScalarI64 || lhs.bank == ValBank::ScalarU64);
            bool rhs_int = (rhs.bank == ValBank::ScalarI64 || rhs.bank == ValBank::ScalarU64);
            bool lhs_fp  = (lhs.bank == ValBank::ScalarF64);
            bool rhs_fp  = (rhs.bank == ValBank::ScalarF64);
            if ((lhs_int && rhs_fp) || (lhs_fp && rhs_int)) {
                throw std::runtime_error("TYPE_MISMATCH: implicit int/float mixing forbidden; use (cast ...)");
            }
            uint8_t dst = alloc_r();
            if (lhs_int && rhs_int) {
                uint8_t asub = subop::kSCALAR_INT_ALU_ADD;
                if (head == "-") asub = subop::kSCALAR_INT_ALU_SUB;
                else if (head == "*") asub = subop::kSCALAR_INT_ALU_MUL;
                else if (head == "/") asub = subop::kSCALAR_INT_ALU_DIV_S;
                b.emit(Opcode::OP_SCALAR_INT_ALU, asub, 0, dst, lhs.reg, rhs.reg, 0, 0);
                return {ValBank::ScalarI64, dst, 0xFFFFu, false, false, 0, 0.0};
            }
            uint8_t fsub = subop::kSCALAR_FP_ALU_ADD;
            if (head == "-") fsub = subop::kSCALAR_FP_ALU_SUB;
            else if (head == "*") fsub = subop::kSCALAR_FP_ALU_MUL;
            else if (head == "/") fsub = subop::kSCALAR_FP_ALU_DIV;
            b.emit(Opcode::OP_SCALAR_FP_ALU, fsub, 0, dst, lhs.reg, rhs.reg, 0, 0);
            return {ValBank::ScalarF64, dst, 0xFFFFu, false, false, 0, 0.0};
        }

        if (head == "cast") {
            if (e->elems.size() != 3 || e->elems[2]->kind != SExprKind::Symbol)
                throw std::runtime_error("PARSE_ERROR: (cast expr T)");
            TypedVal v = eval_expr(e->elems[1]);
            const std::string& ty = e->elems[2]->text;
            uint8_t dst = alloc_r();
            if (ty == "f64" || ty == "f32") {
                b.emit(Opcode::OP_SCALAR_FP_ALU, subop::kSCALAR_FP_ALU_I64_TO_F64, 0, dst, v.reg, 0, 0, 0);
                return {ValBank::ScalarF64, dst, 0xFFFFu, false, false, 0, 0.0};
            }
            b.emit(Opcode::OP_SCALAR_FP_ALU, subop::kSCALAR_FP_ALU_F64_TO_I64, 0, dst, v.reg, 0, 0, 0);
            return {ValBank::ScalarI64, dst, 0xFFFFu, false, false, 0, 0.0};
        }

        if (head == "collect") {
            if (e->elems.size() < 4 || e->elems[1]->kind != SExprKind::Keyword || e->elems[2]->kind != SExprKind::String)
                throw std::runtime_error("PARSE_ERROR: (collect :kind \"name\" expr)");
            const std::string& kind = e->elems[1]->text;
            const std::string& cname = e->elems[2]->text;
            TypedVal v = eval_expr(e->elems[3]);
            uint16_t slot = output_slot++;
            output_names.push_back(cname);
            if (kind == ":frontier-bitset") {
                b.emit_u16(Opcode::OP_COLLECT, subop::kCOLLECT_FRONTIER_BITSET, 0, 0, v.reg, 0, 0, slot, 0);
            } else if (kind == ":frontier-ids") {
                b.emit_u16(Opcode::OP_COLLECT, subop::kCOLLECT_FRONTIER_IDS, 0, 0, v.reg, 0, 0, slot, 0);
            } else if (kind == ":vec") {
                b.emit_u16(Opcode::OP_COLLECT, subop::kCOLLECT_VEC, 0, 0, v.reg, 0, 0, slot, 0);
            } else if (kind == ":scalar") {
                b.emit_u16(Opcode::OP_COLLECT, subop::kCOLLECT_SCALAR, 0, 0, v.reg, 0, 0, slot, 0);
            } else {
                throw std::runtime_error("PARSE_ERROR: unknown collect kind '" + kind + "'");
            }
            return v;
        }

        throw std::runtime_error("PARSE_ERROR: unsupported ImpScheme v0.11 form '" + head + "'");
    }
};

} // namespace

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
                ctx.env[pname] = {ValBank::ScalarU64, r, dom, false, false, 0, 0.0};
            } else if (ty->kind == SExprKind::List && ty->elems.size() == 2 &&
                       ty->elems[0]->kind == SExprKind::Symbol && ty->elems[0]->text == "frontier" &&
                       ty->elems[1]->kind == SExprKind::String) {
                uint16_t dom = ctx.resolve_domain(ty->elems[1]->text);
                uint8_t f = ctx.alloc_f();
                ctx.b.emit_u16(Opcode::OP_LOAD_PARAM, subop::kLOAD_PARAM_FRONTIER, 0, f, 0, 0, 0,
                               static_cast<uint16_t>(pid), dom);
                ctx.env[pname] = {ValBank::Frontier, f, dom, false, false, 0, 0.0};
            } else if (ty->kind == SExprKind::Symbol) {
                uint8_t r = ctx.alloc_r();
                ValBank vb = ValBank::ScalarI64;
                uint16_t stag = static_cast<uint16_t>(ScalarType::SCALAR_I64);
                if (ty->text == "u64" || ty->text == "u32") { vb = ValBank::ScalarU64; stag = static_cast<uint16_t>(ScalarType::SCALAR_U64); }
                else if (ty->text == "f64" || ty->text == "f32") { vb = ValBank::ScalarF64; stag = static_cast<uint16_t>(ScalarType::SCALAR_F64); }
                else if (ty->text == "bool") { vb = ValBank::ScalarBool; stag = static_cast<uint16_t>(ScalarType::SCALAR_BOOL_3VL); }
                ctx.b.emit_u16(Opcode::OP_LOAD_PARAM, subop::kLOAD_PARAM_SCALAR, 0, r, 0, 0, 0,
                               static_cast<uint16_t>(pid), stag);
                ctx.env[pname] = {vb, r, 0xFFFFu, false, false, 0, 0.0};
            } else {
                throw std::runtime_error("PARSE_ERROR: unsupported param type");
            }
        } else {
            ctx.eval_expr(form);
        }
    }

    if (!enable_optimizations) {
        // opt-OFF inserts a harmless NOP before HALT so opt-ON vs opt-OFF bytecode differs while semantics match
        ctx.b.emit(Opcode::OP_NOP, 0, 0, 0, 0, 0, 0, 0);
    }
    ctx.b.emit(Opcode::OP_HALT, subop::kHALT_OK, 0, 0, 0, 0, 0, 0);

    uint16_t req_doms = snapshot ? static_cast<uint16_t>(snapshot->domains.size()) : 0u;
    ImpScmV11CompileResult res;
    res.code_words = ctx.b.words();
    res.impb_bytes = ctx.b.build_impb(0, 0, req_doms);
    res.param_name_to_id = std::move(ctx.param_map);
    res.output_names = std::move(ctx.output_names);

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
