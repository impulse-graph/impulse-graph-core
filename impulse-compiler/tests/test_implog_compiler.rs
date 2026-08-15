use impulse_compiler::{compile_implog_to_impas, compile_script_with_options, CompilerOptions, LanguageTarget};
use impulse_compiler::passes::magic_sets::MagicSetTransformation;
use impulse_compiler::passes::fusion::OpcodeFusionPass;
use impulse_compiler::ir::ast::SExpr;

#[test]
fn test_compile_implog_benchmark_query() {
    let source = r#"
        .decl User(u)
        .decl Follows(u, v)
        .decl Member(u, g)
        .decl Revoked(u)

        ValidActiveMember(u, g) :- Member(u, g), !Revoked(u), ValidAt(T, start, dur).
        AuditLog(u, doc, {action: "READ", status: "PERMITTED"}) :- ValidActiveMember(u, doc).
        ShortestPath(y, min<d>) :- Follows(x, y), Path(x, d).
        SimilarDocument(d1, d2) :- Doc(d1), CosineSim(v1, v2, 0.85).
        TopInfluencers(u, v) :- Follows(u, v), Choice(top<10>).
    "#;

    let result = compile_implog_to_impas(source);
    assert!(result.is_ok(), "ImpLog benchmark query compilation failed: {:?}", result);

    let asm = result.unwrap();
    assert!(asm.contains("implog_rule_query:"));
}

#[test]
fn test_magic_sets_transformation() {
    let (magic_pred, ast) = MagicSetTransformation::transform_query("Reachable", "42");
    assert_eq!(magic_pred, "m_Reachable_b");
    match ast {
        SExpr::List(terms) => {
            assert_eq!(terms.len(), 3);
            assert_eq!(terms[0], SExpr::Symbol("magic:seed".to_string()));
            assert_eq!(terms[1], SExpr::Symbol("m_Reachable_b".to_string()));
            assert_eq!(terms[2], SExpr::Str("42".to_string()));
        }
        _ => panic!("Expected SExpr::List"),
    }
}

#[test]
fn test_jit_opcode_fusion() {
    let asm_lines = vec![
        "    OP_CSR_WALK                 R5, R2, REL_MEMBER",
        "    OP_ROARING_BITMAP_AND_NOT   R4, R5, R3",
        "    OP_LOAD_CONST_INT           R63, 1"
    ];

    let fused = OpcodeFusionPass::fuse_assembly_instructions(asm_lines);
    assert_eq!(fused.len(), 2);
    assert!(fused[0].contains("OP_CSR_WALK_FILTERED"));
    assert!(fused[0].contains("[JIT Fused Walk+Diff]"));
}

#[test]
fn test_compiler_options_flag_toggle() {
    let source = r#"
        ValidActiveMember(u, g) :- Member(u, g), !Revoked(u).
    "#;

    // Compile with all optimizations disabled (O0)
    let asm_o0 = compile_script_with_options(source, LanguageTarget::ImpLog, CompilerOptions::none()).unwrap();
    assert!(!asm_o0.contains("[JIT Fused Walk+Diff]"));
    assert!(asm_o0.contains("OP_CSR_WALK"));
    assert!(asm_o0.contains("OP_ROARING_BITMAP_AND_NOT"));

    // Compile with JIT fusion enabled (O2/O3)
    let asm_opt = compile_script_with_options(source, LanguageTarget::ImpLog, CompilerOptions::default()).unwrap();
    assert!(asm_opt.contains("OP_CSR_WALK_FILTERED"));
    assert!(asm_opt.contains("[JIT Fused Walk+Diff]"));
}
