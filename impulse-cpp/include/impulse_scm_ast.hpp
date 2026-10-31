/**
 * @file impulse_scm_ast.hpp
 * @brief Homoiconic ImpScheme (`.impscm`) S-Expression AST Node Definitions & Graph Catalog.
 */

#ifndef IMPULSE_SCM_AST_HPP
#define IMPULSE_SCM_AST_HPP

#include "impulse_vm.h"
#include "impulse_graph.h"
#include "impulse_cel.h"
#include "impulse_assert.h"

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <sstream>
#include <iomanip>
#include <iostream>
#include <cstdint>
#include <algorithm>
#include <functional>

namespace impulse::compiler {

// ---------------------------------------------------------------------------
// Compiler Configuration & Knobs
// ---------------------------------------------------------------------------
struct CompilerOptions {
    bool enable_constant_folding{true};
    bool enable_kernel_fusion{true};
    bool enable_direction_selection{true};
    bool enable_register_ping_pong{true};
    bool enable_seed_inlining{true};
    bool enable_early_exit{true};
    bool enable_tracing{false};
    uint32_t disabled_passes_mask{0};

    double fused_2hop_max_multiplicity_threshold{1.5};
    size_t simd_predicate_eval_min_degree_threshold{64};

    static CompilerOptions default_options() {
        return CompilerOptions{};
    }

    static CompilerOptions unoptimized() {
        CompilerOptions opts;
        opts.enable_constant_folding = false;
        opts.enable_kernel_fusion = false;
        opts.enable_direction_selection = false;
        opts.enable_register_ping_pong = false;
        opts.enable_seed_inlining = false;
        opts.enable_early_exit = false;
        opts.disabled_passes_mask = IMPULSE_OPT_ALL_DISABLED;
        return opts;
    }

    static CompilerOptions from_disable_mask(uint32_t mask) {
        CompilerOptions opts;
        opts.disabled_passes_mask = mask;
        if (mask & IMPULSE_OPT_CONSTANT_FOLDING)    opts.enable_constant_folding = false;
        if (mask & IMPULSE_OPT_KERNEL_FUSION_2HOP)  opts.enable_kernel_fusion = false;
        if (mask & IMPULSE_OPT_DIRECTION_SELECTION) opts.enable_direction_selection = false;
        if (mask & IMPULSE_OPT_REGISTER_PING_PONG)  opts.enable_register_ping_pong = false;
        if (mask & IMPULSE_OPT_SEED_INLINING)       opts.enable_seed_inlining = false;
        if (mask & IMPULSE_OPT_EARLY_EXIT)          opts.enable_early_exit = false;
        return opts;
    }

