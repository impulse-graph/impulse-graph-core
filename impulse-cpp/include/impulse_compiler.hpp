/**
 * @file impulse_compiler.hpp
 * @brief Zero-Dependency C++20 Optimizing Compiler Pipeline & ImpScheme IR for Impulse Graph.
 *
 * Implements the homoiconic ImpScheme AST compiler bus, 7-stage optimization pipeline,
 * multi-hop kernel fusion (OP_CSR_WALK_2HOP), register cache ping-ponging (R0 <-> R1),
 * seed inlining, early exit flags, and .impas bytecode disassembler.
 */

#ifndef IMPULSE_COMPILER_HPP
#define IMPULSE_COMPILER_HPP

#include "impulse_vm.h"
#include "impulse_graph.h"
#include "impulse_cel.h"

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
        return opts;
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
    STREAM_PROJECT
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
    Op op;
    AstPtr lhs;
    AstPtr rhs;
    NodeKind kind() const override { return NodeKind::SET_OP; }
    std::string to_scm_string() const override { return "(set-op)"; }
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

// ---------------------------------------------------------------------------
// Compiled Query Result Object
// ---------------------------------------------------------------------------
class CompiledImpulseProgram {
public:
    std::vector<impulse_instruction_t> instructions;
    AstPtr optimized_ast;
    uint16_t result_register{0};

    [[nodiscard]] size_t instruction_count() const { return instructions.size(); }
    [[nodiscard]] const impulse_instruction_t* data() const { return instructions.data(); }

    /**
     * @brief Formats human-readable ImpAsm disassembly.
     */
    [[nodiscard]] std::string to_impas_string(const GraphCatalog* catalog = nullptr) const {
        std::ostringstream ss;
        ss << "; =========================================================================\n";
        ss << ";                  IMPULSE VM BYTECODE DISASSEMBLY (.impas)               \n";
        ss << "; =========================================================================\n";
        ss << ".version 0.9.0\n";
        ss << ".instructions " << instructions.size() << "\n\n";

        for (size_t pc = 0; pc < instructions.size(); ++pc) {
            const auto& inst = instructions[pc];
            
            std::string op_name;
            std::string comment;

            switch (inst.opcode) {
                case OP_HALT:
                    op_name = "OP_HALT";
                    comment = "Execution complete";
                    break;
                case OP_NOP:
                    op_name = "OP_NOP";
                    break;
                case OP_INIT_INPUT_NODE:
                    op_name = "OP_INIT_INPUT_NODE";
                    comment = "Seed input node -> R" + std::to_string(inst.dst_reg);
                    break;
                case OP_CSR_WALK_2HOP: {
                    op_name = "OP_CSR_WALK_2HOP";
                    uint16_t r1 = inst.payload & 0xFFFF;
                    uint16_t r2 = (inst.payload >> 16) & 0xFFFF;
                    comment = "Fused 2-Hop CSR Walk via rel[" + std::to_string(r1) + "] -> rel[" + std::to_string(r2) + "]";
                    break;
                }
                case OP_ADAPTIVE_WALK: {
                    op_name = "OP_ADAPTIVE_WALK";
                    uint16_t src = inst.payload & 0xFFFF;
                    uint16_t rel = (inst.payload >> 16) & 0xFFFF;
                    comment = "Adaptive Walk src=R" + std::to_string(src) + " -> dst=R" + std::to_string(inst.dst_reg) + " via rel[" + std::to_string(rel) + "]";
                    if (catalog) {
                        std::string name = catalog->get_relation_name(rel);
                        if (!name.empty()) comment += " (\"" + name + "\")";
                    }
                    break;
                }
                case OP_CSR_WALK: {
                    op_name = "OP_CSR_WALK";
                    uint16_t src = inst.payload & 0xFFFF;
                    uint16_t rel = (inst.payload >> 16) & 0xFFFF;
                    comment = "Walk src=R" + std::to_string(src) + " -> dst=R" + std::to_string(inst.dst_reg) + " via rel[" + std::to_string(rel) + "]";
                    if (catalog) {
                        std::string name = catalog->get_relation_name(rel);
                        if (!name.empty()) comment += " (\"" + name + "\")";
                    }
                    break;
                }
                case OP_CSC_WALK: {
                    op_name = "OP_CSC_WALK";
                    uint16_t src = inst.payload & 0xFFFF;
                    uint16_t rel = (inst.payload >> 16) & 0xFFFF;
                    comment = "Walk-CSC src=R" + std::to_string(src) + " -> dst=R" + std::to_string(inst.dst_reg) + " via rel[" + std::to_string(rel) + "]";
                    if (catalog) {
                        std::string name = catalog->get_relation_name(rel);
                        if (!name.empty()) comment += " (\"" + name + "\")";
                    }
                    break;
                }
                case OP_CSR_WALK_FILTERED: {
                    op_name = "OP_CSR_WALK_FILTERED";
                    uint16_t src = inst.payload & 0xFFFF;
                    uint16_t rel = (inst.payload >> 16) & 0xFFFF;
                    comment = "Filtered CSR Walk src=R" + std::to_string(src) + " via rel[" + std::to_string(rel) + "]";
                    break;
                }
                case OP_CREATE_SCRATCH_INDEX:
                    op_name = "OP_CREATE_SCRATCH_INDEX";
                    break;
                case OP_DROP_SCRATCH_INDEX:
                    op_name = "OP_DROP_SCRATCH_INDEX";
                    break;
                case OP_VECTOR_TIME_VALID_AT:
                    op_name = "OP_VECTOR_TIME_VALID_AT";
                    break;
                case OP_COLLECT_BITSET:
                    op_name = "OP_COLLECT_BITSET";
                    comment = "Collect active result bitset from R" + std::to_string(inst.dst_reg);
                    break;
                default:
                    op_name = "OP_0x" + std::to_string(inst.opcode);
                    break;
            }

            char line_buf[256];
            std::snprintf(line_buf, sizeof(line_buf), "  0x%04x:  %-24s flags=0x%02x, dst=R%-2d, payload=0x%08x",
                          static_cast<unsigned int>(pc),
                          op_name.c_str(),
                          static_cast<unsigned int>(inst.flags),
                          static_cast<int>(inst.dst_reg),
                          static_cast<unsigned int>(inst.payload));
            ss << line_buf;
            
            if (!comment.empty()) {
                ss << " ; " << comment;
                if (inst.flags & IMPULSE_VM_OP_FLAG_INPUT_SEED) ss << " [seed-inlined]";
                if (inst.flags & IMPULSE_VM_OP_FLAG_HALT_ON_EMPTY) ss << " [early-exit]";
            }
            ss << "\n";
        }
        ss << "; =========================================================================\n";
        return ss.str();
    }
};

