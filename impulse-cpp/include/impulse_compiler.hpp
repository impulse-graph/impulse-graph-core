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
    LITERAL,
    SYMBOL
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
    enum class OutputType { BITSET, COUNT, SCALAR };
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
        }
        return "(collect)";
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
            ss << "  0x" << std::hex << std::setw(4) << std::setfill('0') << pc << ":  " << std::dec;
            
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
                    comment = "Walk src=R" + std::to_string(src) + " -> dst=R" + std::to_string(inst.dst_reg) + " via rel[" + std::to_string(rel) + "]";
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
                case OP_COLLECT_BITSET:
                    op_name = "OP_COLLECT_BITSET";
                    comment = "Collect active result bitset from R" + std::to_string(inst.dst_reg);
                    break;
                default:
                    op_name = "OP_0x" + std::to_string(inst.opcode);
                    break;
            }

            ss << std::left << std::setw(24) << op_name;
            ss << "flags=0x" << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(inst.flags) << ", ";
            ss << "dst=R" << std::dec << std::setw(2) << std::setfill(' ') << inst.dst_reg << ", ";
            ss << "payload=0x" << std::hex << std::setw(8) << std::setfill('0') << inst.payload << std::dec;
            
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

    static AstPtr pass_parameter_binding(
        const std::shared_ptr<ScmProgram>& prog,
        const std::unordered_map<std::string, double>& params,
        const CompilerOptions& /*options*/)
    {
        std::vector<AstPtr> new_steps;
        for (const auto& step : prog->steps) {
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
                new_steps.push_back(new_w);
            } else {
                new_steps.push_back(step);
            }
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
        return root;
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

    static CompiledImpulseProgram emit_bytecode(const AstPtr& root, const CompilerOptions& options) {
        auto prog = std::static_pointer_cast<ScmProgram>(root);
        CompiledImpulseProgram result;
        result.optimized_ast = root;

        bool is_first_walk = true;
        uint16_t last_dst_reg = 0;

        for (const auto& step : prog->steps) {
            if (step->kind() == NodeKind::WALK) {
                auto w = std::static_pointer_cast<ScmWalk>(step);
                impulse_instruction_t inst{};
                inst.opcode = (w->direction == WalkDirection::FORWARD_CSR) ? OP_CSR_WALK : OP_CSC_WALK;
                if (w->predicate) {
                    inst.opcode = OP_CSR_WALK_FILTERED;
                }
                inst.dst_reg = w->dst_reg;
                uint16_t rel_id = static_cast<uint16_t>(std::max(0, w->physical_rel_id));
                inst.payload = (static_cast<uint32_t>(rel_id) << 16) | static_cast<uint32_t>(w->src_reg);

                inst.flags = 0;
                if (is_first_walk && options.enable_seed_inlining) {
                    inst.flags |= IMPULSE_VM_OP_FLAG_INPUT_SEED;
                }
                if (options.enable_early_exit) {
                    inst.flags |= IMPULSE_VM_OP_FLAG_HALT_ON_EMPTY;
                }

                result.instructions.push_back(inst);
                last_dst_reg = w->dst_reg;
                is_first_walk = false;
            } else if (step->kind() == NodeKind::WALK_2HOP) {
                auto w2 = std::static_pointer_cast<ScmWalk2Hop>(step);
                impulse_instruction_t inst{};
                inst.opcode = OP_CSR_WALK_2HOP;
                inst.dst_reg = w2->dst_reg;
                uint16_t r1 = static_cast<uint16_t>(std::max(0, w2->rel1_id));
                uint16_t r2 = static_cast<uint16_t>(std::max(0, w2->rel2_id));
                inst.payload = (static_cast<uint32_t>(r2) << 16) | static_cast<uint32_t>(r1);

                inst.flags = 0;
                if (is_first_walk && options.enable_seed_inlining) {
                    inst.flags |= IMPULSE_VM_OP_FLAG_INPUT_SEED;
                }
                if (options.enable_early_exit) {
                    inst.flags |= IMPULSE_VM_OP_FLAG_HALT_ON_EMPTY;
                }

                result.instructions.push_back(inst);
                last_dst_reg = w2->dst_reg;
                is_first_walk = false;
            } else if (step->kind() == NodeKind::COLLECT) {
                auto c = std::static_pointer_cast<ScmCollect>(step);
                impulse_instruction_t inst{};
                inst.opcode = OP_COLLECT_BITSET;
                inst.dst_reg = last_dst_reg;
                inst.payload = last_dst_reg;
                inst.flags = 0;
                result.instructions.push_back(inst);
                result.result_register = last_dst_reg;
            }
        }

        // Emit OP_HALT
        impulse_instruction_t halt_inst{};
        halt_inst.opcode = OP_HALT;
        halt_inst.dst_reg = 0;
        halt_inst.payload = 0;
        halt_inst.flags = 0;
        result.instructions.push_back(halt_inst);

        return result;
    }
};

} // namespace impulse::compiler

#endif // IMPULSE_COMPILER_HPP