    [[nodiscard]] CompilerOptions effective() const {
        CompilerOptions out = *this;
        uint32_t mask = disabled_passes_mask | impulse_test_get_opt_disable_mask();
        out.disabled_passes_mask = mask;
        if (mask & IMPULSE_OPT_CONSTANT_FOLDING)    out.enable_constant_folding = false;
        if (mask & IMPULSE_OPT_KERNEL_FUSION_2HOP)  out.enable_kernel_fusion = false;
        if (mask & IMPULSE_OPT_DIRECTION_SELECTION) out.enable_direction_selection = false;
        if (mask & IMPULSE_OPT_REGISTER_PING_PONG)  out.enable_register_ping_pong = false;
        if (mask & IMPULSE_OPT_SEED_INLINING)       out.enable_seed_inlining = false;
        if (mask & IMPULSE_OPT_EARLY_EXIT)          out.enable_early_exit = false;
        return out;
    }
};

// ---------------------------------------------------------------------------
// AST Enums & Kinds
// ---------------------------------------------------------------------------
enum class NodeKind {
    PROGRAM,
    WALK,
    WALK_2HOP,
    VECTOR_FILTER,
    CEL_EXPR,
    REDUCE,
    COLLECT,
    LET,
    SET,
    LOOP_WHILE,
    RETURN,
    CARDINALITY,
    BITSET_INIT,
    VAR_REF,
    SET_OP,
    LITERAL,
    SYMBOL,
    LIST,
    STREAM_FILTER,
    STREAM_PROJECT,
    LANE_INIT_SEEDS,
    LANE_INIT_GROUPS,
    LANE_SET_EDGE_MASK,
    LANE_WALK,
    LANE_PRUNE_MASK,
    LANE_FILTER_QUORUM,
    LANE_REDUCE_AND,
    LANE_REDUCE_ATTR,
    LANE_SET_COVER,
    VECTOR_ANN_SEARCH,
    VECTOR_RERANK_DOT,
    FIXPOINT
};

enum class WalkDirection {
    FORWARD_CSR,
    REVERSE_CSC
};

enum class CompareOp {
    EQ,
    NEQ,
    LT,
    LTE,
    GT,
    GTE
};

inline const char* compare_op_to_str(CompareOp op) {
    switch (op) {
        case CompareOp::EQ: return "vec-cmp-eq";
        case CompareOp::NEQ: return "vec-cmp-neq";
        case CompareOp::LT: return "vec-cmp-lt";
        case CompareOp::LTE: return "vec-cmp-lte";
        case CompareOp::GT: return "vec-cmp-gt";
        case CompareOp::GTE: return "vec-cmp-gte";
    }
    return "vec-cmp";
}

// ---------------------------------------------------------------------------
// Base AST Node
// ---------------------------------------------------------------------------
class ImpScmNode {
public:
    virtual ~ImpScmNode() = default;
    [[nodiscard]] virtual NodeKind kind() const = 0;
    [[nodiscard]] virtual std::string to_scm_string() const = 0;
};

using AstPtr = std::shared_ptr<ImpScmNode>;

// ---------------------------------------------------------------------------
// Literal & Symbol AST Nodes
// ---------------------------------------------------------------------------
class ScmLiteral : public ImpScmNode {
public:
    enum class LitType { INT, FLOAT, BOOL, STRING };

    LitType type;
    int64_t int_val{0};
    double float_val{0.0};
    bool bool_val{false};
    std::string str_val;

    static std::shared_ptr<ScmLiteral> of_int(int64_t v) {
        auto n = std::make_shared<ScmLiteral>();
        n->type = LitType::INT;
        n->int_val = v;
        return n;
    }

    static std::shared_ptr<ScmLiteral> of_float(double v) {
        auto n = std::make_shared<ScmLiteral>();
        n->type = LitType::FLOAT;
        n->float_val = v;
        return n;
    }

    static std::shared_ptr<ScmLiteral> of_bool(bool v) {
        auto n = std::make_shared<ScmLiteral>();
        n->type = LitType::BOOL;
        n->bool_val = v;
        return n;
    }

    static std::shared_ptr<ScmLiteral> of_str(std::string v) {
        auto n = std::make_shared<ScmLiteral>();
        n->type = LitType::STRING;
        n->str_val = std::move(v);
        return n;
    }

    [[nodiscard]] NodeKind kind() const override { return NodeKind::LITERAL; }

    [[nodiscard]] std::string to_scm_string() const override {
        switch (type) {
            case LitType::INT: return std::to_string(int_val);
            case LitType::FLOAT: {
                std::ostringstream ss;
                ss << float_val;
                if (ss.str().find('.') == std::string::npos) ss << ".0";
                return ss.str();
            }
            case LitType::BOOL: return bool_val ? "#t" : "#f";
            case LitType::STRING: return "\"" + str_val + "\"";
        }
        return "";
    }
};

class ScmSymbol : public ImpScmNode {
public:
    std::string name;

    explicit ScmSymbol(std::string n) : name(std::move(n)) {}

    [[nodiscard]] NodeKind kind() const override { return NodeKind::SYMBOL; }
    [[nodiscard]] std::string to_scm_string() const override { return name; }
};

class ScmList : public ImpScmNode {
public:
    std::vector<AstPtr> elements;
    
    explicit ScmList(std::vector<AstPtr> elems) : elements(std::move(elems)) {}
    
    [[nodiscard]] NodeKind kind() const override { return NodeKind::LIST; }
    [[nodiscard]] std::string to_scm_string() const override { return "(list)"; }
};

class ScmReduce : public ImpScmNode {
public:
    enum class Op { SUM, COUNT, MIN, MAX, ARGMIN, ARGMAX, FIRST };
    Op op_val;
    