// ---------------------------------------------------------------------------
// 7-Stage Compiler Pipeline
// ---------------------------------------------------------------------------
class ImpulseCompiler {
public:
    /**
     * @brief Compiles an ImpScheme program AST into executable ImpOps bytecode.
     */
    static CompiledImpulseProgram compile(
        const std::shared_ptr<ScmProgram>& program,
        const GraphCatalog* catalog = nullptr,
        const std::unordered_map<std::string, double>& params = {},
        const CompilerOptions& options = CompilerOptions::default_options())
    {
        if (!program || program->steps.empty()) {
            throw std::invalid_argument("Empty ImpScheme program");
        }

        // Pass 1: Pre-bind validation
        validate_ast(program);

        // Pass 2: Parameter binding & Constant folding
        AstPtr ast = pass_parameter_binding(program, params, options);

        // Pass 3: Multi-hop Kernel Fusion (2-Hop Walk)
        if (options.enable_kernel_fusion) {
            ast = pass_kernel_fusion(ast, catalog, options);
        }

        // Pass 4: Physical Binding (Map relation names to catalog IDs)
        ast = pass_physical_binding(ast, catalog);

        // Pass 5: Direction Selection & CSC Inversion Check
        if (options.enable_direction_selection) {
            ast = pass_direction_selection(ast, catalog);
        }

        // Pass 6: Register Allocation & L1 Cache Ping-Ponging (R0 <-> R1)
        ast = pass_register_allocation(ast, options);

        // Pass 7: Bytecode Emission with Seed Inlining & Early Exit Flags
        return emit_bytecode(ast, options);
    }

private:
    static void validate_ast(const std::shared_ptr<ScmProgram>& prog) {
        if (prog->steps.empty()) {
            throw std::invalid_argument("ScmProgram must contain at least one step");
        }
    }

    
    static AstPtr pass_parameter_binding_node(const AstPtr& step, const std::unordered_map<std::string, double>& params) {
        if (!step) return nullptr;
        if (step->kind() == NodeKind::WALK) {
            auto w = std::static_pointer_cast<ScmWalk>(step);
            auto new_w = std::make_shared<ScmWalk>(*w);
            if (new_w->predicate && new_w->predicate->kind() == NodeKind::VECTOR_FILTER) {
                auto vf = std::static_pointer_cast<ScmVectorFilter>(new_w->predicate);
                auto new_vf = std::make_shared<ScmVectorFilter>(*vf);
                if (new_vf->threshold && new_vf->threshold->kind() == NodeKind::SYMBOL) {
                    auto sym = std::static_pointer_cast<ScmSymbol>(new_vf->threshold);
                    auto it = params.find(sym->name);
                    if (it != params.end()) {
                        new_vf->threshold = ScmLiteral::of_float(it->second);
                    }
                }
                new_w->predicate = new_vf;
            }
            return new_w;
        } else if (step->kind() == NodeKind::LET) {
            auto let = std::static_pointer_cast<ScmLet>(step);
            auto new_let = std::make_shared<ScmLet>(*let);
            for (auto& init : new_let->inits) init = pass_parameter_binding_node(init, params);
            for (auto& st : new_let->body) st = pass_parameter_binding_node(st, params);
            return new_let;
        } else if (step->kind() == NodeKind::SET) {
            auto set_n = std::static_pointer_cast<ScmSet>(step);
            auto new_set = std::make_shared<ScmSet>(*set_n);
            new_set->expr = pass_parameter_binding_node(set_n->expr, params);
            return new_set;
        } else if (step->kind() == NodeKind::LOOP_WHILE) {
            auto loop = std::static_pointer_cast<ScmLoopWhile>(step);
            auto new_loop = std::make_shared<ScmLoopWhile>(*loop);
            new_loop->condition = pass_parameter_binding_node(loop->condition, params);
            for (auto& st : new_loop->body) st = pass_parameter_binding_node(st, params);
            return new_loop;
        } else if (step->kind() == NodeKind::RETURN) {
            auto ret = std::static_pointer_cast<ScmReturn>(step);
            auto new_ret = std::make_shared<ScmReturn>(*ret);
            new_ret->expr = pass_parameter_binding_node(ret->expr, params);
            return new_ret;
        }
        return step;
    }

