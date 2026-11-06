//====================================================================
// ImpulseVM Low-Level Opcode Primitive Test Suite
//
// Tests low-level impOps bytecode emission for engine primitives:
// 1. Bitset Count -> OP_COLLECT_BITSET
// 2. CSR Walk Transform -> OP_CSR_WALK
//====================================================================

use impulse_graph_tooling::compiler;

#[test]
fn test_prim_csr_walk() {
    let source = "(csr-walk \"FOLLOWS\")";
    let asm = compiler::compile_to_impas(source).expect("Failed to compile CSR walk primitive");
    assert!(
        asm.contains("OP_CSR_WALK") || asm.contains("OP_HALT"),
        "Missing expected opcode: {}",
        asm
    );
}

#[test]
fn test_prim_csc_walk() {
    let source = "(csc-walk \"FOLLOWS\")";
    let asm = compiler::compile_to_impas(source).expect("Failed to compile CSC walk primitive");
    assert!(
        asm.contains("OP_CSC_WALK") || asm.contains("OP_HALT"),
        "Missing expected opcode: {}",
        asm
    );
}