    explicit ScmReduce(Op o) : op_val(o) {}
    
    [[nodiscard]] NodeKind kind() const override { return NodeKind::REDUCE; }
    [[nodiscard]] std::string to_scm_string() const override { return "(reduce)"; }
};

class ScmStreamFilter : public ImpScmNode {
public:
    AstPtr predicate;
    explicit ScmStreamFilter(AstPtr pred) : predicate(std::move(pred)) {}
    [[nodiscard]] NodeKind kind() const override { return NodeKind::STREAM_FILTER; }
    [[nodiscard]] std::string to_scm_string() const override { return "(stream-filter)"; }
};

class ScmStreamProject : public ImpScmNode {
public:
    AstPtr expr;
    explicit ScmStreamProject(AstPtr e) : expr(std::move(e)) {}
    [[nodiscard]] NodeKind kind() const override { return NodeKind::STREAM_PROJECT; }
    [[nodiscard]] std::string to_scm_string() const override { return "(stream-project)"; }
};

// ---------------------------------------------------------------------------
// Vector Filter & CEL Expression AST Nodes
// ---------------------------------------------------------------------------
class ScmVectorFilter : public ImpScmNode {
public:
    std::string attribute_name;
    CompareOp op{CompareOp::GTE};
    AstPtr threshold;
    int attribute_id{-1};

    ScmVectorFilter(std::string attr, CompareOp o, AstPtr thresh)
        : attribute_name(std::move(attr)), op(o), threshold(std::move(thresh)) {}

    [[nodiscard]] NodeKind kind() const override { return NodeKind::VECTOR_FILTER; }

    [[nodiscard]] std::string to_scm_string() const override {
        std::ostringstream ss;
        ss << "(" << compare_op_to_str(op) << " (get-attr edge \"" << attribute_name << "\") ";
        if (threshold) ss << threshold->to_scm_string();
        else ss << "nil";
        ss << ")";
        return ss.str();
    }
};

class ScmCelExpr : public ImpScmNode {
public:
    std::string cel_source;
    int filter_id{-1};

    explicit ScmCelExpr(std::string src) : cel_source(std::move(src)) {}

    [[nodiscard]] NodeKind kind() const override { return NodeKind::CEL_EXPR; }
    [[nodiscard]] std::string to_scm_string() const override {
        return "(cel-eval \"" + cel_source + "\")";
    }
};

// ---------------------------------------------------------------------------
// Walk AST Nodes
// ---------------------------------------------------------------------------
class ScmWalk : public ImpScmNode {
public:
    WalkDirection direction{WalkDirection::FORWARD_CSR};
    std::string relation_name;
    int physical_rel_id{-1};
    AstPtr predicate;

    uint16_t src_reg{0};
    uint16_t dst_reg{1};
    bool inlined_seed{false};
    bool halt_on_empty{false};
    bool is_adaptive{false};
    std::vector<AstPtr> shader_steps;

    static std::shared_ptr<ScmWalk> forward(std::string rel, AstPtr pred = nullptr) {
        auto n = std::make_shared<ScmWalk>();
        n->direction = WalkDirection::FORWARD_CSR;
        n->relation_name = std::move(rel);
        n->predicate = std::move(pred);
        return n;
    }

    static std::shared_ptr<ScmWalk> reverse(std::string rel, AstPtr pred = nullptr) {
        auto n = std::make_shared<ScmWalk>();
        n->direction = WalkDirection::REVERSE_CSC;
        n->relation_name = std::move(rel);
        n->predicate = std::move(pred);
        return n;
    }

    [[nodiscard]] NodeKind kind() const override { return NodeKind::WALK; }

    [[nodiscard]] std::string to_scm_string() const override {
        std::ostringstream ss;
        ss << "(" << (direction == WalkDirection::FORWARD_CSR ? "csr-walk " : "csc-walk ");
        if (physical_rel_id >= 0) {
            ss << physical_rel_id;
        } else {
            ss << "\"" << relation_name << "\"";
        }
        if (predicate) {
            ss << " " << predicate->to_scm_string();
        }
        ss << ")";
        return ss.str();
    }
};

class ScmWalk2Hop : public ImpScmNode {
public:
    std::string rel1_name;
    std::string rel2_name;
    int rel1_id{-1};
    int rel2_id{-1};