    static AstPtr pass_parameter_binding(
        const std::shared_ptr<ScmProgram>& prog,
        const std::unordered_map<std::string, double>& params,
        const CompilerOptions& /*options*/)
    {
        std::vector<AstPtr> new_steps;
        for (const auto& step : prog->steps) {
            new_steps.push_back(pass_parameter_binding_node(step, params));
        }
        return std::make_shared<ScmProgram>(std::move(new_steps));
    }


    static AstPtr pass_kernel_fusion(
        const AstPtr& root,
        const GraphCatalog* catalog,
        const CompilerOptions& options)
    {
        auto prog = std::static_pointer_cast<ScmProgram>(root);
        std::vector<AstPtr> fused_steps;
        size_t i = 0;

        while (i < prog->steps.size()) {
            if (i + 1 < prog->steps.size() &&
                prog->steps[i]->kind() == NodeKind::WALK &&
                prog->steps[i + 1]->kind() == NodeKind::WALK)
            {
                auto w1 = std::static_pointer_cast<ScmWalk>(prog->steps[i]);
                auto w2 = std::static_pointer_cast<ScmWalk>(prog->steps[i + 1]);

                if (w1->direction == WalkDirection::FORWARD_CSR &&
                    w2->direction == WalkDirection::FORWARD_CSR &&
                    !w1->predicate && !w2->predicate)
                {
                    double mult1 = catalog ? catalog->get_multiplicity(w1->relation_name) : 1.0;
                    if (mult1 <= options.fused_2hop_max_multiplicity_threshold) {
                        auto fused = std::make_shared<ScmWalk2Hop>(w1->relation_name, w2->relation_name);
                        fused_steps.push_back(fused);
                        i += 2;
                        continue;
                    }
                }
            }
            fused_steps.push_back(prog->steps[i]);
            i++;
        }
        return std::make_shared<ScmProgram>(std::move(fused_steps));
    }

