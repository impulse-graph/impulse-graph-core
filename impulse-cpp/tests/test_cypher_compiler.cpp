#include "impulse_cypher.hpp"
#include <iostream>
#include <cassert>

using namespace impulse::compiler;

int main() {
    std::cout << "=========================================================================\n";
    std::cout << "               IMPULSE GRAPH C++ CYPHER COMPILER TEST                    \n";
    std::cout << "=========================================================================\n\n";

    // 1. Test standard multi-hop path pattern
    std::string q1 = "MATCH (d:Disease)-[:DaG]->(g1:Gene)-[:GpPW]->(p:Pathway)<-[:GpPW]-(g2:Gene)<-[:CbG]-(c:Compound) WHERE d.id = $diseaseId RETURN c";
    auto res1 = CypherCompiler::compile(q1);
    std::string s_expr1 = res1.ast->to_scm_string();

    std::cout << "Parsed S-Expression for Q1:\n" << s_expr1 << "\n" << std::endl;
    assert(s_expr1.find("(csr-walk \"DaG\")") != std::string::npos);
    assert(s_expr1.find("(csr-walk \"GpPW\")") != std::string::npos);
    assert(s_expr1.find("(csc-walk \"GpPW\")") != std::string::npos);
    assert(s_expr1.find("(csc-walk \"CbG\")") != std::string::npos);
    assert(s_expr1.find("(collect-bitset)") != std::string::npos);
    assert(res1.seed_variable == "d");
    assert(res1.seed_param_or_value == "$diseaseId");
    std::cout << "  ✓ Q1 openCypher parsing & ImpScheme lowering: PASSED\n" << std::endl;

    // 2. Test DRKG backticked names
    std::string q2 = "MATCH (c1:Compound)-[:`DRUGBANK::ddi_interactor_in`]->(c2:Compound)-[:`GNBR::C`]->(s:SideEffect) WHERE c1.id = $compoundId RETURN s";
    auto res2 = CypherCompiler::compile(q2);
    std::string s_expr2 = res2.ast->to_scm_string();

    std::cout << "Parsed S-Expression for Q2:\n" << s_expr2 << "\n" << std::endl;
    assert(s_expr2.find("(csr-walk \"DRUGBANK::ddi_interactor_in\")") != std::string::npos);
    assert(s_expr2.find("(csr-walk \"GNBR::C\")") != std::string::npos);
    assert(s_expr2.find("(collect-bitset)") != std::string::npos);
    std::cout << "  ✓ DRKG Backtick relation parsing: PASSED\n" << std::endl;

    // 3. Test Mandatory Typed Walk Error Check
    try {
        CypherParser::parse("MATCH (a)-->(b) RETURN b");
        assert(false && "Should have thrown error for untyped walk");
    } catch (const std::exception& e) {
        std::cout << "  ✓ Caught expected untyped walk rejection: " << e.what() << std::endl;
    }

    // 4. Test Unbounded Wildcard Error Check
    try {
        CypherParser::parse("MATCH (a)-[:Rel*]->(b) RETURN b");
        assert(false && "Should have thrown error for unbounded walk");
    } catch (const std::exception& e) {
        std::cout << "  ✓ Caught expected unbounded walk rejection: " << e.what() << std::endl;
    }

    std::cout << "\n=========================================================================\n";
    std::cout << "                   ALL C++ CYPHER PARSER TESTS PASSED!                   \n";
    std::cout << "=========================================================================\n";
    return 0;
}