    uint16_t src_reg{0};
    uint16_t dst_reg{1};
    bool inlined_seed{false};
    bool halt_on_empty{false};

    ScmWalk2Hop(std::string r1, std::string r2)
        : rel1_name(std::move(r1)), rel2_name(std::move(r2)) {}

    [[nodiscard]] NodeKind kind() const override { return NodeKind::WALK_2HOP; }

    [[nodiscard]] std::string to_scm_string() const override {
        std::ostringstream ss;
        ss << "(csr-walk-2hop ";
        if (rel1_id >= 0 && rel2_id >= 0) {
            ss << rel1_id << " " << rel2_id;
        } else {
            ss << "\"" << rel1_name << "\" \"" << rel2_name << "\"";
        }
        ss << ")";
        return ss.str();
    }
};

// ---------------------------------------------------------------------------
// Multi-Lane (0xC0..0xC9) & Hybrid Vector Search (0xCA..0xCB) AST Nodes
// ---------------------------------------------------------------------------
class ScmLaneInitSeeds : public ImpScmNode {
public:
    uint16_t seed_reg{0};
    uint8_t width_flag{0};
    uint16_t dst_reg{0};
    std::vector<uint64_t> inline_seeds;

    explicit ScmLaneInitSeeds(uint16_t sreg = 0, uint8_t wflag = 0)
        : seed_reg(sreg), width_flag(wflag) {}

    static std::shared_ptr<ScmLaneInitSeeds> from_inline_seeds(
        std::vector<uint64_t> seeds, uint16_t sreg = 0, uint8_t wflag = 0)
    {
        auto n = std::make_shared<ScmLaneInitSeeds>(sreg, wflag);
        n->inline_seeds = std::move(seeds);
        return n;
    }

    [[nodiscard]] NodeKind kind() const override { return NodeKind::LANE_INIT_SEEDS; }
    [[nodiscard]] std::string to_scm_string() const override {
        return "(lane-init-seeds " + std::to_string(seed_reg) + " " + std::to_string(width_flag) + ")";
    }
};

class ScmLaneInitGroups : public ImpScmNode {
public:
    uint16_t pairs_reg{0};
    uint16_t num_groups{1};
    uint16_t dst_reg{0};
    std::vector<uint64_t> inline_pairs;

    ScmLaneInitGroups(uint16_t preg, uint16_t ngroups)
        : pairs_reg(preg), num_groups(ngroups) {}

    static std::shared_ptr<ScmLaneInitGroups> from_inline_pairs(
        std::vector<uint64_t> pairs, uint16_t ngroups, uint16_t preg = 0)
    {
        auto n = std::make_shared<ScmLaneInitGroups>(preg, ngroups);
        n->inline_pairs = std::move(pairs);
        return n;
    }

    [[nodiscard]] NodeKind kind() const override { return NodeKind::LANE_INIT_GROUPS; }
    [[nodiscard]] std::string to_scm_string() const override {
        return "(lane-init-groups " + std::to_string(pairs_reg) + " " + std::to_string(num_groups) + ")";
    }
};

class ScmLaneSetEdgeMask : public ImpScmNode {
public:
    uint16_t pairs_reg{0};
    std::string relation_name;
    int physical_rel_id{-1};
    uint16_t src_reg{0};
    uint16_t dst_reg{0};

    ScmLaneSetEdgeMask(uint16_t preg, std::string rel, int rel_id = -1)
        : pairs_reg(preg), relation_name(std::move(rel)), physical_rel_id(rel_id) {}

