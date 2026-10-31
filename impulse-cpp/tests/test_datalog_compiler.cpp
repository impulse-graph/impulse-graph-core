/**
 * @file test_datalog_compiler.cpp
 * @brief Unit Tests for Zero-Dependency C++20 Datalog (ImpLog) Frontend Compiler & Stratification.
 */

#include "impulse_datalog.hpp"
#include <iostream>
#include <cassert>

using namespace impulse::datalog;
using namespace impulse::compiler;

static void test_datalog_valid_stratification() {
    std::cout << "[Test] Datalog Valid Stratified Rule Parsing..." << std::endl;

    std::string datalog_src = R"(
        // Multi-hop transitive reachability
        path(X, Y) :- edge(X, Z), edge(Z, Y).
        authorized(U, R) :- member_of(U, G), has_role(G, R).
    )";

    auto prog = DatalogParser::parse(datalog_src);
    assert(prog != nullptr);
    assert(prog->steps.size() >= 3);

    GraphCatalog catalog;
    catalog.register_relation("edge", 0);
    catalog.register_relation("member_of", 1);
    catalog.register_relation("has_role", 2);

    auto compiled = ImpulseCompiler::compile(prog, &catalog);
    assert(!compiled.instructions.empty());
    std::cout << "  -> PASSED: Generated " << compiled.instructions.size() << " instructions." << std::endl;
}

static void test_datalog_stratification_failure() {
    std::cout << "[Test] Datalog Non-Stratified Negation Detection..." << std::endl;

    std::string bad_datalog = R"(
        // Negative cycle: p depends on !q, q depends on !p
        p(X) :- not q(X).
        q(X) :- not p(X).
    )";

    bool caught = false;
    try {
        auto prog = DatalogParser::parse(bad_datalog);
    } catch (const std::runtime_error& e) {
        caught = true;
        std::cout << "  -> Expected rejection caught: " << e.what() << std::endl;
    }
    if (!caught) {
        std::cerr << "Assertion failed: expected exception" << std::endl;
        std::exit(1);
    }
}

static void test_magic_sets_transformation() {
    std::cout << "[Test] Datalog Magic Sets Query Transformation..." << std::endl;

    auto [magic_name, magic_ast] = MagicSetsTransformation::transform_query("path", "node_14726");
    assert(magic_name == "m_path_b");
    assert(magic_ast.find("node_14726") != std::string::npos);

    auto [multi_name, multi_node] = MagicSetsTransformation::transform_multi_seed("reach", {10ULL, 20ULL, 30ULL});
    assert(multi_name == "m_reach_lanes");
    assert(multi_node != nullptr);
    assert(multi_node->kind() == NodeKind::LANE_INIT_SEEDS);
    assert(multi_node->inline_seeds.size() == 3);
    assert(multi_node->inline_seeds[0] == 10ULL);
    assert(multi_node->inline_seeds[2] == 30ULL);

    auto reachable = MagicSetsTransformation::compute_reachable_predicates(
        "reach",
        {{"reach", "member_of", false}, {"unused", "other_rel", false}});
    assert(reachable.count("reach") == 1);
    assert(reachable.count("member_of") == 1);
    assert(reachable.count("unused") == 0);

    std::cout << "  -> PASSED: " << magic_name << " -> " << magic_ast << std::endl;
}