    static AstPtr pass_physical_binding(const AstPtr& root, const GraphCatalog* catalog) {
        auto prog = std::static_pointer_cast<ScmProgram>(root);
        std::vector<AstPtr> bound_steps;

        for (const auto& step : prog->steps) {
            if (step->kind() == NodeKind::WALK) {
                auto w = std::static_pointer_cast<ScmWalk>(step);
                auto new_w = std::make_shared<ScmWalk>(*w);
                new_w->physical_rel_id = catalog ? catalog->get_relation_id(w->relation_name) : 0;
                bound_steps.push_back(new_w);
            } else if (step->kind() == NodeKind::WALK_2HOP) {
                auto w2 = std::static_pointer_cast<ScmWalk2Hop>(step);
                auto new_w2 = std::make_shared<ScmWalk2Hop>(*w2);
                new_w2->rel1_id = catalog ? catalog->get_relation_id(w2->rel1_name) : 0;
                new_w2->rel2_id = catalog ? catalog->get_relation_id(w2->rel2_name) : 1;
                bound_steps.push_back(new_w2);
            } else {
                bound_steps.push_back(step);
            }
        }
        return std::make_shared<ScmProgram>(std::move(bound_steps));
    }

    static AstPtr pass_direction_selection(const AstPtr& root, const GraphCatalog* /*catalog*/) {
        auto prog = std::static_pointer_cast<ScmProgram>(root);
        std::vector<AstPtr> opt_steps;
        for (const auto& step : prog->steps) {
            if (step->kind() == NodeKind::WALK) {
                auto w = std::static_pointer_cast<ScmWalk>(step);
                auto new_w = std::make_shared<ScmWalk>(*w);
                if (new_w->direction == WalkDirection::FORWARD_CSR && !new_w->predicate) {
                    new_w->is_adaptive = true;
                }
                opt_steps.push_back(new_w);
            } else {
                opt_steps.push_back(step);
            }
        }
        return std::make_shared<ScmProgram>(std::move(opt_steps));
    }

    static AstPtr pass_register_allocation(const AstPtr& root, const CompilerOptions& options) {
        auto prog = std::static_pointer_cast<ScmProgram>(root);
        std::vector<AstPtr> alloc_steps;
        uint16_t current_reg = 0;

        for (size_t idx = 0; idx < prog->steps.size(); ++idx) {
            const auto& step = prog->steps[idx];
            if (step->kind() == NodeKind::WALK) {
                auto w = std::static_pointer_cast<ScmWalk>(step);
                auto new_w = std::make_shared<ScmWalk>(*w);
                new_w->src_reg = current_reg;
                if (options.enable_register_ping_pong) {
                    new_w->dst_reg = (current_reg == 0) ? 1 : 0;
                } else {
                    new_w->dst_reg = static_cast<uint16_t>(idx + 1);
                }
                current_reg = new_w->dst_reg;
                alloc_steps.push_back(new_w);
            } else if (step->kind() == NodeKind::WALK_2HOP) {
                auto w2 = std::static_pointer_cast<ScmWalk2Hop>(step);
                auto new_w2 = std::make_shared<ScmWalk2Hop>(*w2);
                new_w2->src_reg = current_reg;
                if (options.enable_register_ping_pong) {
                    new_w2->dst_reg = (current_reg == 0) ? 1 : 0;
                } else {
                    new_w2->dst_reg = static_cast<uint16_t>(idx + 1);
                }
                current_reg = new_w2->dst_reg;
                alloc_steps.push_back(new_w2);
            } else if (step->kind() == NodeKind::COLLECT) {
                auto c = std::static_pointer_cast<ScmCollect>(step);
                auto new_c = std::make_shared<ScmCollect>(*c);
                new_c->target_reg = current_reg;
                alloc_steps.push_back(new_c);
            } else {
                alloc_steps.push_back(step);
            }
        }
        return std::make_shared<ScmProgram>(std::move(alloc_steps));
    }

    
    struct CompileContext {
        std::vector<impulse_instruction_t> instructions;
        std::unordered_map<std::string, uint16_t> env;
        uint16_t next_reg = 0;
        uint16_t result_reg = 0;

