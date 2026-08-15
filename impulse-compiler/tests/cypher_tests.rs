/**
 * @file cypher_tests.rs
 * @brief Unit tests for Simplified openCypher Frontend compilation to impOps bytecode.
 */

use impulse_compiler::frontends::cypher::{parse_cypher, compile_cypher_to_impscm};

#[test]
fn test_compile_simplified_cypher_query() {
    let script = r#"
        MATCH (u:User)-[r:Follows]->(v:User)
        WHERE u.age >= 21 AND u.age <= 25 AND u.state = 'HI' AND u.email CONTAINS 'gmail.com'
        RETURN v
    "#;

    let query = parse_cypher(script).expect("Failed to parse Cypher query");
    assert_eq!(query.src_label, "User");
    assert_eq!(query.rel_label, "Follows");
    assert_eq!(query.tgt_label, "User");
    assert_eq!(query.where_clauses.len(), 4);

    let ir = compile_cypher_to_impscm(&query);
    let ir_str = format!("{:?}", ir);

    assert!(ir_str.contains("index-lookup-inverted-bitset"));
    assert!(ir_str.contains("index-lookup-trigram"));
    assert!(ir_str.contains("csr-walk-filtered"));
    println!("Compiled Cypher ImpScheme IR:\n{}", ir_str);
}