static void test_multiseed_recursive_and_inline_facts() {
    std::cout << "[Test] Multi-Seed Recursive Fixpoint (0xC0 + 0xC3 flags=0x04) & Inline Facts..." << std::endl;

    std::string rec_src = R"(
        reach(X, Y) :- member_of(X, Y).
        reach(X, Z) :- reach(X, Y), member_of(Y, Z).
    )";

    auto prog = DatalogParser::parse_multi_seed(rec_src, {0ULL, 1ULL});
    assert(prog != nullptr);
    assert(prog->steps.size() == 3);
    assert(prog->steps[0]->kind() == NodeKind::LANE_INIT_SEEDS);
    assert(prog->steps[1]->kind() == NodeKind::LANE_WALK);
    auto lw = std::static_pointer_cast<ScmLaneWalk>(prog->steps[1]);
    assert(lw->relation_name == "member_of");
    assert(lw->fixed_point == true);
    assert(lw->flags == 0x04);

    GraphCatalog catalog;
    catalog.register_relation("member_of", 0);
    catalog.register_relation("blocked", 1);

    auto compiled = ImpulseCompiler::compile(prog, &catalog);
    bool found_init_seeds = false;
    bool found_lane_fp_walk = false;
    for (const auto& inst : compiled.instructions) {
        if (inst.opcode == OP_LANE_INIT_SEEDS) found_init_seeds = true;
        if (inst.opcode == OP_LANE_CSR_WALK && inst.flags == 0x04) found_lane_fp_walk = true;
    }
    assert(found_init_seeds);
    assert(found_lane_fp_walk);

    // Inline .seed, .denied, and !blocked negation -> OP_LANE_INIT_SEEDS (0xC0) + OP_LANE_PRUNE_MASK (0xC5 mode 0)
    std::string inline_deny_src = R"(
        .seed(0, 1).
        .denied(2).
        allowed(X, Y) :- member_of(X, Y), !blocked(X, Y).
    )";
    auto deny_prog = DatalogParser::parse(inline_deny_src);
    assert(deny_prog->steps.size() == 4);
    assert(deny_prog->steps[0]->kind() == NodeKind::LANE_INIT_SEEDS);
    assert(deny_prog->steps[1]->kind() == NodeKind::LANE_WALK);
    assert(deny_prog->steps[2]->kind() == NodeKind::LANE_PRUNE_MASK);
    auto pm = std::static_pointer_cast<ScmLanePruneMask>(deny_prog->steps[2]);
    assert(pm->mode == 0);
    assert(pm->inline_deny_nodes.size() == 1 && pm->inline_deny_nodes[0] == 2ULL);

    // Inline .group -> OP_LANE_INIT_GROUPS (0xC1)
    std::string group_src = R"(
        .group(0, 0).
        .group(1, 1).
        reach(X, Y) :- member_of(X, Y).
    )";
    auto grp_prog = DatalogParser::parse(group_src);
    assert(grp_prog->steps[0]->kind() == NodeKind::LANE_INIT_GROUPS);
    auto ig = std::static_pointer_cast<ScmLaneInitGroups>(grp_prog->steps[0]);
    assert(ig->num_groups == 2);

    // Allow/deny split (0xC5 mode 1)
    auto split_prog = DatalogParser::parse_allow_deny_split(
        "can_access(X, Y) :- member_of(X, Y).", {0ULL, 1ULL}, {2ULL});
    bool found_mode1 = false;
    for (const auto& st : split_prog->steps) {
        if (st->kind() == NodeKind::LANE_PRUNE_MASK) {
            auto spm = std::static_pointer_cast<ScmLanePruneMask>(st);
            if (spm->mode == 1 && spm->arg0 == 2) found_mode1 = true;
        }
    }
    assert(found_mode1);
    std::cout << "  -> PASSED" << std::endl;
}

static void test_single_seed_recursive_and_negation() {
    std::cout << "[Test] Single-Seed Recursive Fixpoint (0x88) & Stratified Negation (DIFF)..." << std::endl;

    std::string rec_src = R"(
        reach(X, Y) :- member_of(X, Y).
        reach(X, Z) :- reach(X, Y), member_of(Y, Z).
    )";
    auto rec_prog = DatalogParser::parse(rec_src);
    assert(rec_prog->steps[0]->kind() == NodeKind::FIXPOINT);

    GraphCatalog catalog;
    catalog.register_relation("member_of", 0);
    catalog.register_relation("blocked", 1);

    auto rec_compiled = ImpulseCompiler::compile(rec_prog, &catalog);
    bool found_kleene = false;
    for (const auto& inst : rec_compiled.instructions) {
        if (inst.opcode == OP_FIXPOINT_KLEENE_STAR) found_kleene = true;
    }
    assert(found_kleene);

    std::string neg_src = "allowed(X, Y) :- member_of(X, Y), not blocked(X, Y).";
    auto neg_prog = DatalogParser::parse(neg_src);
    assert(neg_prog->steps[0]->kind() == NodeKind::SET_OP);
    auto so = std::static_pointer_cast<ScmSetOp>(neg_prog->steps[0]);
    assert(so->op == ScmSetOp::DIFFERENCE);

    auto neg_compiled = ImpulseCompiler::compile(neg_prog, &catalog);
    bool found_diff = false;
    for (const auto& inst : neg_compiled.instructions) {
        if (inst.opcode == OP_SET_DIFFERENCE) found_diff = true;
    }
    assert(found_diff);
    std::cout << "  -> PASSED" << std::endl;
}

int main() {
    std::cout << "=== Impulse C++20 Datalog (ImpLog) Compiler Test Suite ===" << std::endl;
    test_datalog_valid_stratification();
    test_datalog_stratification_failure();
    test_magic_sets_transformation();
    test_multiseed_recursive_and_inline_facts();
    test_single_seed_recursive_and_negation();
    std::cout << "=== ALL DATALOG TESTS PASSED ===" << std::endl;
    return 0;
}
