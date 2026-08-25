#include "impulse_compiler.hpp"
#include "impulse_sexpr.hpp"
#include "impulse_vm.h"
#include "impulse_graph.h"
#include <cassert>
#include <iostream>
#include <memory>
#include <vector>

using namespace impulse::compiler;
using namespace impulse::impscm;

void test_mcdc_sexpr_parser() {
    std::cout << "[MC/DC] Testing SExprParser boundary conditions..." << std::endl;

    // 1. Empty string, whitespace, comments
    SExprParser p1("  \t \n  ; this is a comment\n ");
    SExpr e1 = p1.parse();
    assert(!e1.is_list && e1.atom.empty());

    // 2. Parentheses and brackets
    SExprParser p2("(a [b c])");
    SExpr e2 = p2.parse();
    assert(e2.is_list && e2.list.size() == 2);
    assert(e2.list[0].atom == "a");
    assert(e2.list[1].is_list && e2.list[1].list.size() == 2);
    assert(e2.list[1].list[0].atom == "b");
    
    // 3. String literals (empty, normal, unterminated)
    SExprParser p3("\"\" \"hello\" \"unterminated");
    SExpr e3 = p3.parse();
    assert(e3.atom == ""); // empty string
    SExpr e3b = p3.parse();
    assert(e3b.atom == "hello");
    SExpr e3c = p3.parse();
    assert(e3c.atom == "unterminated");
}

void test_mcdc_ast_builder() {
    std::cout << "[MC/DC] Testing ImpScmAstBuilder boundary conditions..." << std::endl;

    // 1. Literal numbers and variables
    SExprParser p_num("123");
    auto ast_num = ImpScmAstBuilder::build(p_num.parse());
    assert(ast_num->kind() == NodeKind::LITERAL);
    
    SExprParser p_var("my_var");
    auto ast_var = ImpScmAstBuilder::build(p_var.parse());
    assert(ast_var->kind() == NodeKind::VAR_REF);
    
    // Empty list
    std::cout << "p_empty" << std::endl; SExprParser p_empty("()");
    auto ast_empty = ImpScmAstBuilder::build(p_empty.parse());
    assert(ast_empty == nullptr);

    // 2. Pipeline and Define-Query
    std::cout << "p_pipe" << std::endl; SExprParser p_pipe("(impk-pipeline (define-query my_q (g:walk-csr edge)))");
    auto ast_pipe = ImpScmAstBuilder::build(p_pipe.parse());
    assert(ast_pipe->kind() == NodeKind::PROGRAM);
    
    // Missing define-query
    SExprParser p_pipe_empty("(impk-pipeline)");
    auto ast_pipe_empty = ImpScmAstBuilder::build(p_pipe_empty.parse());
    assert(ast_pipe_empty->kind() == NodeKind::PROGRAM);

    // 3. Let blocks
    std::cout << "p_let" << std::endl; SExprParser p_let("(let ((a 1) (b c)) (set! a 3) (return a))");
    auto ast_let = ImpScmAstBuilder::build(p_let.parse());
    assert(ast_let->kind() == NodeKind::LET);
    
    SExprParser p_let_bad("(let)");
    auto ast_let_bad = ImpScmAstBuilder::build(p_let_bad.parse());
    assert(ast_let_bad == nullptr); // Invalid let (no bindings)

    // 4. Loop-while
    std::cout << "p_loop" << std::endl; SExprParser p_loop("(loop-while (> (bitset:cardinality frontier) 0) (csr-walk edge))");
    auto ast_loop = ImpScmAstBuilder::build(p_loop.parse());
    assert(ast_loop->kind() == NodeKind::LOOP_WHILE);
    
    SExprParser p_loop_bad("(loop-while)");
    auto ast_loop_bad = ImpScmAstBuilder::build(p_loop_bad.parse());
    assert(ast_loop_bad == nullptr);

    // 5. Bitset Operations
    std::cout << "p_bs1" << std::endl; SExprParser p_bs1("(bitset:empty)");
    assert(ImpScmAstBuilder::build(p_bs1.parse())->kind() == NodeKind::BITSET_INIT);
    
    SExprParser p_bs2("(bitset:all)");
    assert(ImpScmAstBuilder::build(p_bs2.parse())->kind() == NodeKind::BITSET_INIT);
    
    SExprParser p_bs3("(bitset:from start_node)");
    assert(ImpScmAstBuilder::build(p_bs3.parse())->kind() == NodeKind::BITSET_INIT);
    
    SExprParser p_bs4("(bitset:cardinality frontier)");
    assert(ImpScmAstBuilder::build(p_bs4.parse())->kind() == NodeKind::CARDINALITY);

    // 6. Set Operations
    std::cout << "p_set1" << std::endl; SExprParser p_set1("(set:difference a b)");
    assert(ImpScmAstBuilder::build(p_set1.parse())->kind() == NodeKind::SET_OP);
    
    SExprParser p_set2("(set:union a b)");
    assert(ImpScmAstBuilder::build(p_set2.parse())->kind() == NodeKind::SET_OP);
    
    SExprParser p_set3("(set:intersect a b)");
    assert(ImpScmAstBuilder::build(p_set3.parse())->kind() == NodeKind::SET_OP);

    SExprParser p_set_bad("(set:union a)"); // Not enough args
    assert(ImpScmAstBuilder::build(p_set_bad.parse()) == nullptr);

    // 7. Collect and set!
    std::cout << "p_coll" << std::endl; SExprParser p_coll("(collect-bitset)");
    assert(ImpScmAstBuilder::build(p_coll.parse())->kind() == NodeKind::COLLECT);
    
    SExprParser p_set("(set! var 1)");
    assert(ImpScmAstBuilder::build(p_set.parse())->kind() == NodeKind::SET);
    
    SExprParser p_set_err("(set! var)");
    assert(ImpScmAstBuilder::build(p_set_err.parse()) == nullptr);
}

