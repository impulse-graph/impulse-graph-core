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

    std::cout << "  -> PASSED: " << magic_name << " -> " << magic_ast << std::endl;
}

int main() {
    std::cout << "=== Impulse C++20 Datalog (ImpLog) Compiler Test Suite ===" << std::endl;
    test_datalog_valid_stratification();
    test_datalog_stratification_failure();
    test_magic_sets_transformation();
    std::cout << "=== ALL DATALOG TESTS PASSED ===" << std::endl;
    return 0;
}
