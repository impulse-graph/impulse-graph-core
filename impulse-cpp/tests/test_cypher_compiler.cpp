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

    // 4. Test Unbounded Range Error Check (*1.. without upper bound) & Unbounded Fixpoint (* without range)
    try {
        CypherParser::parse("MATCH (a)-[:Rel*1..]->(b) RETURN b");
        assert(false && "Should have thrown error for incomplete range *1..");
    } catch (const std::exception& e) {
        std::cout << "  ✓ Caught expected incomplete range rejection: " << e.what() << std::endl;
    }

    auto res_fp = CypherCompiler::compile("MATCH (a:Node)-[:Rel*]->(b:Node) WHERE a.id = 0 RETURN b");
    std::string s_fp = res_fp.ast->to_scm_string();
    assert(s_fp.find("(fixpoint") != std::string::npos);
    assert(s_fp.find("(csr-walk \"Rel\")") != std::string::npos);
    std::cout << "  ✓ Unbounded Kleene star [:Rel*] lowers to ScmFixpoint: PASSED\n" << std::endl;

    // 5. Test RETURN count, WHERE with AND, comparisons, and variable-length hops
    std::string q3 = "// Leading comment\nMATCH (a:Person)-[:KNOWS*1..3]->(b:Person) WHERE a.age >= 21 AND b.active != 0 AND b.flagged == 0 RETURN count(b)";
    auto res3 = CypherCompiler::compile(q3);
    assert(res3.ast != nullptr);
    std::string s_expr3 = res3.ast->to_scm_string();
    assert(s_expr3.find("(collect-count)") != std::string::npos);
    std::cout << "  ✓ Q3 count & complex WHERE: PASSED\n" << std::endl;

    // 6. Test float literals, single-quoted strings, and == / <> operators
    std::string q4 = "MATCH (x:Item)-[:IN_CATEGORY*..2]->(c:Cat) WHERE x.price <= 19.99 AND c.name == 'Electronics' AND x.tag <> 'old' RETURN c";
    auto res4 = CypherCompiler::compile(q4);
    assert(res4.ast != nullptr);
    std::cout << "  ✓ Q4 float literals & equality: PASSED\n" << std::endl;

    // 6b. Fixed hop test: [:Rel*2]
    std::string q5 = "MATCH (a:A)-[:Rel*2]->(b:B) WHERE a.id = $id RETURN b";
    auto res5 = CypherCompiler::compile(q5);
    assert(res5.ast != nullptr);
    std::cout << "  ✓ Q5 fixed multi-hop: PASSED\n" << std::endl;

    // 7. Error handling paths
    // Missing MATCH
    try {
        CypherParser::parse("WHERE a.id = 1 RETURN a");
        assert(false);
    } catch (const std::exception&) {}

    // Missing RETURN
    try {
        CypherParser::parse("MATCH (a:Node)-[:Rel]->(b:Node)");
        assert(false);
    } catch (const std::exception&) {}

    // Missing closing paren
    try {
        CypherParser::parse("MATCH (a:Node-[:Rel]->(b:Node) RETURN b");
        assert(false);
    } catch (const std::exception&) {}

    // Unexpected character
    try {
        CypherLexer lexer("@invalid");
        lexer.next_token();
    } catch (const std::exception&) {}

    // 8. Multi-seed IN predicate + quorum filter lowering (0xC0 -> 0xC3 -> 0xC6)
    {
        std::string q_ml = "MATCH (a:Entity)-[:REL]->(b:Entity) WHERE a.id IN [0, 1, 2] AND quorum >= 2 RETURN b";
        auto res_ml = CypherCompiler::compile(q_ml);
        std::string s_ml = res_ml.ast->to_scm_string();
        assert(s_ml.find("(lane-init-seeds 0 0)") != std::string::npos);
        assert(s_ml.find("(lane-walk-csr \"REL\")") != std::string::npos);
        assert(s_ml.find("(lane-filter-quorum 2 255)") != std::string::npos);

        auto seeds_node = std::dynamic_pointer_cast<ScmLaneInitSeeds>(res_ml.ast->steps[0]);
        assert(seeds_node != nullptr && seeds_node->inline_seeds.size() == 3);

        // Also test WITH b, count(a) AS c WHERE c >= 2 syntax
        std::string q_with = "MATCH (a:Entity)-[:REL]->(b:Entity) WHERE a.id IN [0, 1] WITH b, count(a) AS c WHERE c >= 2 RETURN b";
        auto res_with = CypherCompiler::compile(q_with);
        std::string s_with = res_with.ast->to_scm_string();
        assert(s_with.find("(lane-init-seeds 0 0)") != std::string::npos);
        assert(s_with.find("(lane-walk-csr \"REL\")") != std::string::npos);
        assert(s_with.find("(lane-filter-quorum 2 255)") != std::string::npos);
        std::cout << "  ✓ Multi-seed IN + quorum / WITH count lowering (0xC0/0xC3/0xC6): PASSED\n" << std::endl;
    }

    // 9. Multi-seed unbounded Kleene star [:REL*] -> ScmLaneWalk with fixed_point = true (flags = 0x04)
    {
        std::string q_ml_fp = "MATCH (a:User)-[:KNOWS*]->(b:User) WHERE a.id IN [0, 1] AND quorum >= 2 RETURN b";
        auto res_ml_fp = CypherCompiler::compile(q_ml_fp);
        std::string s_ml_fp = res_ml_fp.ast->to_scm_string();
        assert(s_ml_fp.find("(lane-init-seeds 0 0)") != std::string::npos);
        assert(s_ml_fp.find("(lane-walk-csr \"KNOWS\" 1)") != std::string::npos);
        assert(s_ml_fp.find("(lane-filter-quorum 2 255)") != std::string::npos);

        auto walk_node = std::dynamic_pointer_cast<ScmLaneWalk>(res_ml_fp.ast->steps[1]);
        assert(walk_node != nullptr && walk_node->fixed_point && walk_node->flags == 0x04);
        std::cout << "  ✓ Multi-seed unbounded [:KNOWS*] fixpoint lowering: PASSED\n" << std::endl;
    }

    // 10. Hybrid vector ANN search (0xCA) + exact reranking (0xCB)
    {
        std::string q_vec = "MATCH (a:Doc)-[:CITES]->(b:Doc) WHERE vector_ann(a.emb, [1.0, 0.0], 4) AND quorum >= 2 RETURN b ORDER BY vector_dot(b.emb, [1.0, 0.0]) DESC LIMIT 5";
        auto res_vec = CypherCompiler::compile(q_vec);
        std::string s_vec = res_vec.ast->to_scm_string();
        assert(s_vec.find("(vector-ann-search 2 4 0 1)") != std::string::npos);
        assert(s_vec.find("(lane-walk-csr \"CITES\")") != std::string::npos);
        assert(s_vec.find("(lane-filter-quorum 2 255)") != std::string::npos);
        assert(s_vec.find("(vector-rerank-dot 2 0 0)") != std::string::npos);
        std::cout << "  ✓ Hybrid vector_ann (0xCA) + ORDER BY vector_dot (0xCB) lowering: PASSED\n" << std::endl;
    }

    // 11. Issue #36: Undirected -[:REL]- and pipe-separated relation union -[:R1|R2]->
    {
        auto res_undir = CypherCompiler::compile("MATCH (a:Gene)-[:INTERACTS]-(b:Gene) WHERE a.id = 0 RETURN b");
        std::string s_undir = res_undir.ast->to_scm_string();
        assert(s_undir.find("(set:union (csr-walk \"INTERACTS\") (csc-walk \"INTERACTS\"))") != std::string::npos ||
               s_undir.find("(set-union (csr-walk \"INTERACTS\") (csc-walk \"INTERACTS\"))") != std::string::npos);

        auto res_union = CypherCompiler::compile("MATCH (a:Drug)-[:TREATS|PALLIATES]->(d:Disease) WHERE a.id = 0 RETURN d");
        std::string s_union = res_union.ast->to_scm_string();
        assert(s_union.find("(set:union (csr-walk \"TREATS\") (csr-walk \"PALLIATES\"))") != std::string::npos ||
               s_union.find("(set-union (csr-walk \"TREATS\") (csr-walk \"PALLIATES\"))") != std::string::npos);
        std::cout << "  ✓ Undirected -[:REL]- and relation union -[:R1|R2]-> lowering: PASSED\n" << std::endl;
    }

    std::cout << "\n=========================================================================\n";
    std::cout << "                   ALL C++ CYPHER PARSER TESTS PASSED!                   \n";
    std::cout << "=========================================================================\n";
    return 0;
}