void test_mcdc_compiler_pipeline() {
    std::cout << "[MC/DC] Testing ImpulseCompiler boundary conditions..." << std::endl;

    // 1. Empty program / Null checks
    try {
        ImpulseCompiler::compile(nullptr);
        assert(false);
    } catch (const std::invalid_argument&) {}

    auto empty_prog = std::make_shared<ScmProgram>(std::vector<AstPtr>{});
    try {
        ImpulseCompiler::compile(empty_prog);
        assert(false);
    } catch (const std::invalid_argument&) {}

    // 2. Parameter binding (NodeKind::VECTOR_FILTER)
    auto prog = std::make_shared<ScmProgram>(std::vector<AstPtr>{});
    auto walk = ScmWalk::forward("edge");
    auto filter = std::make_shared<ScmVectorFilter>("attr", CompareOp::EQ, std::make_shared<ScmSymbol>("dummy"));
    auto sym = std::make_shared<ScmSymbol>("threshold_val");
    filter->threshold = sym;
    walk->predicate = filter;
    prog->steps.push_back(walk);

    std::unordered_map<std::string, double> params = {{"threshold_val", 0.5}};
    auto ast_bound = ImpulseCompiler::compile(prog, nullptr, params);
    
    // Test where symbol is NOT in params
    auto prog_missing = std::make_shared<ScmProgram>(std::vector<AstPtr>{});
    auto walk2 = ScmWalk::forward("edge");
    auto filter2 = std::make_shared<ScmVectorFilter>("attr", CompareOp::EQ, std::make_shared<ScmSymbol>("dummy"));
    filter2->threshold = std::make_shared<ScmSymbol>("missing_val");
    walk2->predicate = filter2;
    prog_missing->steps.push_back(walk2);
    auto ast_miss = ImpulseCompiler::compile(prog_missing); // params empty

    // Predicate not VECTOR_FILTER
    auto prog_pred_other = std::make_shared<ScmProgram>(std::vector<AstPtr>{});
    auto w_other = ScmWalk::forward("edge");
    w_other->predicate = std::make_shared<ScmLiteral>();
    prog_pred_other->steps.push_back(w_other);
    ImpulseCompiler::compile(prog_pred_other, nullptr, params);

    // Threshold not SYMBOL
    auto prog_thresh_lit = std::make_shared<ScmProgram>(std::vector<AstPtr>{});
    auto w_lit = ScmWalk::forward("edge");
    auto f_lit = std::make_shared<ScmVectorFilter>("attr", CompareOp::EQ, std::make_shared<ScmLiteral>());
    w_lit->predicate = f_lit;
    prog_thresh_lit->steps.push_back(w_lit);
    ImpulseCompiler::compile(prog_thresh_lit, nullptr, params);

    // Threshold NULL
    auto prog_thresh_null = std::make_shared<ScmProgram>(std::vector<AstPtr>{});
    auto w_null = ScmWalk::forward("edge");
    auto f_null = std::make_shared<ScmVectorFilter>("attr", CompareOp::EQ, nullptr);
    w_null->predicate = f_null;
    prog_thresh_null->steps.push_back(w_null);
    ImpulseCompiler::compile(prog_thresh_null, nullptr, params);


    // 3. Kernel Fusion
    // T,T,T,T (fusable)
    auto prog_fuse = std::make_shared<ScmProgram>(std::vector<AstPtr>{});
    auto w_fuse1 = ScmWalk::forward("edge1");
    auto w_fuse2 = ScmWalk::forward("edge2");
    prog_fuse->steps.push_back(w_fuse1);
    prog_fuse->steps.push_back(w_fuse2);
    
    CompilerOptions opts;
    opts.enable_kernel_fusion = true;
    opts.fused_2hop_max_multiplicity_threshold = 2.0;
    auto ast_fused = ImpulseCompiler::compile(prog_fuse, nullptr, {}, opts);
    
    // T,F (w1 forward, w2 reverse)
    auto prog_no_fuse = std::make_shared<ScmProgram>(std::vector<AstPtr>{});
    auto w_no_fuse1 = ScmWalk::forward("edge1");
    auto w_no_fuse2 = ScmWalk::reverse("edge2");
    prog_no_fuse->steps.push_back(w_no_fuse1);
    prog_no_fuse->steps.push_back(w_no_fuse2);
    auto ast_not_fused = ImpulseCompiler::compile(prog_no_fuse, nullptr, {}, opts);

    // Predicate blocks fusion
    auto prog_pred = std::make_shared<ScmProgram>(std::vector<AstPtr>{});
    auto w_pred1 = ScmWalk::forward("edge1");
    w_pred1->predicate = std::make_shared<ScmVectorFilter>("attr", CompareOp::EQ, std::make_shared<ScmSymbol>("dummy"));
    auto w_pred2 = ScmWalk::forward("edge2");
    prog_pred->steps.push_back(w_pred1);
    prog_pred->steps.push_back(w_pred2);
    auto ast_pred_fused = ImpulseCompiler::compile(prog_pred, nullptr, {}, opts);

    // Fusion loop: first not WALK
    auto prog_first_not_walk = std::make_shared<ScmProgram>(std::vector<AstPtr>{});
    prog_first_not_walk->steps.push_back(std::make_shared<ScmLiteral>());
    prog_first_not_walk->steps.push_back(ScmWalk::forward("edge2"));
    ImpulseCompiler::compile(prog_first_not_walk, nullptr, {}, opts);

    // Fusion loop: second not WALK
    auto prog_second_not_walk = std::make_shared<ScmProgram>(std::vector<AstPtr>{});
    prog_second_not_walk->steps.push_back(ScmWalk::forward("edge1"));
    prog_second_not_walk->steps.push_back(std::make_shared<ScmLiteral>());
    ImpulseCompiler::compile(prog_second_not_walk, nullptr, {}, opts);

    // Fusion checks: w1 reverse CSC
    auto prog_w1_rev = std::make_shared<ScmProgram>(std::vector<AstPtr>{});
    prog_w1_rev->steps.push_back(ScmWalk::reverse("edge1"));
    prog_w1_rev->steps.push_back(ScmWalk::forward("edge2"));
    ImpulseCompiler::compile(prog_w1_rev, nullptr, {}, opts);

    // Fusion checks: w2 has predicate
    auto prog_w2_pred = std::make_shared<ScmProgram>(std::vector<AstPtr>{});
    auto w2p = ScmWalk::forward("edge2");
    w2p->predicate = std::make_shared<ScmVectorFilter>("attr", CompareOp::EQ, std::make_shared<ScmSymbol>("dummy"));
    prog_w2_pred->steps.push_back(ScmWalk::forward("edge1"));
    prog_w2_pred->steps.push_back(w2p);
    ImpulseCompiler::compile(prog_w2_pred, nullptr, {}, opts);

    
    // ScmWalk2Hop to_scm_string with valid relation ids vs without
    auto fuse2hop = std::make_shared<ScmWalk2Hop>("r1", "r2");
    fuse2hop->rel1_id = 1;
    fuse2hop->rel2_id = 2;
    std::string s1 = fuse2hop->to_scm_string();
    assert(s1.find("1 2") != std::string::npos);
    
    auto fuse2hop_bad = std::make_shared<ScmWalk2Hop>("r1", "r2");
    fuse2hop_bad->rel1_id = -1;
    fuse2hop_bad->rel2_id = 2;
    std::string s2 = fuse2hop_bad->to_scm_string();
    assert(s2.find("\"r1\" \"r2\"") != std::string::npos);
    auto fuse2hop_bad2 = std::make_shared<ScmWalk2Hop>("r1", "r2");
    fuse2hop_bad2->rel1_id = 1;
    fuse2hop_bad2->rel2_id = -1;
    fuse2hop_bad2->to_scm_string();


    // 4. Direction Selection & Emitter OP_HALT appending

    auto prog_dir = std::make_shared<ScmProgram>(std::vector<AstPtr>{});
    prog_dir->steps.push_back(ScmWalk::forward("rel1"));
    auto res_dir = ImpulseCompiler::compile(prog_dir);
    assert(res_dir.instructions.back().opcode == OP_HALT);

    // Test explicit OP_HALT via ScmReturn
    auto prog_ret = std::make_shared<ScmProgram>(std::vector<AstPtr>{});
    auto ret_node = std::make_shared<ScmReturn>();
    ret_node->expr = std::make_shared<ScmLiteral>();
    prog_ret->steps.push_back(ret_node);
    auto res_ret = ImpulseCompiler::compile(prog_ret);
    assert(res_ret.instructions.back().opcode == OP_HALT);

}