    [[nodiscard]] NodeKind kind() const override { return NodeKind::LANE_SET_EDGE_MASK; }
    [[nodiscard]] std::string to_scm_string() const override {
        std::ostringstream ss;
        ss << "(lane-set-edge-mask " << pairs_reg << " ";
        if (physical_rel_id >= 0) ss << physical_rel_id;
        else ss << "\"" << relation_name << "\"";
        ss << ")";
        return ss.str();
    }
};

class ScmLaneWalk : public ImpScmNode {
public:
    WalkDirection direction{WalkDirection::FORWARD_CSR};
    std::string relation_name;
    int physical_rel_id{-1};
    bool fixed_point{false};
    uint8_t flags{0};
    uint16_t src_reg{0};
    uint16_t dst_reg{0};

    ScmLaneWalk() = default;

    ScmLaneWalk(std::string rel, bool fp, uint8_t flg = 0x04)
        : direction(WalkDirection::FORWARD_CSR),
          relation_name(std::move(rel)),
          fixed_point(fp),
          flags(fp ? (flg != 0 ? flg : 0x04) : flg) {}

    ScmLaneWalk(std::string rel, int rel_id, WalkDirection dir, bool fp = false, uint8_t flg = 0)
        : direction(dir),
          relation_name(std::move(rel)),
          physical_rel_id(rel_id),
          fixed_point(fp),
          flags(fp ? (flg != 0 ? flg : 0x04) : flg) {}

    static std::shared_ptr<ScmLaneWalk> forward(std::string rel, bool fp = false, uint8_t flg = 0) {
        auto n = std::make_shared<ScmLaneWalk>();
        n->direction = WalkDirection::FORWARD_CSR;
        n->relation_name = std::move(rel);
        n->fixed_point = fp;
        n->flags = fp ? (flg != 0 ? flg : 0x04) : flg;
        return n;
    }

    static std::shared_ptr<ScmLaneWalk> reverse(std::string rel, bool fp = false, uint8_t flg = 0) {
        auto n = std::make_shared<ScmLaneWalk>();
        n->direction = WalkDirection::REVERSE_CSC;
        n->relation_name = std::move(rel);
        n->fixed_point = fp;
        n->flags = fp ? (flg != 0 ? flg : 0x04) : flg;
        return n;
    }

    [[nodiscard]] NodeKind kind() const override { return NodeKind::LANE_WALK; }
    [[nodiscard]] std::string to_scm_string() const override {
        std::ostringstream ss;
        ss << "(" << (direction == WalkDirection::FORWARD_CSR ? "lane-walk-csr " : "lane-walk-csc ");
        if (physical_rel_id >= 0) ss << physical_rel_id;
        else ss << "\"" << relation_name << "\"";
        if (fixed_point) ss << " 1";
        ss << ")";
        return ss.str();
    }
};

class ScmLanePruneMask : public ImpScmNode {
public:
    uint8_t mode{0};
    uint16_t arg0{0};
    uint16_t arg1{0};
    uint16_t src_reg{0};
    uint16_t dst_reg{0};
    std::vector<uint64_t> inline_deny_nodes;

    ScmLanePruneMask(uint8_t m, uint16_t a0, uint16_t a1 = 0)
        : mode(m), arg0(a0), arg1(a1) {}

    static std::shared_ptr<ScmLanePruneMask> from_register(uint16_t mask_reg = 0, uint8_t m = 0) {
        return std::make_shared<ScmLanePruneMask>(m, mask_reg, 0);
    }

    static std::shared_ptr<ScmLanePruneMask> from_inline_deny(
        std::vector<uint64_t> deny_nodes, uint16_t mask_reg = 0)
    {
        auto n = std::make_shared<ScmLanePruneMask>(0, mask_reg, 0);
        n->inline_deny_nodes = std::move(deny_nodes);
        return n;
    }

    static std::shared_ptr<ScmLanePruneMask> allow_deny_split(uint16_t half_lanes) {
        return std::make_shared<ScmLanePruneMask>(1, half_lanes, 0);
    }

    [[nodiscard]] NodeKind kind() const override { return NodeKind::LANE_PRUNE_MASK; }
    [[nodiscard]] std::string to_scm_string() const override {
        return "(lane-prune-mask " + std::to_string(mode) + " " + std::to_string(arg0) + " " + std::to_string(arg1) + ")";
    }
};

class ScmLaneFilterQuorum : public ImpScmNode {
public:
    uint16_t min_quorum{1};
    uint16_t mask_reg{0xFF};
    uint16_t src_reg{0};
    uint16_t dst_reg{0};

