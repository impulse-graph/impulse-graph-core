use impulse_compiler::{compile_impk_to_impas, compile_script_to_impas, LanguageTarget};

#[test]
fn test_compile_impk_query_basic() {
    let source = r#"
        fn reachability(g, start) {
            frontier: bitset start;
            visited: bitset start;
            next: g @[frontier; "FOLLOWS"];
            return next;
        }
    "#;

    let result = compile_impk_to_impas(source);
    assert!(result.is_ok(), "ImpK compilation failed: {:?}", result);

    let asm = result.unwrap();
    assert!(asm.contains("reachability:"));
    assert!(asm.contains("OP_ENTER_FRAME"));
    assert!(asm.contains("OP_CSR_WALK"));
    assert!(asm.contains("OP_HALT"));
}

#[test]
fn test_compile_impk_with_bitset_fusion() {
    let source = r#"
        fn filtered_search(g, start) {
            frontier: bitset start;
            mask: bitset start;
            next: (g @[frontier; "FOLLOWS"]) &~ mask;
            return next;
        }
    "#;

    let result = compile_script_to_impas(source, LanguageTarget::ImpK);
    assert!(result.is_ok());

    let asm = result.unwrap();
    assert!(asm.contains("filtered_search:"));
    assert!(asm.contains("OP_ENTER_FRAME"));
}