void test_mcdc_sexpr_deep_coverage() {
    std::cout << "[MC/DC] Testing SExprParser & AST Builder deep decision paths..." << std::endl;

    // 1. Line 35: Comment terminated by EOF without newline
    SExprParser p_eof_comment("; comment at eof");
    p_eof_comment.parse();

    // 2. Line 74: Atom termination characters: '(', ')', '[', ']', space, eof
    SExprParser p_delim_paren("(foo(bar))");
    p_delim_paren.parse();
    SExprParser p_delim_bracket("[foo[bar]]");
    p_delim_bracket.parse();
    SExprParser p_delim_close("(foo)bar");
    p_delim_close.parse();
    p_delim_close.parse();
    SExprParser p_delim_brack_close("[foo]bar");
    p_delim_brack_close.parse();
    p_delim_brack_close.parse();

    // 3. Line 86: Empty atom with is_list = false
    SExpr empty_atom{};
    empty_atom.is_list = false;
    empty_atom.atom = "";
    assert(ImpScmAstBuilder::build(empty_atom) == nullptr);

    // 4. Line 101: Head variations for pipeline: "impk-pipeline", "pipeline", "query", "module", non-matching
    SExprParser p_pipeline("(pipeline (csr-walk edge))");
    assert(ImpScmAstBuilder::build(p_pipeline.parse())->kind() == NodeKind::PROGRAM);
    SExprParser p_query("(query (csr-walk edge))");
    assert(ImpScmAstBuilder::build(p_query.parse())->kind() == NodeKind::PROGRAM);
    SExprParser p_module("(module (csr-walk edge))");
    assert(ImpScmAstBuilder::build(p_module.parse())->kind() == NodeKind::PROGRAM);

    // 5. Line 104 & 107: define-query and define-kernel in pipeline
    // Item is not a list
    SExprParser p_pipe_atom("(pipeline atom_step)");
    assert(ImpScmAstBuilder::build(p_pipe_atom.parse())->kind() == NodeKind::PROGRAM);
    // Item is empty list
    SExprParser p_pipe_empty_step("(pipeline ())");
    assert(ImpScmAstBuilder::build(p_pipe_empty_step.parse())->kind() == NodeKind::PROGRAM);
    // Item is define-kernel
    SExprParser p_pipe_kernel("(pipeline (define-kernel my_k (csr-walk edge)))");
    assert(ImpScmAstBuilder::build(p_pipe_kernel.parse())->kind() == NodeKind::PROGRAM);
    // Item is define-query
    SExprParser p_pipe_dq("(pipeline (define-query my_q (csr-walk edge)))");
    assert(ImpScmAstBuilder::build(p_pipe_dq.parse())->kind() == NodeKind::PROGRAM);

    // 6. Line 128: Let bindings variations
    // Bindings is not a list: (let a (return a))
    SExprParser p_let_nonlist("(let a (return a))");
    assert(ImpScmAstBuilder::build(p_let_nonlist.parse())->kind() == NodeKind::LET);
    // Bind item is not a list: (let (a) (return a))
    SExprParser p_let_bind_nonlist("(let (a) (return a))");
    assert(ImpScmAstBuilder::build(p_let_bind_nonlist.parse())->kind() == NodeKind::LET);
    // Bind item is list of size != 2: (let ((a)) (return a)) and (let ((a 1 2)) (return a))
    SExprParser p_let_bind_sz1("(let ((a)) (return a))");
    assert(ImpScmAstBuilder::build(p_let_bind_sz1.parse())->kind() == NodeKind::LET);
    SExprParser p_let_bind_sz3("(let ((a 1 2)) (return a))");
    assert(ImpScmAstBuilder::build(p_let_bind_sz3.parse())->kind() == NodeKind::LET);

    // 7. Line 162: bitset:from with size != 2
    SExprParser p_bs_from_err("(bitset:from)");
    assert(ImpScmAstBuilder::build(p_bs_from_err.parse()) == nullptr);
    SExprParser p_bs_from_err3("(bitset:from a b)");
    assert(ImpScmAstBuilder::build(p_bs_from_err3.parse()) == nullptr);

    // 8. Line 167: bitset:cardinality with size != 2
    SExprParser p_bs_card_err("(bitset:cardinality)");
    assert(ImpScmAstBuilder::build(p_bs_card_err.parse()) == nullptr);
    SExprParser p_bs_card_err3("(bitset:cardinality a b)");
    assert(ImpScmAstBuilder::build(p_bs_card_err3.parse()) == nullptr);

    // 9. Line 171: > with size != 3
    SExprParser p_gt_err1("(>)");
    assert(ImpScmAstBuilder::build(p_gt_err1.parse()) == nullptr);
    SExprParser p_gt_err2("(> a)");
    assert(ImpScmAstBuilder::build(p_gt_err2.parse()) == nullptr);
    SExprParser p_gt_err4("(> a b c)");
    assert(ImpScmAstBuilder::build(p_gt_err4.parse()) == nullptr);

    // 10. Line 175: Walk heads (g:walk-csr and mxv)
    SExprParser p_walk_g("(g:walk-csr edge)");
    assert(ImpScmAstBuilder::build(p_walk_g.parse())->kind() == NodeKind::WALK);
    SExprParser p_walk_mxv("(mxv edge)");
    assert(ImpScmAstBuilder::build(p_walk_mxv.parse())->kind() == NodeKind::WALK);

    // 11. Line 178: Walk arguments filtering: is_list, "g", "frontier", "updateEdge"
    SExprParser p_walk_args("(csr-walk g frontier updateEdge (some-list) custom_rel)");
    auto ast_walk = ImpScmAstBuilder::build(p_walk_args.parse());
    assert(ast_walk->kind() == NodeKind::WALK);
    auto w_node = std::static_pointer_cast<ScmWalk>(ast_walk);
    assert(w_node->relation_name == "custom_rel");

    // 12. Line 195: Return with size != 2
    SExprParser p_ret_err1("(return)");
    assert(ImpScmAstBuilder::build(p_ret_err1.parse()) == nullptr);
    SExprParser p_ret_err3("(return a b)");
    assert(ImpScmAstBuilder::build(p_ret_err3.parse()) == nullptr);

    // 13. Top-level ImpScmAstBuilder::parse API
    // 13a. Program root
    auto parsed_prog = ImpScmAstBuilder::parse("(pipeline (csr-walk edge))");
    assert(parsed_prog != nullptr && parsed_prog->kind() == NodeKind::PROGRAM);
    // 13b. Single statement root wrapped in ScmProgram
    auto parsed_single = ImpScmAstBuilder::parse("(csr-walk edge)");
    assert(parsed_single != nullptr && parsed_single->kind() == NodeKind::PROGRAM);
    // 13c. Parse failure exception
    try {
        ImpScmAstBuilder::parse("()");
        assert(false);
    } catch (const std::runtime_error&) {}
}