    explicit ScmLaneFilterQuorum(uint16_t mq, uint16_t mreg = 0xFF)
        : min_quorum(mq), mask_reg(mreg) {}

    [[nodiscard]] NodeKind kind() const override { return NodeKind::LANE_FILTER_QUORUM; }
    [[nodiscard]] std::string to_scm_string() const override {
        return "(lane-filter-quorum " + std::to_string(min_quorum) + " " + std::to_string(mask_reg) + ")";
    }
};

class ScmLaneReduceAnd : public ImpScmNode {
public:
    uint16_t src_reg{0};
    uint16_t dst_reg{0};

    [[nodiscard]] NodeKind kind() const override { return NodeKind::LANE_REDUCE_AND; }
    [[nodiscard]] std::string to_scm_string() const override {
        return "(lane-reduce-and)";
    }
};

class ScmLaneReduceAttr : public ImpScmNode {
public:
    uint16_t attr_id{0};
    uint8_t reduce_op{0};
    uint16_t src_reg{0};
    uint16_t dst_reg{0};

    ScmLaneReduceAttr(uint16_t aid, uint8_t op)
        : attr_id(aid), reduce_op(op) {}

    [[nodiscard]] NodeKind kind() const override { return NodeKind::LANE_REDUCE_ATTR; }
    [[nodiscard]] std::string to_scm_string() const override {
        return "(lane-reduce-attr " + std::to_string(attr_id) + " " + std::to_string(reduce_op) + ")";
    }
};

class ScmLaneSetCover : public ImpScmNode {
public:
    uint16_t mask_reg{0xFF};
    uint8_t mode{0};
    uint16_t src_reg{0};
    uint16_t dst_reg{0};

    explicit ScmLaneSetCover(uint16_t mreg = 0xFF, uint8_t m = 0)
        : mask_reg(mreg), mode(m) {}

    [[nodiscard]] NodeKind kind() const override { return NodeKind::LANE_SET_COVER; }
    [[nodiscard]] std::string to_scm_string() const override {
        return "(lane-set-cover " + std::to_string(mask_reg) + " " + std::to_string(mode) + ")";
    }
};

class ScmVectorAnnSearch : public ImpScmNode {
public:
    uint16_t query_reg{0};
    uint16_t top_k{1};
    uint16_t index_or_attr_id{0};
    bool fused_lane_frontier{false};
    uint16_t dst_reg{0};

    ScmVectorAnnSearch(uint16_t qreg, uint16_t k, uint16_t idx_id = 0, bool fused = false)
        : query_reg(qreg), top_k(k), index_or_attr_id(idx_id), fused_lane_frontier(fused) {}

    [[nodiscard]] NodeKind kind() const override { return NodeKind::VECTOR_ANN_SEARCH; }
    [[nodiscard]] std::string to_scm_string() const override {
        return "(vector-ann-search " + std::to_string(query_reg) + " " + std::to_string(top_k) + " " +
               std::to_string(index_or_attr_id) + " " + (fused_lane_frontier ? "1" : "0") + ")";
    }
};

class ScmVectorRerankDot : public ImpScmNode {
public:
    uint16_t query_reg{0};
    uint16_t attr_id{0};
    uint8_t metric{0};
    uint16_t src_reg{0};
    uint16_t dst_reg{0};

    ScmVectorRerankDot(uint16_t qreg, uint16_t aid = 0, uint8_t met = 0)
        : query_reg(qreg), attr_id(aid), metric(met) {}

    [[nodiscard]] NodeKind kind() const override { return NodeKind::VECTOR_RERANK_DOT; }
    [[nodiscard]] std::string to_scm_string() const override {
        return "(vector-rerank-dot " + std::to_string(query_reg) + " " +
               std::to_string(attr_id) + " " + std::to_string(metric) + ")";
    }
};

// ---------------------------------------------------------------------------
// Collect & Program AST Nodes
// ---------------------------------------------------------------------------
class ScmCollect : public ImpScmNode {
public:
    enum class OutputType { BITSET, COUNT, SCALAR, VECTOR, LIST };
    OutputType output_type{OutputType::BITSET};
    uint16_t target_reg{0};

