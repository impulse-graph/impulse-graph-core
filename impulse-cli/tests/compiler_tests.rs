use impulse_graph_tooling::compiler::{self, LanguageTarget};

#[test]
fn test_compile_cypher_to_impas() {
    let source = "MATCH (u:User)-[:Follows]->(v:User) RETURN v";
    let asm_res = compiler::compile_script_to_impas(source, LanguageTarget::Cypher);
    assert!(asm_res.is_ok(), "compile Cypher failed: {:?}", asm_res);

    let asm = asm_res.unwrap();
    assert!(
        asm.contains(".version"),
        "Assembly missing .version header: {}",
        asm
    );
    assert!(
        asm.contains("OP_CSR_WALK") || asm.contains("OP_ADAPTIVE_WALK"),
        "Assembly missing traversal opcode: {}",
        asm
    );
    assert!(asm.contains("OP_HALT"), "Assembly missing OP_HALT: {}", asm);
}

#[test]
fn test_compile_implog_to_impas() {
    let source = "can_view(U, D) :- member(U, G), group_parent(G, D).";
    let asm_res = compiler::compile_script_to_impas(source, LanguageTarget::ImpLog);
    assert!(asm_res.is_ok(), "compile ImpLog failed: {:?}", asm_res);

    let asm = asm_res.unwrap();
    assert!(
        asm.contains(".version"),
        "Assembly missing .version header: {}",
        asm
    );
    assert!(
        asm.contains("OP_CSR_WALK")
            || asm.contains("OP_ADAPTIVE_WALK")
            || asm.contains("OP_COLLECT_BITSET"),
        "Assembly missing expected instructions: {}",
        asm
    );
    assert!(asm.contains("OP_HALT"), "Assembly missing OP_HALT: {}", asm);
}

#[test]
fn test_compile_impk_to_impas() {
    let source = "v = A @ x";
    let asm_res = compiler::compile_script_to_impas(source, LanguageTarget::ImpK);
    assert!(asm_res.is_ok(), "compile ImpK failed: {:?}", asm_res);

    let asm = asm_res.unwrap();
    assert!(
        asm.contains(".version"),
        "Assembly missing .version header: {}",
        asm
    );
    assert!(
        asm.contains("OP_CSR_WALK") || asm.contains("OP_MXV") || asm.contains("OP_ADAPTIVE_WALK"),
        "Assembly missing traversal opcode: {}",
        asm
    );
}

#[test]
fn test_compile_impscm_to_impas() {
    let source = "(csr-walk \"edge\")";
    let asm_res = compiler::compile_to_impas(source);
    assert!(asm_res.is_ok(), "compile ImpScheme failed: {:?}", asm_res);

    let asm = asm_res.unwrap();
    assert!(
        asm.contains(".version"),
        "Assembly missing .version header: {}",
        asm
    );
    assert!(asm.contains("OP_HALT"), "Assembly missing OP_HALT: {}", asm);
}