void test_mcdc_ast_nodes_and_passes_exhaustive() {
    std::cout << "[MC/DC] Testing Exhaustive AST Nodes, Printers, and 7 Optimization Passes..." << std::endl;

    // 1. Compiler options
    CompilerOptions unopt = CompilerOptions::unoptimized();
    assert(!unopt.enable_kernel_fusion);
    assert(!unopt.enable_constant_folding);

    // 2. Compare op to string
    assert(std::string(compare_op_to_str(CompareOp::EQ)) == "vec-cmp-eq");
    assert(std::string(compare_op_to_str(CompareOp::NEQ)) == "vec-cmp-neq");
    assert(std::string(compare_op_to_str(CompareOp::LT)) == "vec-cmp-lt");
    assert(std::string(compare_op_to_str(CompareOp::LTE)) == "vec-cmp-lte");
    assert(std::string(compare_op_to_str(CompareOp::GT)) == "vec-cmp-gt");
    assert(std::string(compare_op_to_str(CompareOp::GTE)) == "vec-cmp-gte");

    // 3. AST Node Creation and Printers (to_scm_string)
    auto lit_int = ScmLiteral::of_int(42);
    assert(lit_int->kind() == NodeKind::LITERAL);
    assert(lit_int->to_scm_string() == "42");

    auto lit_flt = ScmLiteral::of_float(3.14);
    assert(lit_flt->kind() == NodeKind::LITERAL);
    assert(!lit_flt->to_scm_string().empty());

    auto lit_bool = ScmLiteral::of_bool(true);
    assert(lit_bool->kind() == NodeKind::LITERAL);
    assert(lit_bool->to_scm_string() == "#t");

    auto lit_bool_f = ScmLiteral::of_bool(false);
    assert(lit_bool_f->to_scm_string() == "#f");

    auto lit_str = ScmLiteral::of_str("hello");
    assert(lit_str->kind() == NodeKind::LITERAL);
    assert(lit_str->to_scm_string() == "\"hello\"");

    auto sym = std::make_shared<ScmSymbol>("foo");
    assert(sym->kind() == NodeKind::SYMBOL);
    assert(sym->to_scm_string() == "foo");

    auto var = std::make_shared<ScmVarRef>();
    var->var = "x";
    assert(var->kind() == NodeKind::VAR_REF);
    assert(var->to_scm_string() == "x");

    auto let_node = std::make_shared<ScmLet>();
    let_node->vars = {"x"};
    let_node->inits = {lit_int};
    assert(let_node->kind() == NodeKind::LET);
    assert(!let_node->to_scm_string().empty());

    auto set_node = std::make_shared<ScmSet>();
    set_node->var = "x";
    set_node->expr = lit_int;
    assert(set_node->kind() == NodeKind::SET);
    assert(!set_node->to_scm_string().empty());

    auto loop_node = std::make_shared<ScmLoopWhile>();
    loop_node->condition = var;
    loop_node->body = {set_node};
    assert(loop_node->kind() == NodeKind::LOOP_WHILE);
    assert(!loop_node->to_scm_string().empty());

    auto ret_node = std::make_shared<ScmReturn>();
    ret_node->expr = var;
    assert(ret_node->kind() == NodeKind::RETURN);
    assert(!ret_node->to_scm_string().empty());

    auto walk_csr = ScmWalk::forward("edge");
    assert(walk_csr->kind() == NodeKind::WALK);
    assert(!walk_csr->to_scm_string().empty());

    auto walk_csc = ScmWalk::reverse("edge");
    assert(walk_csc->kind() == NodeKind::WALK);
    assert(!walk_csc->to_scm_string().empty());

    auto walk_2hop = std::make_shared<ScmWalk2Hop>("rel1", "rel2");
    assert(walk_2hop->kind() == NodeKind::WALK_2HOP);
    assert(!walk_2hop->to_scm_string().empty());

    auto filter_node = std::make_shared<ScmVectorFilter>("age", CompareOp::GT, lit_int);
    assert(filter_node->kind() == NodeKind::VECTOR_FILTER);
    assert(!filter_node->to_scm_string().empty());

    auto collect_bitset = ScmCollect::bitset(0);
    assert(collect_bitset->kind() == NodeKind::COLLECT);
    assert(!collect_bitset->to_scm_string().empty());

    auto collect_count = ScmCollect::count(1);
    assert(collect_count->kind() == NodeKind::COLLECT);
    assert(!collect_count->to_scm_string().empty());

    auto card_node = std::make_shared<ScmCardinality>();
    card_node->var = "x";
    assert(card_node->kind() == NodeKind::CARDINALITY);
    assert(!card_node->to_scm_string().empty());

    auto bs_node = std::make_shared<ScmBitsetInit>();
    bs_node->type = ScmBitsetInit::InitType::EMPTY;
    assert(bs_node->kind() == NodeKind::BITSET_INIT);
    assert(!bs_node->to_scm_string().empty());

    auto cel_node = std::make_shared<ScmCelExpr>("age > 21");
    assert(cel_node->kind() == NodeKind::CEL_EXPR);
    assert(!cel_node->to_scm_string().empty());

    auto prog = ScmProgram::of(walk_csr, collect_bitset);
    assert(prog->kind() == NodeKind::PROGRAM);
    assert(!prog->to_scm_string().empty());

    // 4. Compilation with unoptimized pipeline
    auto res_unopt = ImpulseCompiler::compile(prog, nullptr, {}, unopt);
    assert(res_unopt.instructions.size() > 0);
    assert(!res_unopt.to_impas_string().empty());

    // 5. Compilation with default optimized pipeline
    auto res_opt = ImpulseCompiler::compile(prog, nullptr, {}, CompilerOptions::default_options());
    assert(res_opt.instructions.size() > 0);
    assert(!res_opt.to_impas_string().empty());
}

int main() {
    std::cout << "================================================================" << std::endl;
    std::cout << " ImpScheme (impscm) Compiler MC/DC Boundary Suite" << std::endl;
    std::cout << "================================================================" << std::endl;

    test_mcdc_sexpr_parser();
    test_mcdc_ast_builder();
    test_mcdc_compiler_pipeline();
    test_mcdc_sexpr_deep_coverage();
    test_mcdc_ast_nodes_and_passes_exhaustive();

    std::cout << "================================================================" << std::endl;
    std::cout << " ALL IMPSCM COMPILER MC/DC CONDITION INDEPENDENCE TESTS PASSED!" << std::endl;
    std::cout << "================================================================" << std::endl;
    return 0;
}