    static std::shared_ptr<ScmCollect> bitset(uint16_t reg = 0) {
        auto n = std::make_shared<ScmCollect>();
        n->output_type = OutputType::BITSET;
        n->target_reg = reg;
        return n;
    }

    static std::shared_ptr<ScmCollect> count(uint16_t reg = 0) {
        auto n = std::make_shared<ScmCollect>();
        n->output_type = OutputType::COUNT;
        n->target_reg = reg;
        return n;
    }

    [[nodiscard]] NodeKind kind() const override { return NodeKind::COLLECT; }

    [[nodiscard]] std::string to_scm_string() const override {
        switch (output_type) {
            case OutputType::BITSET: return "(collect-bitset)";
            case OutputType::COUNT: return "(collect-count)";
            case OutputType::SCALAR: return "(collect-scalar)";
            case OutputType::VECTOR: return "(collect-vector)";
            case OutputType::LIST: return "(collect-list)";
        }
        return "(collect)";
    }
};

class ScmLet : public ImpScmNode {
public:
    std::vector<std::string> vars;
    std::vector<AstPtr> inits;
    std::vector<AstPtr> body;
    NodeKind kind() const override { return NodeKind::LET; }
    std::string to_scm_string() const override { return "(let)"; }
};

class ScmSet : public ImpScmNode {
public:
    std::string var;
    AstPtr expr;
    NodeKind kind() const override { return NodeKind::SET; }
    std::string to_scm_string() const override { return "(set!)"; }
};

class ScmLoopWhile : public ImpScmNode {
public:
    AstPtr condition;
    std::vector<AstPtr> body;
    NodeKind kind() const override { return NodeKind::LOOP_WHILE; }
    std::string to_scm_string() const override { return "(loop-while)"; }
};

class ScmReturn : public ImpScmNode {
public:
    AstPtr expr;
    NodeKind kind() const override { return NodeKind::RETURN; }
    std::string to_scm_string() const override { return "(return)"; }
};

class ScmCardinality : public ImpScmNode {
public:
    std::string var;
    NodeKind kind() const override { return NodeKind::CARDINALITY; }
    std::string to_scm_string() const override { return "(bitset:cardinality)"; }
};

class ScmBitsetInit : public ImpScmNode {
public:
    enum class InitType { EMPTY, ALL, FROM_NODE };
    InitType type;
    std::string param;
    NodeKind kind() const override { return NodeKind::BITSET_INIT; }
    std::string to_scm_string() const override { return "(bitset-init)"; }
};

class ScmVarRef : public ImpScmNode {
public:
    std::string var;
    NodeKind kind() const override { return NodeKind::VAR_REF; }
    std::string to_scm_string() const override { return var; }
};

class ScmSetOp : public ImpScmNode {
public:
    enum Op { UNION, DIFFERENCE, INTERSECT };
    Op op{UNION};
    AstPtr lhs;
    AstPtr rhs;

    ScmSetOp() = default;
    ScmSetOp(Op o, AstPtr l, AstPtr r) : op(o), lhs(std::move(l)), rhs(std::move(r)) {}

    static std::shared_ptr<ScmSetOp> diff(AstPtr l, AstPtr r) {
        return std::make_shared<ScmSetOp>(DIFFERENCE, std::move(l), std::move(r));
    }

    static std::shared_ptr<ScmSetOp> union_op(AstPtr l, AstPtr r) {
        return std::make_shared<ScmSetOp>(UNION, std::move(l), std::move(r));
    }

    static std::shared_ptr<ScmSetOp> intersect(AstPtr l, AstPtr r) {
        return std::make_shared<ScmSetOp>(INTERSECT, std::move(l), std::move(r));
    }

    NodeKind kind() const override { return NodeKind::SET_OP; }
    std::string to_scm_string() const override {
        const char* op_name = (op == DIFFERENCE) ? "set:difference" : (op == UNION ? "set:union" : "set:intersect");
        std::ostringstream ss;
        ss << "(" << op_name << " "
           << (lhs ? lhs->to_scm_string() : "nil") << " "
           << (rhs ? rhs->to_scm_string() : "nil") << ")";
        return ss.str();
    }
};

class ScmProgram : public ImpScmNode {
public:
    std::vector<AstPtr> steps;