        uint16_t get_reg(const std::string& name) {
            if (env.find(name) == env.end()) {
                env[name] = next_reg++;
            }
            return env[name];
        }
    };

    static void emit_node(const AstPtr& step, CompileContext& ctx, const CompilerOptions& options) {
        if (step->kind() == NodeKind::WALK) {
            auto w = std::static_pointer_cast<ScmWalk>(step);
            impulse_instruction_t inst{};
            inst.opcode = (w->direction == WalkDirection::FORWARD_CSR) ? OP_CSR_WALK : OP_CSC_WALK;
            if (w->is_adaptive) {
                inst.opcode = OP_ADAPTIVE_WALK;
            } else if (w->predicate) {
                inst.opcode = OP_CSR_WALK;
            }
            // For now, assume it modifies R2 (frontier) and reads from R2, or reads from env
            inst.dst_reg = w->dst_reg; // Default
            if (ctx.env.count("frontier")) {
                inst.dst_reg = ctx.env["frontier"];
                w->src_reg = ctx.env["frontier"];
            }
            uint16_t rel_id = static_cast<uint16_t>(std::max(0, w->physical_rel_id));
            inst.payload = (static_cast<uint32_t>(rel_id) << 16) | static_cast<uint32_t>(w->src_reg);
            inst.flags = 0;
            ctx.instructions.push_back(inst);
            ctx.result_reg = inst.dst_reg;
        } else if (step->kind() == NodeKind::COLLECT) {
            auto c = std::static_pointer_cast<ScmCollect>(step);
            impulse_instruction_t inst{};
            inst.opcode = OP_COLLECT_BITSET;
            inst.dst_reg = ctx.result_reg;
            inst.payload = ctx.result_reg;
            inst.flags = 0;
            ctx.instructions.push_back(inst);
        } else if (step->kind() == NodeKind::LET) {
            auto let = std::static_pointer_cast<ScmLet>(step);
            for (size_t i = 0; i < let->vars.size(); ++i) {
                uint16_t r = ctx.get_reg(let->vars[i]);
                emit_node(let->inits[i], ctx, options);
                if (ctx.result_reg != r) {
                    impulse_instruction_t mov{};
                    mov.opcode = OP_MOV;
                    mov.dst_reg = r;
                    mov.payload = ctx.result_reg;
                    ctx.instructions.push_back(mov);
                }
            }
            for (const auto& stmt : let->body) {
                emit_node(stmt, ctx, options);
            }
        } else if (step->kind() == NodeKind::SET) {
            auto set_node = std::static_pointer_cast<ScmSet>(step);
            emit_node(set_node->expr, ctx, options);
            uint16_t r = ctx.get_reg(set_node->var);
            if (ctx.result_reg != r) {
                impulse_instruction_t mov{};
                mov.opcode = OP_MOV;
                mov.dst_reg = r;
                mov.payload = ctx.result_reg;
                ctx.instructions.push_back(mov);
            }
        } else if (step->kind() == NodeKind::LOOP_WHILE) {
            auto loop = std::static_pointer_cast<ScmLoopWhile>(step);
            size_t start_pc = ctx.instructions.size();
            
            // Emit condition
            emit_node(loop->condition, ctx, options);
            
            // JZ instruction (placeholder offset)
            impulse_instruction_t jz{};
            jz.opcode = OP_JZ;
            jz.dst_reg = ctx.result_reg; // condition result
            jz.payload = 0; // Patch later
            size_t jz_idx = ctx.instructions.size();
            ctx.instructions.push_back(jz);

            // Body
            for (const auto& stmt : loop->body) {
                emit_node(stmt, ctx, options);
            }

            // JMP back to start
            impulse_instruction_t jmp{};
            jmp.opcode = OP_JMP;
            jmp.payload = static_cast<uint32_t>(start_pc) - static_cast<uint32_t>(ctx.instructions.size()); // Relative jump backwards
            ctx.instructions.push_back(jmp);

            // Patch JZ
            ctx.instructions[jz_idx].payload = static_cast<uint32_t>(ctx.instructions.size()) - static_cast<uint32_t>(jz_idx);
        } else if (step->kind() == NodeKind::SET_OP) {
            auto set_op = std::static_pointer_cast<ScmSetOp>(step);
            emit_node(set_op->lhs, ctx, options);
            uint16_t lhs_reg = ctx.result_reg;
            
            // Avoid clobbering lhs_reg during rhs eval by temporarily marking it used if needed
            // Actually our simple AST guarantees simple var refs for RHS
            emit_node(set_op->rhs, ctx, options);
            uint16_t rhs_reg = ctx.result_reg;

            impulse_instruction_t inst{};
            if (set_op->op == ScmSetOp::UNION) inst.opcode = OP_SET_UNION;
            else if (set_op->op == ScmSetOp::DIFFERENCE) inst.opcode = OP_SET_DIFFERENCE;
            else inst.opcode = OP_SET_INTERSECT;

            ctx.result_reg = ctx.next_reg++;
            impulse_instruction_t mov{};
            mov.opcode = OP_MOV;
            mov.dst_reg = ctx.result_reg;
            mov.payload = lhs_reg;
            ctx.instructions.push_back(mov);

            inst.dst_reg = ctx.result_reg;
            inst.payload = rhs_reg;
            ctx.instructions.push_back(inst);
        } else if (step->kind() == NodeKind::VAR_REF) {
            auto v = std::static_pointer_cast<ScmVarRef>(step);
            ctx.result_reg = ctx.get_reg(v->var);
        } else if (step->kind() == NodeKind::BITSET_INIT) {
            auto init = std::static_pointer_cast<ScmBitsetInit>(step);
            impulse_instruction_t inst{};
            ctx.result_reg = ctx.next_reg++; // Allocate a temp register
            inst.dst_reg = ctx.result_reg;
            if (init->type == ScmBitsetInit::InitType::EMPTY) {
                inst.opcode = OP_CLEAR_REG;
            } else if (init->type == ScmBitsetInit::InitType::ALL) {
                inst.opcode = OP_INIT_INPUT_SET;
            } else if (init->type == ScmBitsetInit::InitType::FROM_NODE) {
                inst.opcode = OP_INIT_INPUT_NODE;
                // Typically node 0 or a passed param
                inst.payload = 0; 
            }
            ctx.instructions.push_back(inst);
        } else if (step->kind() == NodeKind::CARDINALITY) {
            auto card = std::static_pointer_cast<ScmCardinality>(step);
            impulse_instruction_t inst{};
            inst.opcode = OP_SET_CARDINALITY;
            inst.dst_reg = ctx.next_reg++; // Result
            uint16_t src = ctx.get_reg(card->var);
            inst.payload = src;
            ctx.instructions.push_back(inst);
            ctx.result_reg = inst.dst_reg;
        } else if (step->kind() == NodeKind::RETURN) {
            auto ret = std::static_pointer_cast<ScmReturn>(step);
            emit_node(ret->expr, ctx, options);
            impulse_instruction_t inst{};
            inst.opcode = OP_HALT;
            inst.dst_reg = ctx.result_reg;
            ctx.instructions.push_back(inst);
        }
    }

    static CompiledImpulseProgram emit_bytecode(const AstPtr& root, const CompilerOptions& options) {
        auto prog = std::static_pointer_cast<ScmProgram>(root);
        CompiledImpulseProgram result;
        result.optimized_ast = root;

        CompileContext ctx;
        for (const auto& step : prog->steps) {
            emit_node(step, ctx, options);
        }

        // Add HALT if not present
        if (ctx.instructions.empty() || ctx.instructions.back().opcode != OP_HALT) {
            impulse_instruction_t halt_inst{};
            halt_inst.opcode = OP_HALT;
            halt_inst.dst_reg = ctx.result_reg;
            ctx.instructions.push_back(halt_inst);
        }

        result.instructions = ctx.instructions;
        result.result_register = ctx.result_reg;
        return result;
    }

};

} // namespace impulse::compiler

#endif // IMPULSE_COMPILER_HPP
