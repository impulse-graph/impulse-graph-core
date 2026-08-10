use impulse_compiler::compile_implog_to_impas;

#[test]
fn test_compile_implog_rule() {
    let source = r#"
        .decl member(User, Group)
        reachable(U, G) :- member(U, G).
    "#;

    let result = compile_implog_to_impas(source);
    assert!(result.is_ok(), "ImpLog compilation failed: {:?}", result);

    let asm = result.unwrap();
    assert!(asm.contains("implog_rule_query:"));
    assert!(asm.contains("OP_ENTER_FRAME"));
    assert!(asm.contains("OP_HALT"));
}