    explicit ScmProgram(std::vector<AstPtr> s) : steps(std::move(s)) {}

    template<typename... Args>
    static std::shared_ptr<ScmProgram> of(Args&&... args) {
        std::vector<AstPtr> vec = { std::forward<Args>(args)... };
        return std::make_shared<ScmProgram>(std::move(vec));
    }

    [[nodiscard]] NodeKind kind() const override { return NodeKind::PROGRAM; }

    [[nodiscard]] std::string to_scm_string() const override {
        std::ostringstream ss;
        ss << "(program";
        for (const auto& step : steps) {
            ss << "\n  " << step->to_scm_string();
        }
        ss << ")";
        return ss.str();
    }
};

class ScmFixpoint : public ImpScmNode {
public:
    std::string relation_name;
    int physical_rel_id{-1};
    WalkDirection direction{WalkDirection::FORWARD_CSR};
    int max_iterations{100};
    std::vector<AstPtr> body_steps;
    uint16_t src_reg{0};
    uint16_t dst_reg{1};

    ScmFixpoint() = default;

    ScmFixpoint(std::string rel, WalkDirection dir = WalkDirection::FORWARD_CSR, int max_iter = 100)
        : relation_name(std::move(rel)), direction(dir), max_iterations(max_iter > 0 ? max_iter : 100) {}

    explicit ScmFixpoint(const std::shared_ptr<ScmProgram>& body) {
        if (body) {
            body_steps = body->steps;
            for (const auto& st : body->steps) {
                if (st && st->kind() == NodeKind::WALK) {
                    auto w = std::static_pointer_cast<ScmWalk>(st);
                    relation_name = w->relation_name;
                    physical_rel_id = w->physical_rel_id;
                    direction = w->direction;
                    break;
                }
            }
        }
    }

    static std::shared_ptr<ScmFixpoint> of_relation(
        std::string rel, WalkDirection dir = WalkDirection::FORWARD_CSR, int max_iter = 100)
    {
        return std::make_shared<ScmFixpoint>(std::move(rel), dir, max_iter);
    }

    [[nodiscard]] NodeKind kind() const override { return NodeKind::FIXPOINT; }

    [[nodiscard]] std::string to_scm_string() const override {
        std::ostringstream ss;
        ss << "(fixpoint ";
        if (physical_rel_id >= 0) ss << physical_rel_id;
        else ss << "\"" << relation_name << "\"";
        ss << " " << max_iterations;
        if (direction == WalkDirection::REVERSE_CSC) ss << " :csc";
        for (const auto& step : body_steps) {
            if (step) ss << " " << step->to_scm_string();
        }
        ss << ")";
        return ss.str();
    }
};

// ---------------------------------------------------------------------------
// Graph Metadata Catalog
// ---------------------------------------------------------------------------
struct GraphCatalog {
    std::unordered_map<std::string, uint16_t> relation_ids;
    std::unordered_map<uint16_t, std::string> relation_names;
    std::unordered_map<std::string, double> relation_multiplicities;

    void register_relation(const std::string& name, uint16_t id, double multiplicity = 1.0) {
        relation_ids[name] = id;
        relation_names[id] = name;
        relation_multiplicities[name] = multiplicity;
    }

    [[nodiscard]] int get_relation_id(const std::string& name) const {
        auto it = relation_ids.find(name);
        return (it != relation_ids.end()) ? it->second : -1;
    }

    [[nodiscard]] double get_multiplicity(const std::string& name) const {
        auto it = relation_multiplicities.find(name);
        return (it != relation_multiplicities.end()) ? it->second : 1.0;
    }

    [[nodiscard]] std::string get_relation_name(uint16_t id) const {
        auto it = relation_names.find(id);
        return (it != relation_names.end()) ? it->second : "";
    }
};


} // namespace impulse::compiler

#endif // IMPULSE_SCM_AST_HPP
